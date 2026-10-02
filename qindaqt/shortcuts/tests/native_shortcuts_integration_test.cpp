/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// Adapted from upstream GlobalShortcutsTest and the owned native lock fixture.
// Native wire drives the actual compositor authority; no registry/input fake.
#include "input.h"
#include "keyboard_input.h"
#include "kwin_wayland_test.h"
#include "main.h"
#include "qindaqt/session-lock/tests/native_lock_fixture.h"
#include "qwayland-keyboard-shortcuts-inhibit-unstable-v1.h"
#include "wayland/keyboard.h"
#include "wayland/seat.h"
#include "wayland_server.h"
#include "workspace.h"
#include "xkb.h"
#include <KWayland/Client/seat.h>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusReply>
#include <linux/input.h>

namespace KWin
{
namespace
{
constexpr auto component = "qindaqt.native-shortcut-fixture";
QDBusConnection nativeClient()
{
    // AGENT-GUARD: the caller needs a real unique sender distinct from the
    // compositor; a same-connection local call cannot qualify admission.
    return QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"),
                                        QStringLiteral("native-shortcuts-runtime-client"));
}
QDBusMessage nativeCall(const QString &method, const QVariantList &args = {})
{
    auto call = QDBusMessage::createMethodCall("org.qindaqt.Shortcuts1", "/org/qindaqt/Shortcuts1", "org.qindaqt.Shortcuts1", method);
    call.setArguments(args);
    auto pending = nativeClient().asyncCall(call, 2000);
    if (!QTest::qWaitFor([&] { return pending.isFinished(); }, 2500)) {
        return QDBusMessage::createError(QStringLiteral("org.freedesktop.DBus.Error.Timeout"), QStringLiteral("Native shortcut reply timed out"));
    }
    return pending.reply();
}
void chord(uint &time, uint key, bool control = true, bool alt = true)
{
    if (control)
        Test::keyboardKeyPressed(KEY_LEFTCTRL, time++);
    if (alt)
        Test::keyboardKeyPressed(KEY_LEFTALT, time++);
    Test::keyboardKeyPressed(key, time++);
    Test::keyboardKeyReleased(key, time++);
    if (alt)
        Test::keyboardKeyReleased(KEY_LEFTALT, time++);
    if (control)
        Test::keyboardKeyReleased(KEY_LEFTCTRL, time++);
}
class Inhibitor final : public QtWayland::zwp_keyboard_shortcuts_inhibitor_v1
{
public:
    explicit Inhibitor(::zwp_keyboard_shortcuts_inhibitor_v1 *object)
        : QtWayland::zwp_keyboard_shortcuts_inhibitor_v1(object)
    {
    }
    bool active = false;

protected:
    void zwp_keyboard_shortcuts_inhibitor_v1_active() override
    {
        active = true;
    }
    void zwp_keyboard_shortcuts_inhibitor_v1_inactive() override
    {
        active = false;
    }
};
}
class NativeShortcutsIntegrationTest final : public QObject
{
    Q_OBJECT
private:
    QStringList m_actions;
    bool bind(const QString &action, const QString &key, bool repeat = false)
    {
        const auto reply = nativeCall("Register", {QLatin1String(component), action, action, QStringList{key}, repeat, true});
        if (reply.type() != QDBusMessage::ReplyMessage || reply.signature() != QLatin1String("bs") || !reply.arguments().first().toBool())
            return false;
        m_actions.append(action);
        return true;
    }
Q_SIGNALS:
    void nativeActivated(const QString &, const QString &, quint64);
    void nativeDeactivated(const QString &, const QString &, quint64);
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(waylandServer()->init(QStringLiteral("qindaqt-native-shortcuts-test")));
        kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));
        qputenv("KWIN_XKB_DEFAULT_KEYMAP", QByteArrayLiteral("1"));
        qputenv("XKB_DEFAULT_RULES", QByteArrayLiteral("evdev"));
        qputenv("XKB_DEFAULT_LAYOUT", QByteArrayLiteral("us,ru"));
        kwinApp()->start();
        Test::setOutputConfig({Rect(0, 0, 1100, 820)});
        const auto bus = QDBusConnection::sessionBus();
        const QDBusReply<QString> owner = bus.interface()->serviceOwner("org.qindaqt.Shortcuts1");
        QVERIFY2(owner.isValid(), "This actual-compositor gate requires KWIN_NATIVE_SHORTCUTS=ON");
        QCOMPARE(owner.value(), bus.baseService());
        QVERIFY(nativeClient().connect(owner.value(), "/org/qindaqt/Shortcuts1", "org.qindaqt.Shortcuts1", "Activated", this, SIGNAL(nativeActivated(QString, QString, quint64))));
        QVERIFY(nativeClient().connect(owner.value(), "/org/qindaqt/Shortcuts1", "org.qindaqt.Shortcuts1", "Deactivated", this, SIGNAL(nativeDeactivated(QString, QString, quint64))));
    }
    void cleanupTestCase()
    {
        QDBusConnection::disconnectFromBus(QStringLiteral("native-shortcuts-runtime-client"));
    }
    void init()
    {
        input()->keyboard()->xkb()->switchToLayout(0);
    }
    void cleanup()
    {
        for (const auto &action : std::as_const(m_actions)) {
            const auto reply = nativeCall("Unregister", {QLatin1String(component), action});
            QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
            QVERIFY(reply.arguments().first().toBool());
        }
        m_actions.clear();
        QVERIFY(!waylandServer()->isScreenLocked());
        QVERIFY(!waylandServer()->isKeyboardShortcutsInhibited());
    }
    void nonLatinLayoutPressAndRelease_data()
    {
        QTest::addColumn<uint>("physicalKey");
        QTest::addColumn<QString>("portable");
        QTest::addColumn<bool>("alt");
        QTest::newRow("physical-w") << uint(KEY_W) << QStringLiteral("Ctrl+Alt+W") << true;
        // AGENT-GUARD: Ctrl+Alt+F12 is reserved for console switching before
        // shortcut filtering. Use an unreserved function-key chord here.
        QTest::newRow("layout-independent-f12") << uint(KEY_F12) << QStringLiteral("Ctrl+F12") << false;
    }
    void nonLatinLayoutPressAndRelease()
    {
        QFETCH(uint, physicalKey);
        QFETCH(QString, portable);
        QFETCH(bool, alt);
        QVERIFY(bind("layout", portable));
        input()->keyboard()->xkb()->switchToLayout(1);
        QCOMPARE(input()->keyboard()->xkb()->layoutName(), QStringLiteral("Russian"));
        QSignalSpy press(this, &NativeShortcutsIntegrationTest::nativeActivated), release(this, &NativeShortcutsIntegrationTest::nativeDeactivated);
        uint time = 1;
        chord(time, physicalKey, true, alt);
        QTRY_COMPARE(press.count(), 1);
        QTRY_COMPARE(release.count(), 1);
        QCOMPARE(press.first().at(1).toString(), QStringLiteral("layout"));
        QVERIFY(press.first().at(2).toULongLong() > 0);
    }
    void rollingSequenceAndModifierOnly()
    {
        QVERIFY(bind("sequence", "Ctrl+K, Ctrl+C"));
        QVERIFY(bind("modifier", QKeySequence(Qt::META).toString(QKeySequence::PortableText)));
        QSignalSpy press(this, &NativeShortcutsIntegrationTest::nativeActivated), release(this, &NativeShortcutsIntegrationTest::nativeDeactivated);
        uint time = 1;
        chord(time, KEY_K, true, false);
        QCOMPARE(press.count(), 0);
        chord(time, KEY_C, true, false);
        QTRY_COMPARE(press.count(), 1);
        QTRY_COMPARE(release.count(), 1);
        QCOMPARE(press.first().at(1).toString(), QStringLiteral("sequence"));
        Test::keyboardKeyPressed(KEY_LEFTMETA, time++);
        Test::keyboardKeyReleased(KEY_LEFTMETA, time++);
        QTRY_COMPARE(press.count(), 2);
        QTRY_COMPARE(release.count(), 2);
        QCOMPARE(press.last().at(1).toString(), QStringLiteral("modifier"));
    }
    void repeatedNativeAction()
    {
        QVERIFY(bind("repeat", "Ctrl+F12", true));
        waylandServer()->seat()->keyboard()->setRepeatInfo(25, 300);
        QSignalSpy press(this, &NativeShortcutsIntegrationTest::nativeActivated), release(this, &NativeShortcutsIntegrationTest::nativeDeactivated);
        uint time = 1;
        Test::keyboardKeyPressed(KEY_WAKEUP, time++);
        Test::keyboardKeyReleased(KEY_WAKEUP, time++);
        Test::keyboardKeyPressed(KEY_LEFTCTRL, time++);
        Test::keyboardKeyPressed(KEY_F12, time++);
        QTRY_VERIFY_WITH_TIMEOUT(press.count() >= 3, 3000);
        Test::keyboardKeyReleased(KEY_F12, time++);
        Test::keyboardKeyReleased(KEY_LEFTCTRL, time++);
        QTRY_COMPARE(release.count(), 1);
    }
    void actualNativeLockSuppressesThenRestores()
    {
        QVERIFY(bind("lock", "Ctrl+F12"));
        QSignalSpy press(this, &NativeShortcutsIntegrationTest::nativeActivated);
        uint time = 1;
        chord(time, KEY_F12, true, false);
        QTRY_COMPARE(press.count(), 1);
        auto locker = NativeClient::create(true);
        QVERIFY(locker && locker->manager);
        NativeLock lock(locker->manager->lock());
        QTRY_VERIFY(lock.locked);
        QVERIFY(waylandServer()->isScreenLocked());
        chord(time, KEY_F12, true, false);
        QTest::qWait(100);
        QCOMPARE(press.count(), 1);
        lock.unlock();
        QTRY_VERIFY(!waylandServer()->isScreenLocked());
        chord(time, KEY_F12, true, false);
        QTRY_COMPARE(press.count(), 2);
    }
    void actualFocusedSurfaceInhibitorSuppressesThenRestores()
    {
        QVERIFY(bind("inhibit", "Ctrl+F12"));
        auto connection = Test::Connection::setup(Test::AdditionalWaylandInterface::Seat);
        QVERIFY(connection && connection->seat);
        KWayland::Client::Registry registry;
        registry.setEventQueue(connection->queue);
        QtWayland::zwp_keyboard_shortcuts_inhibit_manager_v1 manager;
        connect(&registry, &KWayland::Client::Registry::interfaceAnnounced, this, [&](const QByteArray &name, quint32 id, quint32 version) {
            if (name == "zwp_keyboard_shortcuts_inhibit_manager_v1") {
                manager.init(registry, id, version);
                connection->queue->addProxy(manager.object());
            }
        });
        QSignalSpy announced(&registry, &KWayland::Client::Registry::interfacesAnnounced);
        registry.create(connection->connection);
        registry.setup();
        QVERIFY(announced.wait());
        QVERIFY(manager.isInitialized());
        Test::XdgToplevelWindow window(connection.get());
        QVERIFY(window.show());
        workspace()->activateWindow(window.m_window, true);
        Inhibitor inhibitor(manager.inhibit_shortcuts(*window.m_surface, *connection->seat));
        QVERIFY(connection->sync());
        QTRY_VERIFY(inhibitor.active);
        QTRY_VERIFY(waylandServer()->isKeyboardShortcutsInhibited());
        QSignalSpy press(this, &NativeShortcutsIntegrationTest::nativeActivated);
        uint time = 1;
        chord(time, KEY_F12, true, false);
        QTest::qWait(100);
        QCOMPARE(press.count(), 0);
        inhibitor.destroy();
        QVERIFY(connection->sync());
        QTRY_VERIFY(!waylandServer()->isKeyboardShortcutsInhibited());
        chord(time, KEY_F12, true, false);
        QTRY_COMPARE(press.count(), 1);
        manager.destroy();
    }
};
}
WAYLANDTEST_MAIN(KWin::NativeShortcutsIntegrationTest)
#include "native_shortcuts_integration_test.moc"
