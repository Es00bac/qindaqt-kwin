/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "qindaqt/shortcuts/compatibility_endpoint.h"
#include "qindaqt/shortcuts/runtime_store.h"
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>
using namespace KWin::Shortcuts;
namespace {
QDBusMessage invoke(const QDBusConnection &bus, const QString &member, const QVariantList &args) {
    auto message = QDBusMessage::createMethodCall("org.qindaqt.Shortcuts1", "/org/qindaqt/Shortcuts1", "org.qindaqt.Shortcuts1", member); message.setArguments(args);
    QDBusPendingCallWatcher pending(bus.asyncCall(message, 2000)); QEventLoop loop;
    QObject::connect(&pending, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
    if (!pending.isFinished()) loop.exec(); return pending.reply();
}
}
class NativeEndpointTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void transientOwnerIsolationAndDisconnect() {
        Registry registry; CompatibilityEndpoint endpoint(QDBusConnection::sessionBus(), &registry); QVERIFY(endpoint.start("org.kde.kglobalaccel"));
        auto first = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "native-first");
        auto foreign = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "native-foreign");
        const auto created = invoke(first, "Register", {"portal_session", "capture", "Capture", QStringList{"Ctrl+F12"}, false, true});
        QCOMPARE(created.type(), QDBusMessage::ReplyMessage); QVERIFY(created.arguments().first().toBool());
        const auto changed = invoke(foreign, "SetShortcuts", {"portal_session", "capture", QStringList{"Alt+F9"}});
        QCOMPARE(changed.type(), QDBusMessage::ReplyMessage); QVERIFY(!changed.arguments().first().toBool());
        const auto removed = invoke(foreign, "Unregister", {"portal_session", "capture"}); QVERIFY(!removed.arguments().first().toBool());
        const QString id = QStringLiteral("portal_session") + QChar(0x1f) + QStringLiteral("capture");
        QCOMPARE(registry.binding(id)->shortcuts, QList<QKeySequence>{QKeySequence("Ctrl+F12")});
        first = QDBusConnection::sessionBus(); QDBusConnection::disconnectFromBus("native-first");
        QTRY_VERIFY(!registry.binding(id));
        QDBusConnection::disconnectFromBus("native-foreign");
    }
    void persistenceFailureRollsBackOwnership() {
        QTemporaryDir dir; Registry registry; RuntimeStore store(registry); QString error;
        const auto path = dir.filePath("native.json"); QVERIFY(store.load(path, dir.filePath("legacy"), &error));
        QVERIFY(QFile::remove(path)); QVERIFY(QDir().mkdir(path)); // Atomic replacement cannot replace a directory.
        CompatibilityEndpoint endpoint(QDBusConnection::sessionBus(), &registry, &store); QVERIFY(endpoint.start("org.kde.kglobalaccel"));
        auto first = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "rollback-first");
        const auto failed = invoke(first, "Register", {"rollback", "action", "Action", QStringList{"Ctrl+F12"}, false, false});
        QCOMPARE(failed.type(), QDBusMessage::ErrorMessage); QCOMPARE(failed.errorName(), QStringLiteral("org.qindaqt.Shortcuts1.Error.Persistence")); QVERIFY(registry.bindings().isEmpty());
        QVERIFY(QDir().rmdir(path));
        auto second = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "rollback-second");
        const auto retried = invoke(second, "Register", {"rollback", "action", "Action", QStringList{"Ctrl+F12"}, false, false});
        QCOMPARE(retried.type(), QDBusMessage::ReplyMessage); QVERIFY(retried.arguments().first().toBool());
        QDBusConnection::disconnectFromBus("rollback-first"); QDBusConnection::disconnectFromBus("rollback-second");
    }
};
QTEST_GUILESS_MAIN(NativeEndpointTest)
#include "test_shortcut_native_endpoint.moc"
