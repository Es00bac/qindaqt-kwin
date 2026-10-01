// SPDX-License-Identifier: GPL-2.0-or-later
#include "capturechild.h"
#include "capturelaunchpolicy.h"
#include "launchpaths.h"

#include "wayland/clientconnection.h"
#include "wayland/display.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

namespace KWin
{
CaptureChild::CaptureChild(Role role, quint64 generation, quint64 job, Display *display, QObject *parent)
    : QObject(parent)
    , m_role(role)
    , m_generation(generation)
    , m_job(job)
    , m_display(display)
{
    connect(&m_process, &QProcess::started, this, [this] {
        m_pid = m_process.processId();
        m_pidfd = FileDescriptor(int(syscall(SYS_pidfd_open, m_pid, 0)));
        releaseChildDescriptors();
        if (!m_pidfd || !CaptureWire::send(m_control.get(), CaptureWire::Public::Message::Hello, m_generation, m_job)) {
            fail();
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this] { fail(); });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this] { fail(); });
}

CaptureChild::~CaptureChild()
{
    lost = {};
    packetReceived = {};
    stop();
}

bool CaptureChild::makeControl()
{
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair)) {
        return false;
    }
    m_control = FileDescriptor(pair[0]);
    FileDescriptor child(pair[1]);
    if (!CaptureWire::enableCredentials(m_control.get())
        || !duplicateChildDescriptor(child.get(), CaptureWire::Public::ControlFd)) {
        return false;
    }
    m_notifier = new QSocketNotifier(m_control.get(), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] { handleReadable(); });
    return true;
}

bool CaptureChild::duplicateChildDescriptor(int input, int target)
{
    const int safe = fcntl(input, F_DUPFD_CLOEXEC, 8);
    if (safe < 0) {
        return false;
    }
    m_childDescriptors.at(target - CaptureWire::Public::ControlFd) = FileDescriptor(safe);
    return true;
}

bool CaptureChild::makeHelperDescriptors()
{
    auto makeWayland = [this](int target, QPointer<ClientConnection> &connection) {
        int pair[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair)) {
            return false;
        }
        connection = m_display->createClient(pair[0]);
        if (!connection) {
            close(pair[0]);
            close(pair[1]);
            return false;
        }
        FileDescriptor child(pair[1]);
        return duplicateChildDescriptor(child.get(), target);
    };
    if (!makeWayland(CaptureWire::Public::GuiWaylandFd, m_guiClient)
        || !makeWayland(CaptureWire::Public::CaptureWaylandFd, m_captureClient)) {
        return false;
    }
    int request[2];
    int result[2];
    if (pipe2(request, O_CLOEXEC)) {
        return false;
    }
    FileDescriptor requestRead(request[0]);
    m_requestWriter = FileDescriptor(request[1]);
    if (pipe2(result, O_CLOEXEC)) {
        return false;
    }
    m_resultReader = FileDescriptor(result[0]);
    FileDescriptor resultWrite(result[1]);
    return duplicateChildDescriptor(requestRead.get(), CaptureWire::Public::RequestReadFd)
        && duplicateChildDescriptor(resultWrite.get(), CaptureWire::Public::ResultWriteFd);
}

bool CaptureChild::start()
{
    const bool helper = m_role == Role::Helper;
    QString executable = QString::fromUtf8(helper ? QindaQt::CaptureAuthority::Wire::HelperExecutable
                                                   : QindaQt::CaptureAuthority::Wire::BrokerExecutable);
    const QString desktop = QString::fromUtf8(helper ? QindaQt::CaptureAuthority::Wire::HelperDesktop
                                                     : QindaQt::CaptureAuthority::Wire::BrokerDesktop);
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    const bool fixture = !m_testProgram.isEmpty();
    if (fixture) {
        executable = m_testProgram;
    }
#else
    const bool fixture = false;
#endif
    if (!CaptureLaunchPolicy::hasRestrictedPtracePolicy()
        || prctl(PR_GET_DUMPABLE) != 0
        || (!fixture && !CaptureLaunchPolicy::isInstalledCaptureEntry(desktop, executable, helper))
        || !makeControl() || (helper && !makeHelperDescriptors())) {
        fail();
        return false;
    }
    m_process.setProgram(executable);
    m_process.setArguments({});
    m_process.setWorkingDirectory(QStringLiteral("/"));
    m_process.setProcessEnvironment(CaptureLaunchPolicy::environment(QProcessEnvironment::systemEnvironment(), helper));
    QProcess::UnixProcessParameters parameters;
    parameters.flags = QProcess::UnixProcessFlag::CloseFileDescriptors | QProcess::UnixProcessFlag::ResetIds
        | QProcess::UnixProcessFlag::DisableCoreDumps | QProcess::UnixProcessFlag::ResetSignalHandlers;
    parameters.lowestFileDescriptorToClose = 8;
    m_process.setUnixProcessParameters(parameters);
    const std::array<int, 5> descriptors = {
        m_childDescriptors[0].get(), m_childDescriptors[1].get(), m_childDescriptors[2].get(),
        m_childDescriptors[3].get(), m_childDescriptors[4].get(),
    };
    m_process.setChildProcessModifier([this, descriptors, helper] {
        const int count = helper ? 5 : 1;
        for (int i = 0; i < count; ++i) {
            if (dup2(descriptors[i], i + CaptureWire::Public::ControlFd) < 0
                || fcntl(i + CaptureWire::Public::ControlFd, F_SETFD, 0) < 0) {
                m_process.failChildProcessModifier("private capture descriptor", errno);
            }
        }
    });
    m_process.start();
    return true;
}

void CaptureChild::releaseChildDescriptors()
{
    for (auto &descriptor : m_childDescriptors) {
        descriptor.reset();
    }
}

bool CaptureChild::live() const
{
    if (m_failed || !m_pidfd || m_process.state() == QProcess::NotRunning) {
        return false;
    }
    pollfd event{m_pidfd.get(), POLLIN, 0};
    return poll(&event, 1, 0) == 0;
}

bool CaptureChild::send(CaptureWire::Public::Message type, const QByteArray &payload, const std::vector<int> &fds)
{
    return sendJob(type, m_job, payload, fds);
}

bool CaptureChild::sendJob(CaptureWire::Public::Message type, quint64 job, const QByteArray &payload, const std::vector<int> &fds)
{
    return live() && CaptureWire::send(m_control.get(), type, m_generation, job, payload, fds);
}

FileDescriptor CaptureChild::takeRequestWriter()
{
    return std::move(m_requestWriter);
}

FileDescriptor CaptureChild::takeResultReader()
{
    return std::move(m_resultReader);
}

void CaptureChild::handleReadable()
{
    while (!m_failed) {
        CaptureWire::Packet packet;
        const int result = CaptureWire::receive(m_control.get(), &packet);
        if (result == 0) {
            return;
        }
        if (result < 0 || !live() || packet.uid != geteuid() || packet.pid != m_pid
            || packet.generation != m_generation
            || (m_role == Role::Helper && packet.job != m_job)) {
            fail();
            return;
        }
        if (packetReceived) {
            packetReceived(std::move(packet));
        }
    }
}

void CaptureChild::fail()
{
    if (m_failed) {
        return;
    }
    m_failed = true;
    if (m_notifier) {
        m_notifier->setEnabled(false);
    }
    releaseChildDescriptors();
    if (lost) {
        lost();
    }
}

void CaptureChild::stop()
{
    fail();
    m_control.reset();
    m_pidfd.reset();
    m_requestWriter.reset();
    m_resultReader.reset();
    const QPointer<ClientConnection> gui = m_guiClient;
    const QPointer<ClientConnection> capture = m_captureClient;
    m_guiClient = nullptr;
    m_captureClient = nullptr;
    if (capture) {
        capture->destroy();
    }
    if (gui) {
        gui->destroy();
    }
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
void CaptureChild::setTestProgram(const QString &program)
{
    Q_ASSERT(m_process.state() == QProcess::NotRunning);
    m_testProgram = program;
}
#endif
}
