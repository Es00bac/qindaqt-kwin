// SPDX-License-Identifier: GPL-2.0-or-later
#include "screenshotoperation.h"

#include "../../../qindaqt/capture-authority/captureauthority.h"
#include "screenshotlogging.h"
#include "utils/serviceutils.h"
#include "wayland_server.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QSocketNotifier>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace KWin
{
namespace
{
const QString s_errorNotAuthorized = QStringLiteral("org.kde.KWin.ScreenShot2.Error.NoAuthorized");
const QString s_errorNotAuthorizedMessage = QStringLiteral("The process is not authorized to take a screenshot");
const QString s_errorCancelled = QStringLiteral("org.kde.KWin.ScreenShot2.Error.Cancelled");
const QString s_errorCancelledMessage = QStringLiteral("Screenshot got cancelled");
}

#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
static QObject *s_testReceiver = nullptr;
static std::function<void(quint64, ScreenShotTestEvent)> s_testObserver;
void setScreenShotTestObserver(QObject *receiver, std::function<void(quint64, ScreenShotTestEvent)> observer)
{
    s_testReceiver = receiver;
    s_testObserver = std::move(observer);
}
#endif

struct ScreenShotWriteState
{
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    std::function<void(ScreenShotTestEvent)> observe;
#endif
    std::mutex barrier;
    bool active = true;
    FileDescriptor wakeRead;
    FileDescriptor wakeWrite;
    std::chrono::steady_clock::time_point deadline;

    explicit ScreenShotWriteState(bool protectedCall)
    {
        int pair[2];
        if (pipe2(pair, O_CLOEXEC | O_NONBLOCK) == 0) {
            wakeRead = FileDescriptor(pair[0]);
            wakeWrite = FileDescriptor(pair[1]);
        }
        deadline = protectedCall
            ? std::chrono::steady_clock::now()
                + std::chrono::milliseconds(QindaQt::CaptureAuthority::Wire::ProtectedForkMilliseconds)
            : std::chrono::steady_clock::time_point::max();
    }

    void revoke()
    {
        {
            // A completed revoke excludes every later nonblocking write.
            std::lock_guard guard(barrier);
            active = false;
        }
        if (wakeWrite) {
            const char byte = 1;
            (void)::write(wakeWrite.get(), &byte, 1);
        }
    }
};

class ScreenShotWriter2 : public QRunnable
{
public:
    ScreenShotWriter2(FileDescriptor &&pipe, QImage image,
                      std::shared_ptr<ScreenShotWriteState> state,
                      QPointer<ScreenShotSinkPipe2> sink)
        : m_pipe(std::move(pipe))
        , m_image(std::move(image))
        , m_state(std::move(state))
        , m_sink(sink)
    {
    }

    void run() override
    {
        bool success = false;
        if (m_pipe && m_state->wakeRead) {
            const int flags = fcntl(m_pipe.get(), F_GETFL, 0);
            if (flags >= 0 && fcntl(m_pipe.get(), F_SETFL, flags | O_NONBLOCK) == 0) {
                const char *bytes = reinterpret_cast<const char *>(m_image.constBits());
                qsizetype offset = 0;
                const qsizetype size = m_image.sizeInBytes();
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
                bool firstWait = true;
#endif
                while (offset < size) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= m_state->deadline) {
                        break;
                    }
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(m_state->deadline - now);
                    pollfd events[2] = {{m_pipe.get(), POLLOUT, 0}, {m_state->wakeRead.get(), POLLIN, 0}};
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
                    if (firstWait) {
                        firstWait = false;
                        if (m_state->observe) m_state->observe(ScreenShotTestEvent::WriterWaiting);
                    }
#endif
                    const int result = poll(events, 2, int(std::min<std::int64_t>(remaining.count(), 1000)));
                    if (result < 0) {
                        if (errno == EINTR) continue;
                        break;
                    }
                    if (result == 0) continue;
                    if (events[1].revents || !(events[0].revents & POLLOUT)) break;
                    std::lock_guard guard(m_state->barrier);
                    if (!m_state->active || std::chrono::steady_clock::now() >= m_state->deadline) break;
                    const size_t chunk = size_t(std::min<qsizetype>(size - offset, 65536));
                    const ssize_t count = ::write(m_pipe.get(), bytes + offset, chunk);
                    if (count > 0) {
                        offset += count;
                    } else if (count < 0 && errno != EAGAIN && errno != EINTR) {
                        break;
                    }
                }
                std::lock_guard guard(m_state->barrier);
                success = offset == size && m_state->active
                    && std::chrono::steady_clock::now() < m_state->deadline;
            }
        }
        // Closing before the success callback makes the public port's EOF
        // observable before the protected D-Bus success reply.
        m_pipe.reset();
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
        if (m_state->observe) m_state->observe(ScreenShotTestEvent::WriterExited);
#endif
        if (auto *app = QCoreApplication::instance()) {
            const auto sink = m_sink;
            QMetaObject::invokeMethod(app, [sink, success] {
                if (sink) sink->writerFinished(success);
            }, Qt::QueuedConnection);
        }
    }

private:
    FileDescriptor m_pipe;
    QImage m_image;
    std::shared_ptr<ScreenShotWriteState> m_state;
    QPointer<ScreenShotSinkPipe2> m_sink;
};

ScreenShotSinkPipe2::ScreenShotSinkPipe2(int fd, QDBusMessage reply,
                                       ScreenShotInvocation &&invocation,
                                       CaptureAuthority *authority, QObject *parent)
    : QObject(parent)
    , m_reply(std::move(reply))
    , m_pipe(fd)
    , m_invocation(std::move(invocation))
    , m_authority(authority)
    , m_write(std::make_shared<ScreenShotWriteState>(m_invocation.protectedJob.has_value()))
{
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
    if (m_invocation.protectedJob && s_testReceiver && s_testObserver) {
        m_write->observe = [receiver = s_testReceiver, observer = s_testObserver, job = *m_invocation.protectedJob](ScreenShotTestEvent event) {
            QMetaObject::invokeMethod(receiver, [observer, job, event] { observer(job, event); }, Qt::QueuedConnection);
        };
    }
#endif
    connect(waylandServer(), &WaylandServer::screenAboutToLock, this, &ScreenShotSinkPipe2::revoke,
            Qt::DirectConnection);
    connect(waylandServer(), &WaylandServer::lockStateChanged, this, [this] {
        if (!live()) revoke();
    });
    if (m_invocation.protectedJob) {
        connect(authority, &CaptureAuthority::jobRevoked, this, [this](quint64 job) {
            if (job == *m_invocation.protectedJob) revoke();
        }, Qt::DirectConnection);
    }
    connect(authority, &QObject::destroyed, this, &ScreenShotSinkPipe2::revoke,
            Qt::DirectConnection);
    auto *watcher = new QDBusServiceWatcher(m_invocation.sender, QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, &ScreenShotSinkPipe2::revoke);
    if (m_invocation.pidfd) {
        auto *pidfd = new QSocketNotifier(m_invocation.pidfd.get(), QSocketNotifier::Read, this);
        connect(pidfd, &QSocketNotifier::activated, this, &ScreenShotSinkPipe2::revoke);
    }
}

ScreenShotSinkPipe2::~ScreenShotSinkPipe2()
{
    m_write->revoke();
}

bool ScreenShotSinkPipe2::live() const
{
    if (!m_authority || !m_authority->privacyAllows() || !m_invocation.pidfd) {
        return false;
    }
    {
        std::lock_guard guard(m_write->barrier);
        if (!m_write->active || std::chrono::steady_clock::now() >= m_write->deadline) {
            return false;
        }
    }
    pollfd event{m_invocation.pidfd.get(), POLLIN, 0};
    if (poll(&event, 1, 0) != 0) {
        return false;
    }
    auto *daemon = QDBusConnection::sessionBus().interface();
    if (!daemon) return false;
    const auto owner = daemon->serviceOwner(m_invocation.sender);
    const auto uid = daemon->serviceUid(m_invocation.sender);
    const auto pid = daemon->servicePid(m_invocation.sender);
    if (!owner.isValid() || owner.value() != m_invocation.sender
        || !uid.isValid() || uid.value() != m_invocation.uid
        || !pid.isValid() || pid.value() != uint(m_invocation.pid)) {
        return false;
    }
    if (m_invocation.protectedJob) {
        return m_authority->screenshotOperationAllowed(*m_invocation.protectedJob,
                                                       m_invocation.sender, m_invocation.pid);
    }
    return m_invocation.legacyTestPermission
        || fetchRestrictedDBusInterfacesFromPid(m_invocation.pid)
               .contains(QStringLiteral("org.kde.KWin.ScreenShot2"));
}

void ScreenShotSinkPipe2::replyError(const QString &name, const QString &message)
{
    if (!m_replySent) {
        m_replySent = true;
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
        if (m_write->observe) m_write->observe(ScreenShotTestEvent::ErrorReply);
#endif
        QDBusConnection::sessionBus().send(m_reply.createErrorReply(name, message));
    }
}

void ScreenShotSinkPipe2::replySuccess()
{
    if (!m_replySent) {
        m_replySent = true;
#if defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
        if (m_write->observe) m_write->observe(ScreenShotTestEvent::SuccessReply);
#endif
        QDBusConnection::sessionBus().send(m_reply.createReply(m_result));
    }
}

void ScreenShotSinkPipe2::revoke()
{
    m_write->revoke();
    m_pipe.reset();
    replyError(s_errorCancelled, s_errorCancelledMessage);
    deleteLater();
}

void ScreenShotSinkPipe2::cancel()
{
    revoke();
}

void ScreenShotSinkPipe2::flush(const QImage &image, const QVariantMap &attributes)
{
    if (m_writerStarted || !m_pipe || !live()) {
        cancel();
        return;
    }
    m_result = attributes;
    m_result.insert(QStringLiteral("type"), QStringLiteral("raw"));
    m_result.insert(QStringLiteral("format"), quint32(image.format()));
    m_result.insert(QStringLiteral("width"), quint32(image.width()));
    m_result.insert(QStringLiteral("height"), quint32(image.height()));
    m_result.insert(QStringLiteral("stride"), quint32(image.bytesPerLine()));
    m_result.insert(QStringLiteral("scale"), double(image.devicePixelRatio()));
    m_writerStarted = true;
    if (!m_invocation.protectedJob) {
        std::lock_guard guard(m_write->barrier);
        m_write->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    }
    if (!m_invocation.protectedJob) {
        // Existing desktop-authorized clients retain reply-before-bytes.
        replySuccess();
    }
    auto *writer = new ScreenShotWriter2(std::move(m_pipe), image, m_write, this);
    writer->setAutoDelete(true);
    QThreadPool::globalInstance()->start(writer);
}

void ScreenShotSinkPipe2::writerFinished(bool success)
{
    if (success && live()) {
        if (m_invocation.protectedJob) replySuccess();
    } else {
        replyError(s_errorCancelled, s_errorCancelledMessage);
    }
    deleteLater();
}
}
