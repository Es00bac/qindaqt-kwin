// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_lock_fixture.h"
#include "native_session_lock_test.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusUnixFileDescriptor>
#include <unistd.h>
namespace KWin
{
class LockedCaptureStream : public QtWayland::zkde_screencast_stream_unstable_v1
{
public:
    explicit LockedCaptureStream(::zkde_screencast_stream_unstable_v1 *object)
        : QtWayland::zkde_screencast_stream_unstable_v1(object)
    {
    }
    ~LockedCaptureStream() override
    {
        close();
    }
    QString failure;
    bool created = false;
    void zkde_screencast_stream_unstable_v1_failed(const QString &error) override
    {
        failure = error;
    }
    void zkde_screencast_stream_unstable_v1_created(uint32_t) override
    {
        created = true;
    }
};
void NativeSessionLockTest::captureRequestsBlocked()
{
    auto capture = NativeClient::create(false, Test::AdditionalWaylandInterface::ScreencastingV1);
    QVERIFY(capture && capture->connection->screencastingV1);
    Test::XdgToplevelWindow ordinary(capture->connection.get());
    QVERIFY(ordinary.show());
    auto locker = NativeClient::create(true);
    QVERIFY(locker && locker->manager);
    NativeLock lock(locker->manager->lock());
    QTRY_VERIFY(lock.locked);
    auto manager = capture->connection->screencastingV1;
    const auto mode = QtWayland::zkde_screencast_unstable_v1::pointer_hidden;
    std::vector<std::unique_ptr<LockedCaptureStream>> streams;
    streams.push_back(std::make_unique<LockedCaptureStream>(manager->stream_window(ordinary.m_window->internalId().toString(), mode)));
    streams.push_back(std::make_unique<LockedCaptureStream>(manager->stream_output(*capture->connection->outputs[0], mode)));
    streams.push_back(std::make_unique<LockedCaptureStream>(manager->stream_region(0, 0, 200, 200, wl_fixed_from_double(1), mode)));
    streams.push_back(std::make_unique<LockedCaptureStream>(manager->stream_virtual_output(QStringLiteral("lock-denied"), 200, 200, wl_fixed_from_double(1), mode)));
    streams.push_back(std::make_unique<LockedCaptureStream>(manager->stream_virtual_output_with_description(QStringLiteral("lock-denied-2"), QStringLiteral("Private fixture"), 200, 200, wl_fixed_from_double(1), mode)));
    QVERIFY(capture->connection->sync());
    for (const auto &stream : streams) {
        QTRY_COMPARE(stream->failure, QStringLiteral("Session is locked"));
        QVERIFY(!stream->created);
    }
    QCOMPARE(kwinApp()->outputBackend()->outputs().size(), 2);

    // Real IPC on the runner's private session bus. The lock guard must win
    // even when the legacy screenshot permission debug bypass is requested.
    qputenv("KWIN_SCREENSHOT_NO_PERMISSION_CHECKS", "1");
    int descriptors[2];
    QVERIFY(pipe(descriptors) == 0);
    struct Close
    {
        int *fds;
        ~Close()
        {
            close(fds[0]);
            close(fds[1]);
            qunsetenv("KWIN_SCREENSHOT_NO_PERMISSION_CHECKS");
        }
    } close{descriptors};
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.qindaqt.KWin.ScreenShot2"), QStringLiteral("/org/qindaqt/KWin/ScreenShot2"), QStringLiteral("org.qindaqt.KWin.ScreenShot2"), QStringLiteral("CaptureWindow"));
    message.setArguments({ordinary.m_window->internalId().toString(), QVariantMap{}, QVariant::fromValue(QDBusUnixFileDescriptor(descriptors[1]))});
    QDBusPendingCallWatcher watcher(QDBusConnection::sessionBus().asyncCall(message));
    QSignalSpy done(&watcher, &QDBusPendingCallWatcher::finished);
    if (!watcher.isFinished()) {
        QVERIFY(done.wait());
    }
    const auto reply = watcher.reply();
    QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(reply.errorMessage(), QStringLiteral("Session is locked"));
    streams.clear();
    // This legacy fixture manager has no protocol destructor. Release its
    // local proxy before its event queue, after all stream resources.
    wl_proxy_destroy(reinterpret_cast<wl_proxy *>(manager->object()));
    lock.unlock();
    QTRY_VERIFY(!waylandServer()->isScreenLocked());
}
}
