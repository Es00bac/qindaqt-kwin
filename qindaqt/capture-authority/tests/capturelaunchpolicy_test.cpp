// SPDX-License-Identifier: GPL-2.0-or-later
#include "capturelaunchpolicy.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

using namespace KWin;

class CaptureLaunchPolicyTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void rejectsUserManagedImagesAndAliases();
    void stripsHostControlledLaunchOverrides();
};

void CaptureLaunchPolicyTest::rejectsUserManagedImagesAndAliases()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString image = dir.filePath(QStringLiteral("capture-helper"));
    QFile file(image);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("fixture", 7) == 7);
    file.close();
    QVERIFY(!CaptureLaunchPolicy::isRootManagedRegularFile(image));
    QVERIFY(!CaptureLaunchPolicy::isRootManagedRegularFile(QStringLiteral("relative-helper")));

    const QString alias = dir.filePath(QStringLiteral("alias"));
    QVERIFY(QFile::link(QStringLiteral("/usr/bin/true"), alias));
    QVERIFY(!CaptureLaunchPolicy::isRootManagedRegularFile(alias));
    QVERIFY(!CaptureLaunchPolicy::isInstalledCaptureEntry(image, QStringLiteral("/usr/bin/true"), true));
}

void CaptureLaunchPolicyTest::stripsHostControlledLaunchOverrides()
{
    QProcessEnvironment host;
    host.insert(QStringLiteral("HOME"), QStringLiteral("/fixture-home"));
    host.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), QStringLiteral("unix:path=/fixture-bus"));
    host.insert(QStringLiteral("LANG"), QStringLiteral("en_US.UTF-8"));
    for (const char *name : {"LD_PRELOAD", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QML_IMPORT_PATH",
                             "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "KWIN_SCREENSHOT_NO_PERMISSION_CHECKS"}) {
        host.insert(QLatin1String(name), QStringLiteral("hostile"));
    }

    const auto broker = CaptureLaunchPolicy::environment(host, false);
    const auto helper = CaptureLaunchPolicy::environment(host, true);
    QCOMPARE(broker.value(QStringLiteral("HOME")), QStringLiteral("/fixture-home"));
    QCOMPARE(broker.value(QStringLiteral("LANG")), QStringLiteral("en_US.UTF-8"));
    QCOMPARE(broker.value(QStringLiteral("DBUS_SESSION_BUS_ADDRESS")), QStringLiteral("unix:path=/fixture-bus"));
    QCOMPARE(broker.value(QStringLiteral("PATH")), QStringLiteral("/usr/bin:/bin"));
    QVERIFY(!broker.contains(QStringLiteral("WAYLAND_SOCKET")));
    QCOMPARE(helper.value(QStringLiteral("WAYLAND_SOCKET")), QStringLiteral("4"));
    QCOMPARE(helper.value(QStringLiteral("QT_QPA_PLATFORM")), QStringLiteral("wayland"));
    for (const char *name : {"LD_PRELOAD", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QML_IMPORT_PATH",
                             "WAYLAND_DISPLAY", "KWIN_SCREENSHOT_NO_PERMISSION_CHECKS"}) {
        QVERIFY(!broker.contains(QLatin1String(name)));
        QVERIFY(!helper.contains(QLatin1String(name)));
    }

    host.insert(QStringLiteral("LANG"), QStringLiteral("/untrusted/path"));
    QCOMPARE(CaptureLaunchPolicy::environment(host, true).value(QStringLiteral("LANG")), QStringLiteral("C.UTF-8"));
}

QTEST_GUILESS_MAIN(CaptureLaunchPolicyTest)
#include "capturelaunchpolicy_test.moc"
