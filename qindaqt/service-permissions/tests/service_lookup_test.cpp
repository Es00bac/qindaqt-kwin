// SPDX-License-Identifier: GPL-2.0-or-later
#include "utils/serviceutils.h"

#include <KSycoca>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <cstring>

using namespace KWin;

class ServiceLookupTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void consumerLookups_data();
    void consumerLookups();
    void missingIdentityGrantsNothing();

private:
    QString imagePath(const QString &id) const;
};

QString ServiceLookupTest::imagePath(const QString &id) const
{
    return QDir(QString::fromUtf8(qgetenv("XDG_DATA_HOME"))).filePath(id + QStringLiteral("-peer"));
}

void ServiceLookupTest::initTestCase()
{
    const QString data = QString::fromUtf8(qgetenv("XDG_DATA_HOME"));
    const QString config = QString::fromUtf8(qgetenv("XDG_CONFIG_HOME"));
    QVERIFY(QDir().mkpath(data + QStringLiteral("/applications")));
    QVERIFY(QDir().mkpath(config + QStringLiteral("/menus")));
    QFile menu(config + QStringLiteral("/menus/applications.menu"));
    QVERIFY(menu.open(QIODevice::WriteOnly));
    const QByteArray menuContents =
        "<!DOCTYPE Menu PUBLIC \"-//freedesktop//DTD Menu 1.0//EN\" \"http://www.freedesktop.org/standards/menu-spec/1.0/menu.dtd\">\n"
        "<Menu><Name>Applications</Name><DefaultAppDirs/><Include><All/></Include></Menu>\n";
    QCOMPARE(menu.write(menuContents), menuContents.size());
    menu.close();

    const QList<QPair<QString, QByteArray>> entries{{QStringLiteral("native"),
                                                     "X-QindaQt-KWin-Wayland-Interfaces=ext_session_lock_manager_v1;second;\n"
                                                     "X-QindaQt-KWin-DBus-Restricted-Interfaces=org.qindaqt.KWin.ScreenShot2;second;\n"},
                                                    {QStringLiteral("legacy"),
                                                     "X-KDE-Wayland-Interfaces=ext_session_lock_manager_v1;\n"
                                                     "X-KDE-DBUS-Restricted-Interfaces=org.qindaqt.KWin.ScreenShot2;\n"},
                                                    {QStringLiteral("empty"),
                                                     "X-QindaQt-KWin-Wayland-Interfaces=\nX-QindaQt-KWin-DBus-Restricted-Interfaces=\n"
                                                     "X-KDE-Wayland-Interfaces=ext_session_lock_manager_v1;\n"
                                                     "X-KDE-DBUS-Restricted-Interfaces=org.qindaqt.KWin.ScreenShot2;\n"},
                                                    {QStringLiteral("absent"), {}}};
    for (const auto &[id, fields] : entries) {
        // A distinct real image gives each service an unambiguous executable
        // and PID identity without substituting the production /proc resolver.
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), imagePath(id)));
        QFile entry(data + QStringLiteral("/applications/") + id + QStringLiteral(".desktop"));
        QVERIFY(entry.open(QIODevice::WriteOnly));
        const QByteArray contents =
            "[Desktop Entry]\nType=Application\nName=" + id.toUtf8() + "\nExec=\"" + imagePath(id).toUtf8() + "\"\nNoDisplay=true\n" + fields;
        QCOMPARE(entry.write(contents), contents.size());
    }
    QCOMPARE(QProcess::execute(QStringLiteral("kbuildsycoca6"), {QStringLiteral("--noincremental")}), 0);
    KSycoca::self()->ensureCacheValid();
}

void ServiceLookupTest::consumerLookups_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<QStringList>("wayland");
    QTest::addColumn<QStringList>("dbus");
    QTest::newRow("native") << QStringLiteral("native") << QStringList{QStringLiteral("ext_session_lock_manager_v1"), QStringLiteral("second")}
                            << QStringList{QStringLiteral("org.qindaqt.KWin.ScreenShot2"), QStringLiteral("second")};
    for (const char *id : {"legacy", "empty", "absent"}) {
        QTest::newRow(id) << QString::fromLatin1(id) << QStringList() << QStringList();
    }
}

void ServiceLookupTest::consumerLookups()
{
    QFETCH(QString, id);
    QFETCH(QStringList, wayland);
    QFETCH(QStringList, dbus);
    const QString image = QFileInfo(imagePath(id)).canonicalFilePath();
    QVERIFY(!image.isEmpty());
    const auto service = KService::serviceByDesktopName(id);
    QVERIFY(service);
    QVERIFY(!service->entryPath().isEmpty());
    QCOMPARE(fetchRequestedInterfacesForDesktopId(id), wayland);
    QCOMPARE(fetchRequestedInterfaces(image), wayland);
    QCOMPARE(fetchProcessServiceField(image, s_dbusRestrictedInterfaceName), dbus);

    QProcess peer;
    struct StopPeer {
        QProcess &process;
        ~StopPeer()
        {
            if (process.state() != QProcess::NotRunning) {
                process.kill();
                process.waitForFinished(1000);
            }
        }
    } stop{peer};
    peer.start(image, {QStringLiteral("--permission-peer")});
    QVERIFY(peer.waitForStarted(5000));
    QVERIFY(peer.waitForReadyRead(5000));
    QCOMPARE(peer.readAllStandardOutput(), QByteArray("ready\n"));
    const auto pid = static_cast<uint>(peer.processId());
    QCOMPARE(executablePathFromPid(pid), image);
    QCOMPARE(fetchRestrictedDBusInterfacesFromPid(pid), dbus);
    QCOMPARE(peer.write("x\n"), qint64(2));
    QVERIFY(peer.waitForFinished(5000));
    QCOMPARE(peer.exitCode(), 0);
}

void ServiceLookupTest::missingIdentityGrantsNothing()
{
    QVERIFY(fetchRequestedInterfacesForDesktopId(QStringLiteral("missing-native-permission-entry")).isEmpty());
    QVERIFY(fetchRequestedInterfaces(QStringLiteral("/missing/native-permission-image")).isEmpty());
    QVERIFY(fetchRestrictedDBusInterfacesFromPid(0).isEmpty());
}

int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--permission-peer") == 0) {
        std::fputs("ready\n", stdout);
        std::fflush(stdout);
        return std::getchar() == EOF ? 1 : 0;
    }
    // AGENT-GUARD: KService's menu/cache and the child peers are private to
    // this process. The test never rebuilds the user's service database.
    QTemporaryDir fixture;
    if (!fixture.isValid()) {
        return 1;
    }
    for (const auto &[key, directory] : {std::pair{"XDG_DATA_HOME", "data"},
                                         {"XDG_DATA_DIRS", "system-data"},
                                         {"XDG_CONFIG_HOME", "config"},
                                         {"XDG_CONFIG_DIRS", "system-config"},
                                         {"XDG_CACHE_HOME", "cache"}}) {
        const QString path = fixture.filePath(QLatin1String(directory));
        if (!QDir().mkpath(path)) {
            return 1;
        }
        qputenv(key, path.toUtf8());
    }
    qputenv("KDESYCOCA", fixture.filePath(QStringLiteral("sycoca")).toUtf8());
    qputenv("XDG_MENU_PREFIX", "");
    QCoreApplication app(argc, argv);
    ServiceLookupTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "service_lookup_test.moc"
