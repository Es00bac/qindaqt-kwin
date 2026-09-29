// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/backendoutput.h"
#include "core/outputbackend.h"
#include "core/renderloop.h"
#include "kwin_wayland_test.h"
#include "main.h"
#include "qwayland-ext-session-lock-v1.h"
#include "scene/windowitem.h"
#include "sessionlockcontroller.h"
#include "wayland/display.h"
#include "wayland_server.h"
#include "workspace.h"
#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/event_queue.h>
#include <KWayland/Client/output.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/surface.h>

namespace KWin
{
class NativeLock : public QObject, public QtWayland::ext_session_lock_v1
{
    Q_OBJECT
public:
    explicit NativeLock(::ext_session_lock_v1 *object)
        : QtWayland::ext_session_lock_v1(object)
    {
    }
    ~NativeLock() override
    {
        if (isInitialized()) {
            wl_proxy_destroy(reinterpret_cast<wl_proxy *>(object()));
        }
    }
    bool locked = false;
    bool finished = false;
    void unlock()
    {
        unlock_and_destroy();
    }

protected:
    void ext_session_lock_v1_locked() override
    {
        locked = true;
    }
    void ext_session_lock_v1_finished() override
    {
        finished = true;
    }
};
class NativeSurface : public QtWayland::ext_session_lock_surface_v1
{
public:
    explicit NativeSurface(::ext_session_lock_surface_v1 *object)
        : QtWayland::ext_session_lock_surface_v1(object)
    {
    }
    ~NativeSurface() override
    {
        if (isInitialized()) {
            destroy();
        }
    }
    uint32_t serial = 0;
    QSize size;

protected:
    void ext_session_lock_surface_v1_configure(uint32_t serial, uint32_t width, uint32_t height) override
    {
        this->serial = serial;
        size = QSize(width, height);
    }
};
struct NativeClient
{
    // AGENT-GUARD: authorization is per private socketpair, compiled only in
    // the non-installable test build; it never grants production clients access.
    std::unique_ptr<Test::Connection> connection;
    std::unique_ptr<KWayland::Client::Registry> registry;
    std::unique_ptr<QtWayland::ext_session_lock_manager_v1> manager;
    ~NativeClient()
    {
        if (manager) {
            manager->destroy();
        }
    }
    static std::unique_ptr<NativeClient> create(bool authorize)
    {
        auto client = std::make_unique<NativeClient>();
        QMetaObject::Connection grant;
        if (authorize) {
            grant = QObject::connect(waylandServer()->display(), &Display::clientConnected,
                                     waylandServer()->sessionLock(), [](ClientConnection *peer) {
                waylandServer()->sessionLock()->authorizeTestClient(peer);
            });
        }
        client->connection = Test::Connection::setup();
        QObject::disconnect(grant);
        if (!client->connection) {
            return nullptr;
        }
        client->registry = std::make_unique<KWayland::Client::Registry>();
        client->registry->setEventQueue(client->connection->queue);
        QObject::connect(client->registry.get(), &KWayland::Client::Registry::interfaceAnnounced, client->registry.get(),
                         [c = client.get()](const QByteArray &interface, quint32 name, quint32 version) {
            if (interface == QByteArrayLiteral("ext_session_lock_manager_v1")) {
                c->manager = std::make_unique<QtWayland::ext_session_lock_manager_v1>();
                c->manager->init(*c->registry, name, version);
                c->connection->queue->addProxy(c->manager->object());
            }
        });
        QSignalSpy ready(client->registry.get(), &KWayland::Client::Registry::interfacesAnnounced);
        client->registry->create(client->connection->connection);
        client->registry->setup();
        if (!ready.wait()) {
            return nullptr;
        }
        return client;
    }
};
class NativeSessionLockTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void authorization();
    void presentationFenceAndCrashRecovery();
    void explicitSurfaceRole();
};
void NativeSessionLockTest::initTestCase()
{
    QVERIFY(waylandServer()->init(QStringLiteral("native-lock-private-0")));
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
#include "native_session_lock_test.moc"
