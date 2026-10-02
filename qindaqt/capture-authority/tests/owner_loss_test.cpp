// SPDX-License-Identifier: GPL-2.0-or-later
#include "captureauthority.h"
#include "compositor.h"
#include "opengl/eglbackend.h"
#include "opengl/eglcontext.h"
#include "kwin_wayland_test.h"
#include "main.h"
#include "pluginmanager.h"
#include "screenshot.h"
#include "screenshotoperation.h"
#include "wayland_server.h"

#include <KConfigGroup>

#include <QElapsedTimer>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusPendingCall>
#include <QDBusUnixFileDescriptor>
#include <QThreadPool>

#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace KWin
{
namespace
{
const QString frontendName = QStringLiteral("org.freedesktop.portal.Desktop");
const QString backendName = QStringLiteral("org.freedesktop.impl.portal.desktop.qindaqt.capture");
const QString peerPath = QStringLiteral("/org/qindaqt/Test/CaptureOwnerLoss");
const QString peerInterface = QStringLiteral("org.qindaqt.Test.CaptureOwnerLoss");

bool actorLive(const QString &sender, uint *pid = nullptr)
{
    auto *bus = QDBusConnection::sessionBus().interface();
    const QDBusReply<QString> owner = bus->serviceOwner(sender);
    const QDBusReply<uint> actualPid = bus->servicePid(sender), uid = bus->serviceUid(sender);
    if (!owner.isValid() || owner.value() != sender || !actualPid.isValid()
        || !uid.isValid() || uid.value() != uint(geteuid())) return false;
    FileDescriptor descriptor(int(syscall(SYS_pidfd_open, actualPid.value(), 0)));
    pollfd event{descriptor.get(), POLLIN, 0};
    if (!descriptor || poll(&event, 1, 0) != 0) return false;
    if (pid) *pid = actualPid.value();
    return true;
}

bool claim(const QDBusConnection &bus, const QString &name, bool replace = false)
{
    const auto reply = bus.interface()->registerService(name,
        replace ? QDBusConnectionInterface::ReplaceExistingService : QDBusConnectionInterface::DontQueueService,
        QDBusConnectionInterface::AllowReplacement);
    return reply.isValid() && reply.value() == QDBusConnectionInterface::ServiceRegistered;
}
}

class CaptureOwnerLossTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void ownerLoss_data();
    void ownerLoss();
    void cleanup();
private:
    std::unique_ptr<ScreenShotManager> m_screenshot;
    QHash<quint64, QSet<ScreenShotTestEvent>> m_events;
    QString m_activeBroker;
    QList<quint64> m_activeJobs;
};

void CaptureOwnerLossTest::initTestCase()
{
    const rlimit noCore{0, 0};
    QVERIFY(setrlimit(RLIMIT_CORE, &noCore) == 0);
    QVERIFY(prctl(PR_SET_DUMPABLE, 0L) == 0);
    QCOMPARE(prctl(PR_GET_DUMPABLE), 0);
    auto *server = waylandServer();
    server->setCaptureTestPrograms(QStringLiteral(QINDAQT_OWNER_LOSS_PEER), QStringLiteral(QINDAQT_OWNER_LOSS_PEER));
    QVERIFY(server->init(QStringLiteral("qindaqt-8")));
    static_cast<WaylandTestApplication *>(kwinApp())->setInputMethodServerToStart(QStringLiteral("internal"));
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    // AGENT-GUARD: this target compiles the production Screenshot sources
    // with its observer. Do not autoload a second module instance that would
    // own the same DBus object but have an unrelated private observer state.
    KConfigGroup(kwinApp()->config(), QStringLiteral("Plugins")).writeEntry("screenshotEnabled", false);
    kwinApp()->start();
    Test::setOutputConfig({Rect(0, 0, 1100, 820)});
    // AGENT-GUARD: the render node is allocation-only test infrastructure;
    // qualification must use the standard virtual backend with software GL.
    auto *egl = qobject_cast<EglBackend *>(Compositor::self()->backend());
    QVERIFY(egl && egl->openglContext());
    const QByteArray renderer(egl->openglContext()->renderer().data(), egl->openglContext()->renderer().size());
    qInfo() << "OWNER_LOSS_ACTUAL_RENDERER" << renderer;
    QVERIFY(egl->openglContext()->isSoftwareRenderer());
    QVERIFY(renderer.toLower().contains("llvmpipe"));
    QTRY_VERIFY_WITH_TIMEOUT(server->captureAuthority() && server->captureAuthority()->testBrokerReady(), 10000);
    QVERIFY(!kwinApp()->pluginManager()->loadedPlugins().contains(QStringLiteral("screenshot")));
    const QString screenshotPath = QStringLiteral("/org/qindaqt/KWin/ScreenShot2");
    QVERIFY(!QDBusConnection::sessionBus().objectRegisteredAt(screenshotPath));
    m_screenshot = std::make_unique<ScreenShotManager>();
    auto *endpoint = QDBusConnection::sessionBus().objectRegisteredAt(screenshotPath);
    QVERIFY(endpoint);
    QCOMPARE(endpoint->parent(), static_cast<QObject *>(m_screenshot.get()));
}

void CaptureOwnerLossTest::ownerLoss_data()
{
    QTest::addColumn<bool>("backend");
    QTest::addColumn<bool>("replacement");
    QTest::newRow("frontend-release") << false << false;
    QTest::newRow("frontend-replacement") << false << true;
    QTest::newRow("backend-release") << true << false;
    QTest::newRow("backend-replacement") << true << true;
}

void CaptureOwnerLossTest::ownerLoss()
{
    QFETCH(bool, backend);
    QFETCH(bool, replacement);
    auto *authority = waylandServer()->captureAuthority();
    QTRY_VERIFY_WITH_TIMEOUT(authority->testBrokerReady(), 6000);
    const QString suffix = QString::fromLatin1(QTest::currentDataTag());
    const QString frontendConnection = suffix + QStringLiteral("-frontend");
    const QString callerConnection = suffix + QStringLiteral("-caller");
    const QString replacementConnection = suffix + QStringLiteral("-replacement");
    auto frontend = QDBusConnection::connectToBus(QDBusConnection::SessionBus, frontendConnection);
    auto caller = QDBusConnection::connectToBus(QDBusConnection::SessionBus, callerConnection);
    auto successor = QDBusConnection::connectToBus(QDBusConnection::SessionBus, replacementConnection);
    struct Disconnect
    {
        QStringList names;
        ~Disconnect() { for (const auto &name : names) QDBusConnection::disconnectFromBus(name); }
    } disconnect{{frontendConnection, callerConnection, replacementConnection}};
    QVERIFY(frontend.isConnected() && caller.isConnected() && successor.isConnected());
    QVERIFY(claim(frontend, frontendName));
    const QDBusReply<QString> owner = QDBusConnection::sessionBus().interface()->serviceOwner(backendName);
    QVERIFY(owner.isValid());
    const QString brokerSender = owner.value();
    m_activeBroker = brokerSender;
    QDBusInterface broker(brokerSender, peerPath, peerInterface, QDBusConnection::sessionBus());
    QVERIFY(broker.isValid());
    const auto begin = [&](CaptureWire::Public::Scope scope) {
        const QDBusReply<qulonglong> reply = broker.call(QStringLiteral("StartJob"), frontend.baseService(), caller.baseService(), int(scope));
        if (!reply.isValid() || !reply.value()) return qulonglong(0);
        m_activeJobs.append(reply.value());
        return reply.value();
    };
    const quint64 screenshotJob = begin(CaptureWire::Public::Scope::Screenshot);
    const quint64 streamJob = begin(CaptureWire::Public::Scope::ScreenCast);
    QVERIFY(screenshotJob && streamJob && screenshotJob != streamJob);
    QString helperSender, streamSender;
    const auto helperFor = [&](quint64 job) {
        const QDBusReply<QString> reply = broker.call(QStringLiteral("Helper"), QVariant::fromValue<qulonglong>(job));
        return reply.isValid() ? reply.value() : QString();
    };
    QTRY_VERIFY_WITH_TIMEOUT(!(helperSender = helperFor(screenshotJob)).isEmpty(), 6000);
    QTRY_VERIFY_WITH_TIMEOUT(!(streamSender = helperFor(streamJob)).isEmpty(), 6000);
    uint helperPid = 0;
    QVERIFY(actorLive(helperSender, &helperPid));
    QVERIFY(actorLive(streamSender));
    QVERIFY(authority->streamAllowed(streamJob));

    int descriptors[2];
    QVERIFY(pipe2(descriptors, O_CLOEXEC | O_NONBLOCK) == 0);
    FileDescriptor reader(descriptors[0]), writer(descriptors[1]);
    const QByteArray sentinel(4096, char(0xa5));
    qsizetype filled = 0;
    for (;;) {
        const ssize_t count = write(writer.get(), sentinel.constData(), sentinel.size());
        if (count < 0) { QCOMPARE(errno, EAGAIN); break; }
        QVERIFY(count > 0);
        filled += count;
    }
    QVERIFY(filled > 0 && filled < 1024 * 768 * 4);
    setScreenShotTestObserver(this, [this](quint64 id, ScreenShotTestEvent event) { m_events[id].insert(event); });
    QDBusInterface helper(helperSender, peerPath, peerInterface, QDBusConnection::sessionBus());
    QVERIFY(helper.isValid());
    {
        QDBusUnixFileDescriptor fd(writer.get());
        const QDBusReply<bool> issued = helper.call(QStringLiteral("Capture"), QVariant::fromValue(fd));
        QVERIFY(issued.isValid() && issued.value());
    }
    writer.reset();
    QTRY_VERIFY_WITH_TIMEOUT(m_events[screenshotJob].contains(ScreenShotTestEvent::WriterWaiting), 6000);
    QVERIFY(authority->screenshotOperationAllowed(screenshotJob, helperSender, helperPid));
    QVERIFY(!m_events[screenshotJob].contains(ScreenShotTestEvent::WriterExited));

    // This stream is deliberately a local idle observer. It verifies direct
    // notification ordering only; real PipeWire node closure is a consumer gate.
    QObject observations;
    bool streamClosed = false;
    QSet<quint64> barriers;
    QSignalSpy revoked(authority, &CaptureAuthority::jobRevoked);
    connect(authority, &CaptureAuthority::jobRevoked, &observations, [&](quint64 id) {
        if (id == streamJob) streamClosed = true;
    }, Qt::DirectConnection);
    connect(authority, &CaptureAuthority::jobRevoked, &observations, [&](quint64 id) {
        barriers.insert(id);
    }, Qt::QueuedConnection);

    // Prove the releasing actors' unique owners/UID/PIDs and kernel pidfds
    // are live immediately before mutation. Loss must come from the role name.
    QVERIFY(actorLive(frontend.baseService()));
    QVERIFY(actorLive(brokerSender));
    QVERIFY(actorLive(caller.baseService()));
    const QString name = backend ? backendName : frontendName;
    const QString oldSender = backend ? brokerSender : frontend.baseService();
    if (replacement) {
        QVERIFY(claim(successor, name, true));
        QCOMPARE(QDBusConnection::sessionBus().interface()->serviceOwner(name).value(), successor.baseService());
    } else if (backend) {
        broker.asyncCall(QStringLiteral("Release")); // correct retirement can kill broker before its reply
    } else {
        QVERIFY(frontend.unregisterService(name));
    }
    QTRY_VERIFY_WITH_TIMEOUT(barriers.contains(screenshotJob) && barriers.contains(streamJob), 3000);
    QCOMPARE(revoked.count(), 2);
    QVERIFY(streamClosed);
    QVERIFY(!authority->streamAllowed(streamJob));
    QVERIFY(!authority->screenshotOperationAllowed(screenshotJob, helperSender, helperPid));
    QVERIFY(!authority->beginScreenshot(helperSender, helperPid));
    if (!backend) {
        QVERIFY(actorLive(oldSender));
        if (replacement) QVERIFY(successor.unregisterService(name));
        QVERIFY(claim(frontend, name));
        QVERIFY(!authority->streamAllowed(streamJob));
        QVERIFY(!authority->screenshotOperationAllowed(screenshotJob, helperSender, helperPid));
        QVERIFY(frontend.unregisterService(name));
    } else {
        QVERIFY(!authority->testBrokerReady());
        if (replacement) QVERIFY(successor.unregisterService(name));
    }
    QTRY_VERIFY_WITH_TIMEOUT(m_events[screenshotJob].contains(ScreenShotTestEvent::WriterExited), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(m_events[screenshotJob].contains(ScreenShotTestEvent::ErrorReply), 3000);
    QVERIFY(!m_events[screenshotJob].contains(ScreenShotTestEvent::SuccessReply));
    qsizetype drained = 0;
    bool eof = false;
    for (;;) {
        char bytes[4096];
        const ssize_t count = read(reader.get(), bytes, sizeof(bytes));
        if (count == 0) { eof = true; break; }
        QVERIFY(count > 0); // every extra writer is closed; EAGAIN is failure
        QCOMPARE(QByteArray(bytes, count), QByteArray(count, char(0xa5)));
        drained += count;
        QVERIFY(drained <= filled);
    }
    QVERIFY(eof);
    QCOMPARE(drained, filled); // zero image bytes following synchronous revoke
    QCOMPARE(revoked.count(), 2);
}

void CaptureOwnerLossTest::cleanup()
{
    // AGENT-GUARD: negative controls fail while the writer is intentionally
    // full. Always ask the authentic broker to revoke via the normal wire,
    // and pump the compositor while waiting; blocking its thread would delay
    // the very synchronous revoke needed to release the worker.
    for (const auto id : m_activeJobs) {
        auto call = QDBusMessage::createMethodCall(m_activeBroker, peerPath, peerInterface, QStringLiteral("Revoke"));
        call.setArguments({QVariant::fromValue<qulonglong>(id)});
        QDBusConnection::sessionBus().asyncCall(call, 1000);
    }
    QElapsedTimer deadline;
    deadline.start();
    do {
        QTest::qWait(10);
    } while (QThreadPool::globalInstance()->activeThreadCount() && deadline.elapsed() < 6000);
    const bool quiescent = QThreadPool::globalInstance()->waitForDone(0);
    QCoreApplication::sendPostedEvents();
    // Do not put a QVERIFY before clearing the observer: it returns early.
    setScreenShotTestObserver(nullptr, {});
    m_events.clear();
    m_activeJobs.clear();
    m_activeBroker.clear();
    QVERIFY(quiescent);
}
}
WAYLANDTEST_MAIN(KWin::CaptureOwnerLossTest)
#include "owner_loss_test.moc"
