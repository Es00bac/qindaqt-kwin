// SPDX-License-Identifier: GPL-2.0-or-later
#include "utils/serviceutils.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace KWin;

class PermissionListTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void desktopLists_data();
    void desktopLists();
    void escapedValuesRemainLiteral();
    void packagedLockMetadata();
    void packagedCaptureMetadata();
    void stockKeysGrantNothing();
    void absentSourceGrantsNothing();
};

void PermissionListTest::desktopLists_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QByteArray>("encoded");
    QTest::addColumn<QStringList>("expected");
    for (const auto &field : {s_waylandInterfaceName, s_dbusRestrictedInterfaceName}) {
        const auto row = [&](const char *name, const QByteArray &encoded, const QStringList &expected) {
            const QByteArray tag = field.toUtf8() + '-' + name;
            QTest::newRow(tag.constData()) << field << encoded << expected;
        };
        row("single-trailing", "first;", {QStringLiteral("first")});
        row("single-unterminated", "first", {QStringLiteral("first")});
        row("multiple-trailing", "first;second;", {QStringLiteral("first"), QStringLiteral("second")});
        row("multiple-unterminated", "first;second", {QStringLiteral("first"), QStringLiteral("second")});
        row("comma-is-literal", "first,second;", {QStringLiteral("first,second")});
        row("empty", QByteArray(""), {});
        row("absent", {}, {});
    }
}

void PermissionListTest::desktopLists()
{
    QFETCH(QString, field);
    QFETCH(QByteArray, encoded);
    QFETCH(QStringList, expected);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("native.desktop")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray contents = "[Desktop Entry]\nType=Application\nName=Native fixture\nExec=/usr/bin/true\n"
        + (encoded.isNull() ? QByteArray() : field.toUtf8() + '=' + encoded + '\n');
    QCOMPARE(file.write(contents), contents.size());
    file.close();
    const KService service(file.fileName());
    QVERIFY(service.isValid());
    QCOMPARE(readServicePermissionList(service, field), expected);
}

void PermissionListTest::escapedValuesRemainLiteral()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("escaped.desktop"));
    const QStringList values{QStringLiteral("ext_session_lock_manager_v1;literal"), QStringLiteral("literal\\value")};
    {
        KDesktopFile file(path);
        auto entry = file.desktopGroup();
        entry.writeEntry("Type", "Application");
        entry.writeEntry("Name", "Escaped fixture");
        entry.writeEntry("Exec", "/usr/bin/true");
        // Use the same KDE desktop-entry codec for both escape layers.
        entry.writeXdgListEntry(s_waylandInterfaceName, values);
        entry.writeXdgListEntry(s_dbusRestrictedInterfaceName, values);
        QVERIFY(file.sync());
    }
    const KService service(path);
    QVERIFY(service.isValid());
    for (const auto &field : {s_waylandInterfaceName, s_dbusRestrictedInterfaceName}) {
        const auto decoded = readServicePermissionList(service, field);
        QCOMPARE(decoded, values);
        QVERIFY(!decoded.contains(QStringLiteral("ext_session_lock_manager_v1")));
    }
}

void PermissionListTest::packagedLockMetadata()
{
    const KService entry(QStringLiteral(QINDAQT_PERMISSION_TEST_DATA "/org.qindaqt.Lock.desktop"));
    QVERIFY(entry.isValid());
    QCOMPARE(QProcess::splitCommand(entry.exec()), QStringList{QStringLiteral("/usr/bin/qindaqt-lock")});
    const auto permissions = readServicePermissionList(entry, s_waylandInterfaceName);
    QCOMPARE(permissions, QStringList{QStringLiteral("ext_session_lock_manager_v1")});
    QVERIFY(permissions.contains(QStringLiteral("ext_session_lock_manager_v1")));
    QVERIFY(readServicePermissionList(entry, s_dbusRestrictedInterfaceName).isEmpty());
}

void PermissionListTest::stockKeysGrantNothing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("stock.desktop")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray contents =
        "[Desktop Entry]\nType=Application\nName=Legacy fixture\nExec=/usr/bin/true\n"
        "X-KDE-Wayland-Interfaces=ext_session_lock_manager_v1;\n"
        "X-KDE-DBUS-Restricted-Interfaces=org.qindaqt.KWin.ScreenShot2;\n";
    QCOMPARE(file.write(contents), contents.size());
    file.close();
    const KService entry(file.fileName());
    QVERIFY(entry.isValid());
    // These are the exact native fields used by private locker/capture launch.
    QVERIFY(readServicePermissionList(entry, s_waylandInterfaceName).isEmpty());
    QVERIFY(readServicePermissionList(entry, s_dbusRestrictedInterfaceName).isEmpty());
}

void PermissionListTest::packagedCaptureMetadata()
{
    const KService helper(QStringLiteral(QINDAQT_PERMISSION_TEST_DATA "/org.qindaqt.PortalCapture.desktop"));
    QVERIFY(helper.isValid());
    // Capture launch requires this exact singleton and an empty Wayland list.
    QCOMPARE(readServicePermissionList(helper, s_dbusRestrictedInterfaceName), QStringList{QStringLiteral("org.qindaqt.KWin.ScreenShot2")});
    QVERIFY(readServicePermissionList(helper, s_waylandInterfaceName).isEmpty());

    const KService broker(QStringLiteral(QINDAQT_PERMISSION_TEST_DATA "/org.qindaqt.PortalCaptureBackend.desktop"));
    QVERIFY(broker.isValid());
    QVERIFY(readServicePermissionList(broker, s_dbusRestrictedInterfaceName).isEmpty());
    QVERIFY(readServicePermissionList(broker, s_waylandInterfaceName).isEmpty());
}

void PermissionListTest::absentSourceGrantsNothing()
{
    const KService synthetic(QStringLiteral("Synthetic"), QStringLiteral("/usr/bin/true"), QString());
    QVERIFY(synthetic.entryPath().isEmpty());
    QVERIFY(readServicePermissionList(synthetic, s_waylandInterfaceName).isEmpty());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("removed.desktop")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray contents =
        "[Desktop Entry]\nType=Application\nName=Removed fixture\nExec=/usr/bin/true\n"
        "X-QindaQt-KWin-DBus-Restricted-Interfaces=org.qindaqt.KWin.ScreenShot2;\n";
    QCOMPARE(file.write(contents), contents.size());
    file.close();
    const KService removed(file.fileName());
    QVERIFY(removed.isValid());
    QVERIFY(QFile::remove(file.fileName()));
    QVERIFY(readServicePermissionList(removed, s_dbusRestrictedInterfaceName).isEmpty());
}

QTEST_GUILESS_MAIN(PermissionListTest)
#include "permission_list_test.moc"
