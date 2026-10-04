// SPDX-License-Identifier: GPL-2.0-or-later
#include "lockerlauncher.h"
#include "lockerlaunchpolicy.h"
#include "utils/serviceutils.h"
#include "wayland/clientconnection.h"
#include "wayland/display.h"
#include <KService>
#include <QFileInfo>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
namespace KWin
{
LockerLauncher::LockerLauncher(Display *display, QObject *parent)
    : QObject(parent)
    , m_display(display)
{
    connect(&m_process, &QProcess::started, this, [this] {
        if (m_childFd >= 0) {
            close(m_childFd);
            m_childFd = -1;
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_error = QStringLiteral("Native locker launch failed");
        releaseConnection();
    });
    connect(&m_process, &QProcess::finished, this, [this] {
        releaseConnection();
    });
}
LockerLauncher::~LockerLauncher()
{
    disconnect(&m_process, nullptr, this, nullptr);
    releaseConnection();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}
void LockerLauncher::releaseConnection()
{
    if (m_childFd >= 0) {
        close(m_childFd);
        m_childFd = -1;
    }
    // Clear ownership before resource callbacks, so child failure or display
    // shutdown cannot recursively destroy this same wl_client.
    const auto connection = std::exchange(m_connection, {});
    if (connection) {
        connection->destroy();
    }
}
bool LockerLauncher::ownsConnection(ClientConnection *client) const
{
    return client && client == m_connection && m_process.state() != QProcess::NotRunning;
}
bool LockerLauncher::start()
{
    if (m_process.state() != QProcess::NotRunning) {
        return bool(m_connection);
    }
    releaseConnection();
    m_error.clear();
    if (!hasRestrictedPtracePolicy() || !protectLockAuthorityProcess()) {
        m_error = QStringLiteral("Native locking requires restricted kernel ptrace and core protection");
        return false;
    }
    QString executable = QStringLiteral(QINDAQT_SESSION_LOCK_EXECUTABLE);
#if defined(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
    if (!m_testProgram.isEmpty()) {
        executable = m_testProgram;
    }
    const bool fixture = !m_testProgram.isEmpty();
#else
    const bool fixture = false;
#endif
    const QString desktop = QStringLiteral(QINDAQT_SESSION_LOCK_DESKTOP_FILE);
    if (!fixture && (!isRootManagedLockerFile(executable) || !QFileInfo(executable).isExecutable() || !isRootManagedLockerFile(desktop))) {
        m_error = QStringLiteral("Native locker executable or permission entry is unavailable or writable");
        return false;
    }
    KService entry(desktop);
    const auto command = QProcess::splitCommand(entry.exec());
    if (!fixture && (command.size() != 1 || QFileInfo(command.first()).canonicalFilePath() != QFileInfo(executable).canonicalFilePath() || !readServicePermissionList(entry, s_waylandInterfaceName).contains(QStringLiteral("ext_session_lock_manager_v1")))) {
        m_error = QStringLiteral("Native locker permission entry does not grant the standard lock interface");
        return false;
    }
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair)) {
        m_error = QStringLiteral("Cannot create private native locker connection");
        return false;
    }
    m_connection = m_display->createClient(pair[0]);
    if (!m_connection) {
        close(pair[0]);
        close(pair[1]);
        m_error = QStringLiteral("Cannot admit private native locker connection");
        return false;
    }
    m_childFd = pair[1];
    m_process.setProgram(executable);
    m_process.setArguments({});
    m_process.setProcessEnvironment(lockerEnvironment(QProcessEnvironment::systemEnvironment(), 3));
    m_process.setWorkingDirectory(QStringLiteral("/"));
    QProcess::UnixProcessParameters parameters;
    parameters.flags = QProcess::UnixProcessFlag::CloseFileDescriptors | QProcess::UnixProcessFlag::ResetIds
        | QProcess::UnixProcessFlag::DisableCoreDumps | QProcess::UnixProcessFlag::ResetSignalHandlers;
    parameters.lowestFileDescriptorToClose = 4;
    m_process.setUnixProcessParameters(parameters);
    const int childFd = m_childFd;
    m_process.setChildProcessModifier([this, childFd] {
        if (dup2(childFd, 3) < 0 || fcntl(3, F_SETFD, 0) < 0) {
            m_process.failChildProcessModifier("private locker fd", errno);
        }
    });
    m_process.start();
    return true;
}
#if defined(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
void LockerLauncher::setTestProgram(const QString &program)
{
    Q_ASSERT(m_process.state() == QProcess::NotRunning);
    m_testProgram = program;
}
QByteArray LockerLauncher::readTestOutput()
{
    return m_process.readAllStandardOutput() + m_process.readAllStandardError();
}
qint64 LockerLauncher::writeTestInput(const QByteArray &input)
{
    return m_process.write(input);
}
void LockerLauncher::stopTestFixture()
{
    m_process.kill();
    m_process.waitForFinished(1000);
}
#endif

}
