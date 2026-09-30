// SPDX-License-Identifier: GPL-2.0-or-later
#include "lockerlauncher.h"
#include "lockerlaunchpolicy.h"
#include "native_lock_fixture.h"
#include "native_session_lock_test.h"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <cerrno>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
namespace KWin
{
void NativeSessionLockTest::launcherPolicy()
{
    QVERIFY(hasRestrictedPtracePolicy());
    QVERIFY(isRootManagedLockerFile(QStringLiteral("/usr/bin/true")));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("untrusted")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("untrusted");
    file.close();
    QVERIFY(!isRootManagedLockerFile(file.fileName()));
    const auto alias = dir.filePath(QStringLiteral("alias"));
    QVERIFY(QFile::link(QStringLiteral("/usr/bin/true"), alias));
    QVERIFY(!isRootManagedLockerFile(alias));
}
void NativeSessionLockTest::launcherEnvironment()
{
    QProcessEnvironment host;
    host.insert(QStringLiteral("HOME"), QStringLiteral("/fixture-home"));
    host.insert(QStringLiteral("LANG"), QStringLiteral("en_US.UTF-8"));
    for (const auto &name : {"LD_PRELOAD", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "WAYLAND_DISPLAY", "QINDAQT_FIXTURE_INJECTED"}) {
        host.insert(QLatin1String(name), QStringLiteral("hostile"));
    }
    const auto safe = lockerEnvironment(host, 3);
    QCOMPARE(safe.value(QStringLiteral("HOME")), QStringLiteral("/fixture-home"));
    QCOMPARE(safe.value(QStringLiteral("LANG")), QStringLiteral("en_US.UTF-8"));
    QCOMPARE(safe.value(QStringLiteral("WAYLAND_SOCKET")), QStringLiteral("3"));
    for (const auto &name : {"LD_PRELOAD", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "WAYLAND_DISPLAY", "QINDAQT_FIXTURE_INJECTED"}) {
        QVERIFY(!safe.contains(QLatin1String(name)));
    }
    host.insert(QStringLiteral("LANG"), QStringLiteral("/hostile/path"));
    QCOMPARE(lockerEnvironment(host, 3).value(QStringLiteral("LANG")), QStringLiteral("C.UTF-8"));
}
void NativeSessionLockTest::launcherConnectionIsolation()
{
    auto *launcher = waylandServer()->lockerLauncher();
    QVERIFY(launcher);
    const QByteArray oldPreload = qgetenv("LD_PRELOAD");
    qputenv("LD_PRELOAD", QINDAQT_LOCK_INJECTION_PATH);
    struct Restore
    {
        QByteArray value;
        ~Restore()
        {
            if (value.isEmpty()) {
                qunsetenv("LD_PRELOAD");
            } else {
                qputenv("LD_PRELOAD", value);
            }
        }
    } restore{oldPreload};
    launcher->setTestProgram(QStringLiteral(QINDAQT_LOCK_PROBE_PATH));
    struct Stop
    {
        LockerLauncher *launcher;
        ~Stop()
        {
            launcher->stopTestFixture();
        }
    } stop{launcher};
    QVERIFY(launcher->start());
    QByteArray privateOutput;
    QTRY_VERIFY(([&] {
        waylandServer()->display()->flush();
        privateOutput += launcher->readTestOutput();
        return privateOutput.contains("LOCK_GLOBAL=1 DUMPABLE=0 INJECTED=0");
    })());
    QVERIFY(launcher->testIsRunning());
    const auto pid = static_cast<pid_t>(launcher->testProcessId());
    errno = 0;
    const long attached = ptrace(PTRACE_ATTACH, pid, nullptr, nullptr);
    const int attachError = errno;
    if (attached == 0) {
        // Repair only our disposable child if the security assertion fails.
        int status;
        waitpid(pid, &status, WUNTRACED);
        ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
    }
    QCOMPARE(attached, -1L);
    QCOMPARE(attachError, EPERM);
    rlimit limit{};
    QVERIFY(getrlimit(RLIMIT_CORE, &limit) == 0);
    QCOMPARE(limit.rlim_cur, rlim_t(0));

    // The same real executable, with an actually loaded hostile witness, uses
    // an unowned connection on the private test Display. Inode/self-launch is
    // insufficient; its global is absent despite the injected constructor.
    int pair[2];
    QVERIFY(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    auto *peer = waylandServer()->display()->createClient(pair[0]);
    QVERIFY(peer);
    QProcess hostile;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("WAYLAND_SOCKET"), QStringLiteral("3"));
    hostile.setProcessEnvironment(environment);
    hostile.setProgram(QStringLiteral(QINDAQT_LOCK_PROBE_PATH));
    hostile.setChildProcessModifier([fd = pair[1]] {
        if (dup2(fd, 3) < 0 || fcntl(3, F_SETFD, 0) < 0) {
            _exit(127);
        }
    });
    hostile.start();
    QTRY_VERIFY(([&] {
        waylandServer()->display()->flush();
        return hostile.state() == QProcess::NotRunning;
    })());
    close(pair[1]);
    QCOMPARE(hostile.exitCode(), 0);
    QVERIFY(hostile.readAllStandardOutput().contains("LOCK_GLOBAL=0 DUMPABLE=0 INJECTED=1"));
    launcher->stopTestFixture();
    QTRY_VERIFY(!launcher->testIsRunning());
    launcher->setTestProgram({});
}
void NativeSessionLockTest::captureLockReceipt(const QString &nonce, bool admitted, const QDBusMessage &message)
{
    m_receiptNonce = nonce;
    m_receiptAdmitted = admitted;
    m_receiptMessage = message;
}
void NativeSessionLockTest::captureStateReceipt(const QString &nonce, bool locked, bool protectedPresentation, const QDBusMessage &message)
{
    m_receiptNonce = nonce;
    m_receiptLocked = locked;
    m_receiptProtected = protectedPresentation;
    m_receiptMessage = message;
}
void NativeSessionLockTest::nativeLockBackendProtection()
{
    const QString path = QStringLiteral("/org/qindaqt/KWin/NativeLock");
    const QString interface = QStringLiteral("org.qindaqt.KWin.NativeLock1");
    const QString service = QDBusConnection::sessionBus().baseService();
    auto invoke = [](QDBusMessage message) {
        QDBusPendingCallWatcher watcher(QDBusConnection::sessionBus().asyncCall(message));
        QSignalSpy done(&watcher, &QDBusPendingCallWatcher::finished);
        if (!watcher.isFinished() && !done.wait()) {
            return QDBusMessage::createError(QStringLiteral("fixture.Timeout"), QStringLiteral("private IPC did not finish"));
        }
        return watcher.reply();
    };
    auto *controller = waylandServer()->sessionLock();
    auto *launcher = waylandServer()->lockerLauncher();
    // A guaranteed unavailable disposable child avoids invoking any installed
    // greeter or PAM service; launcher loss must not remove the physical fence.
    launcher->setTestProgram(QStringLiteral("/nonexistent/qindaqt-private-lock-probe"));
    QSignalSpy protectedChanged(controller, &SessionLockController::presentationProtectionChanged);
    struct Held
    {
        QList<RenderLoop *> loops;
        ~Held()
        {
            for (auto *loop : loops) {
                loop->uninhibit();
            }
        }
    } held;
    for (auto *output : kwinApp()->outputBackend()->outputs()) {
        auto *loop = output->renderLoop();
        loop->inhibit();
        held.loops.append(loop);
    }
    auto bus = QDBusConnection::sessionBus();
    const QString lockNonce = QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')).toLower();
    QVERIFY(bus.connect(service, path, interface, QStringLiteral("lockAdmissionReceipt"), QStringList{lockNonce}, QStringLiteral("sb"), this,
                        SLOT(captureLockReceipt(QString, bool, QDBusMessage))));
    auto lockCall = QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("RequestLockWithReceipt"));
    lockCall << lockNonce;
    const auto request = invoke(lockCall);
    QTRY_COMPARE(m_receiptNonce, lockNonce);
    bus.disconnect(service, path, interface, QStringLiteral("lockAdmissionReceipt"), QStringList{lockNonce}, QStringLiteral("sb"), this,
                   SLOT(captureLockReceipt(QString, bool, QDBusMessage)));
    QCOMPARE(request.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(request.signature(), QString());
    QCOMPARE(m_receiptNonce, lockNonce);
    QVERIFY(m_receiptAdmitted);
    QCOMPARE(m_receiptMessage.type(), QDBusMessage::SignalMessage);
    QCOMPARE(m_receiptMessage.service(), service);
    QCOMPARE(m_receiptMessage.path(), path);
    QCOMPARE(m_receiptMessage.interface(), interface);
    QCOMPARE(m_receiptMessage.member(), QStringLiteral("lockAdmissionReceipt"));
    QCOMPARE(m_receiptMessage.signature(), QStringLiteral("sb"));
    QVERIFY(controller->isLocked());
    QVERIFY(!controller->isPresentationProtected());
    QTRY_VERIFY(!launcher->testIsRunning());
    launcher->setTestProgram({});
    for (auto *loop : held.loops) {
        loop->uninhibit();
    }
    held.loops.clear();
    QTRY_VERIFY(controller->isPresentationProtected());
    QVERIFY(!controller->client());
    auto get = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    get.setArguments({interface});
    const auto properties = invoke(get);
    QCOMPARE(properties.type(), QDBusMessage::ReplyMessage);
    const auto values = qdbus_cast<QVariantMap>(properties.arguments().first());
    QCOMPARE(values.value(QStringLiteral("Locked")).toBool(), true);
    QCOMPARE(values.value(QStringLiteral("Protected")).toBool(), true);
    const QString stateNonce = QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')).toLower();
    QVERIFY(bus.connect(service, path, interface, QStringLiteral("stateReceipt"), QStringList{stateNonce}, QStringLiteral("sbb"), this,
                        SLOT(captureStateReceipt(QString, bool, bool, QDBusMessage))));
    auto stateCall = QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("RequestStateWithReceipt"));
    stateCall << stateNonce;
    const auto stateReply = invoke(stateCall);
    QTRY_COMPARE(m_receiptNonce, stateNonce);
    bus.disconnect(service, path, interface, QStringLiteral("stateReceipt"), QStringList{stateNonce}, QStringLiteral("sbb"), this,
                   SLOT(captureStateReceipt(QString, bool, bool, QDBusMessage)));
    QCOMPARE(stateReply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(stateReply.signature(), QString());
    QCOMPARE(m_receiptNonce, stateNonce);
    QVERIFY(m_receiptLocked);
    QVERIFY(m_receiptProtected);
    QCOMPARE(m_receiptMessage.type(), QDBusMessage::SignalMessage);
    QCOMPARE(m_receiptMessage.service(), service);
    QCOMPARE(m_receiptMessage.member(), QStringLiteral("stateReceipt"));
    QCOMPARE(m_receiptMessage.signature(), QStringLiteral("sbb"));
    const auto xml = invoke(QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect")));
    QCOMPARE(xml.type(), QDBusMessage::ReplyMessage);
    QVERIFY(xml.arguments().first().toString().contains("RequestLock"));
    QVERIFY(!xml.arguments().first().toString().contains("Unlock"));
    QVERIFY(protectedChanged.count() >= 1);
}

}
