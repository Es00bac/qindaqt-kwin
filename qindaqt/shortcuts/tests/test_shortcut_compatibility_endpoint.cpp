/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "qindaqt/shortcuts/compatibility_endpoint.h"

#include <KGlobalAccel>
#include <QAction>
#include <QDBusArgument>

#include <algorithm>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QtTest>

using namespace KWin::Shortcuts;

class ShortcutCompatibilityEndpointTest final : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void publicClientRegistrationAndInvocation();
};

void ShortcutCompatibilityEndpointTest::publicClientRegistrationAndInvocation()
{
    Registry registry;
    CompatibilityEndpoint endpoint(QDBusConnection::sessionBus(), &registry);
    QVERIFY(endpoint.start(QStringLiteral("org.kde.kglobalaccel")));

    QAction action;
    action.setProperty("componentName", QStringLiteral("qindaqt-private-contract"));
    action.setObjectName(QStringLiteral("show-launcher"));
    action.setText(QStringLiteral("Show launcher"));
    const QKeySequence sequence(Qt::META | Qt::Key_Space);
    QSignalSpy triggered(&action, &QAction::triggered);

    QVERIFY(KGlobalAccel::self()->setDefaultShortcut(&action, {sequence}, KGlobalAccel::NoAutoloading));
    QVERIFY(KGlobalAccel::self()->setShortcut(&action, {sequence}, KGlobalAccel::NoAutoloading));
    const auto stored = registry.binding(QStringLiteral("qindaqt-private-contract")
                                         + QChar(0x1f) + QStringLiteral("show-launcher"));
    QVERIFY(stored);
    QCOMPARE(stored->shortcuts, QList<QKeySequence>{sequence});
    QCOMPARE(KGlobalAccel::self()->shortcut(&action), QList<QKeySequence>{sequence});
    QCOMPARE(KGlobalAccel::self()->defaultShortcut(&action), QList<QKeySequence>{sequence});
    const auto matches = KGlobalAccel::globalShortcutsByKey(sequence);
    QVERIFY(std::any_of(matches.cbegin(), matches.cend(), [](const auto &info) {
        return info.uniqueName() == QStringLiteral("show-launcher");
    }));
    QVERIFY(KGlobalAccel::isComponentActive(QStringLiteral("qindaqt-private-contract")));
    QDBusInterface root(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"),
                        QStringLiteral("org.kde.KGlobalAccel"), QDBusConnection::sessionBus());
    const auto available = root.call(QStringLiteral("globalShortcutAvailable"), sequence,
                                     QStringLiteral("other-component"));
    QCOMPARE(available.type(), QDBusMessage::ReplyMessage);
    QVERIFY(!available.arguments().first().toBool());
    QDBusInterface component(QStringLiteral("org.kde.kglobalaccel"),
                             QStringLiteral("/component/qindaqt_private_contract"),
                             QStringLiteral("org.kde.kglobalaccel.Component"),
                             QDBusConnection::sessionBus());
    QVERIFY(component.isValid());
    const auto activeReply = component.call(QStringLiteral("isActive"));
    QVERIFY(activeReply.type() == QDBusMessage::ReplyMessage);
    QVERIFY(activeReply.arguments().first().toBool());
    const auto infos = component.call(QStringLiteral("allShortcutInfos"));
    QCOMPARE(infos.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(infos.arguments().first().value<QDBusArgument>().currentSignature(),
             QStringLiteral("a(ssssssaiai)"));
    QCOMPARE(component.call(QStringLiteral("invokeShortcut"), QStringLiteral("show-launcher")).type(),
             QDBusMessage::ReplyMessage);
    QTRY_COMPARE(triggered.count(), 1);

    QAction other;
    other.setProperty("componentName", QStringLiteral("qindaqt-other-client"));
    other.setObjectName(QStringLiteral("other-action"));
    other.setText(QStringLiteral("Other action"));
    const QKeySequence otherSequence(Qt::CTRL | Qt::Key_F12);
    QVERIFY(KGlobalAccel::self()->setDefaultShortcut(&other, {otherSequence}, KGlobalAccel::NoAutoloading));
    QVERIFY(KGlobalAccel::self()->setShortcut(&other, {otherSequence}, KGlobalAccel::NoAutoloading));
    const QStringList actionId{QStringLiteral("qindaqt-private-contract"), QStringLiteral("show-launcher"),
                               QStringLiteral("Private contract"), QStringLiteral("Show launcher")};
    const auto reassigned = root.call(QStringLiteral("setForeignShortcutKeys"), actionId,
                                      QVariant::fromValue(QList<QKeySequence>{otherSequence}));
    QCOMPARE(reassigned.type(), QDBusMessage::ReplyMessage);
    QTRY_COMPARE(KGlobalAccel::self()->shortcut(&action), QList<QKeySequence>{otherSequence});
    QTRY_VERIFY(KGlobalAccel::self()->shortcut(&other).isEmpty());
    const auto updated = registry.binding(QStringLiteral("qindaqt-private-contract")
                                          + QChar(0x1f) + QStringLiteral("show-launcher"));
    const auto displaced = registry.binding(QStringLiteral("qindaqt-other-client")
                                             + QChar(0x1f) + QStringLiteral("other-action"));
    QVERIFY(updated);
    QVERIFY(displaced);
    QCOMPARE(updated->shortcuts, QList<QKeySequence>{otherSequence});
    QVERIFY(displaced->shortcuts.isEmpty());

    QCOMPARE(root.call(QStringLiteral("blockGlobalShortcuts"), true).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(root.call(QStringLiteral("blockGlobalShortcuts"), false).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(root.call(QStringLiteral("activateGlobalShortcutContext"), actionId.at(0), QStringLiteral("default")).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(root.call(QStringLiteral("actionList"), QVariant::fromValue(otherSequence)).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(root.call(QStringLiteral("action"), otherSequence[0].toCombined()).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(root.call(QStringLiteral("shortcut"), actionId).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(root.call(QStringLiteral("defaultShortcut"), actionId).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(root.call(QStringLiteral("setShortcut"), actionId,
                       QVariant::fromValue(QList<int>{otherSequence[0].toCombined()}),
                       uint(KGlobalAccel::NoAutoloading)).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(root.call(QStringLiteral("setForeignShortcut"), actionId,
                       QVariant::fromValue(QList<int>{otherSequence[0].toCombined()})).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(root.call(QStringLiteral("getGlobalShortcutsByKey"), otherSequence[0].toCombined()).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(root.call(QStringLiteral("isGlobalShortcutAvailable"), otherSequence[0].toCombined(), actionId.at(0)).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(component.call(QStringLiteral("shortcutNames"), QStringLiteral("default")).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(component.call(QStringLiteral("allShortcutInfos"), QStringLiteral("default")).type(),
             QDBusMessage::ReplyMessage);
    QDBusInterface properties(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/qindaqt_private_contract"),
                              QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::sessionBus());
    const QDBusReply<QDBusVariant> friendly = properties.call(
        QStringLiteral("Get"), QStringLiteral("org.kde.kglobalaccel.Component"), QStringLiteral("friendlyName"));
    QVERIFY(friendly.isValid());
    QCOMPARE(friendly.value().variant().toString(), QCoreApplication::applicationName());
    const QDBusReply<QDBusVariant> unique = properties.call(
        QStringLiteral("Get"), QStringLiteral("org.kde.kglobalaccel.Component"), QStringLiteral("uniqueName"));
    QVERIFY(unique.isValid());
    QCOMPARE(unique.value().variant().toString(), actionId.at(0));
    const auto unsupported = root.call(QStringLiteral("setInverseShortcutActions"),
                                       actionId.at(0), actionId.at(1), QStringLiteral("reverse"), uint(0));
    QCOMPARE(unsupported.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(unsupported.errorName(), QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"));

    const auto binding = registry.binding(QStringLiteral("qindaqt-private-contract")
                                          + QChar(0x1f) + QStringLiteral("show-launcher"));
    QVERIFY(binding);
    QCOMPARE(binding->shortcuts, QList<QKeySequence>{otherSequence});
}
QTEST_MAIN(ShortcutCompatibilityEndpointTest)
#include "test_shortcut_compatibility_endpoint.moc"
