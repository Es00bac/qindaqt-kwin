#include "native_session_lock_test.h"
#include "native_lock_fixture.h"
namespace KWin
{
void NativeSessionLockTest::initTestCase()
{
    QVERIFY(waylandServer()->init(QStringLiteral("native-lock-private-0")));
    static_cast<WaylandTestApplication *>(kwinApp())->setInputMethodServerToStart(QStringLiteral("internal"));
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    kwinApp()->start();
    Test::setOutputConfig({Rect(0, 0, 1280, 1024), Rect(1280, 0, 1280, 1024)});
}
void NativeSessionLockTest::cleanup()
{
    if (waylandServer()->isScreenLocked()) {
        auto replacement = NativeClient::create(true);
        QVERIFY(replacement && replacement->manager);
        NativeLock lock(replacement->manager->lock());
        QTRY_VERIFY(lock.locked);
        lock.unlock();
        QTRY_VERIFY(!waylandServer()->isScreenLocked());
    }
}
void NativeSessionLockTest::authorization()
{
    qputenv("KWIN_WAYLAND_NO_PERMISSION_CHECKS", "1");
    auto ordinary = NativeClient::create(false);
    QVERIFY(ordinary);
    QVERIFY(!ordinary->manager);
    qunsetenv("KWIN_WAYLAND_NO_PERMISSION_CHECKS");
    auto trusted = NativeClient::create(true);
    QVERIFY(trusted && trusted->manager);
    QVERIFY(!waylandServer()->isScreenLocked());
}
void NativeSessionLockTest::presentationFenceAndCrashRecovery()
{
    auto client = NativeClient::create(true);
    QVERIFY(client && client->manager);
    const auto outputs = kwinApp()->outputBackend()->outputs();
    QCOMPARE(outputs.size(), 2);
    auto loop = outputs[1]->renderLoop();
    loop->inhibit();
    auto lock = std::make_unique<NativeLock>(client->manager->lock());
    QTRY_VERIFY(waylandServer()->isScreenLocked());
    // Real virtual-backend presentation is held, not a mocked acknowledgement.
    // The old timeout must not manufacture the locked event after one second.
    auto surface = Test::createSurface(client->connection->compositor);
    NativeSurface role(lock->get_lock_surface(*surface, *client->connection->outputs[0]));
    QTRY_VERIFY(role.serial);
    role.ack_configure(role.serial);
    auto window = Test::renderAndWaitForShown(client->connection->shm, surface.get(), role.size, Qt::green);
    QVERIFY(window);
    QVERIFY(!window->windowItem()->isVisible());
    QVERIFY(!waylandServer()->isLockScreenInputSurface(window->surface()));
    QTest::qWait(1200);
    const bool premature = lock->locked;
    loop->uninhibit();
    QVERIFY(!premature);
    QTRY_VERIFY(lock->locked);
    auto contender = NativeClient::create(true);
    QVERIFY(contender && contender->manager);
    NativeLock rejected(contender->manager->lock());
    QTRY_VERIFY(rejected.finished);
    QVERIFY(!rejected.locked);
    role.destroy();
    surface.reset();
    lock.reset();
    client.reset();
    QTRY_VERIFY(!waylandServer()->sessionLock()->client());
    QVERIFY(waylandServer()->isScreenLocked());
    auto replacement = NativeClient::create(true);
    QVERIFY(replacement && replacement->manager);
    NativeLock recovered(replacement->manager->lock());
    QTRY_VERIFY(recovered.locked);
    recovered.unlock();
    QTRY_VERIFY(!waylandServer()->isScreenLocked());
}
void NativeSessionLockTest::explicitSurfaceRole()
{
    auto client = NativeClient::create(true);
    QVERIFY(client && client->manager);
    Test::XdgToplevelWindow ordinary(client->connection.get());
    QVERIFY(ordinary.show());
    QVERIFY(!ordinary.m_window->isLockScreen());
    NativeLock lock(client->manager->lock());
    QTRY_VERIFY(lock.locked);
    QVERIFY(!ordinary.m_window->windowItem()->isVisible());
    auto surface = Test::createSurface(client->connection->compositor);
    QVERIFY(surface);
    NativeSurface role(lock.get_lock_surface(*surface, *client->connection->outputs[0]));
    QTRY_VERIFY(role.serial);
    QCOMPARE(role.size, QSize(1280, 1024));
    role.ack_configure(role.serial);
    auto window = Test::renderAndWaitForShown(client->connection->shm, surface.get(), role.size, Qt::black);
    QVERIFY(window);
    QVERIFY(window->isLockScreen());
    QVERIFY(waylandServer()->isLockScreenInputSurface(window->surface()));
    QVERIFY(!waylandServer()->isLockScreenInputSurface(ordinary.m_window->surface()));
    lock.unlock();
    QTRY_VERIFY(!waylandServer()->isScreenLocked());
    QTRY_VERIFY(ordinary.m_window->windowItem()->isVisible());
}
}
WAYLANDTEST_MAIN(KWin::NativeSessionLockTest)
