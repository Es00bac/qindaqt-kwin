// SPDX-License-Identifier: GPL-2.0-or-later
#include "captureauthority.h"
#include "core/outputbackend.h"
#include "kwin_wayland_test.h"
#include "main.h"
#include "wayland_server.h"

#include <QEventLoop>
#include <QSocketNotifier>
#include <QTimer>

#include <cstdio>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>

namespace KWin
{
class NativeCaptureAuthorityTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void serveNativeCapture();

private:
    bool m_nativeInput = false;
};

void NativeCaptureAuthorityTest::initTestCase()
{
    // The production compositor enters PR0 before startup. The disposable
    // private runner must establish the same invariant before child exec.
    const rlimit noCore{0, 0};
    QVERIFY(setrlimit(RLIMIT_CORE, &noCore) == 0);
    QVERIFY(prctl(PR_SET_DUMPABLE, 0L) == 0);
    QCOMPARE(prctl(PR_GET_DUMPABLE), 0);

    auto *server = waylandServer();
    m_nativeInput = qEnvironmentVariable("QINDAQT_PRIVATE_NATIVE_INPUT") == QLatin1String("1");
    server->setCaptureTestPrograms(m_nativeInput ? QStringLiteral("@native-input-no-capture-broker")
                                               : QStringLiteral(QINDAQT_PRIVATE_CAPTURE_BROKER),
                                   QStringLiteral(QINDAQT_PRIVATE_CAPTURE_HELPER));
    QVERIFY(server->init(m_nativeInput ? QStringLiteral("qindaqt-7") : QStringLiteral("qindaqt-8")));
    static_cast<WaylandTestApplication *>(kwinApp())->setInputMethodServerToStart(QStringLiteral("internal"));
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    kwinApp()->start();
    if (m_nativeInput) {
        Test::setOutputConfig({Rect(0, 0, 1000, 760)});
    } else if (qEnvironmentVariable("QINDAQT_PRIVATE_CAPTURE_TWO_OUTPUTS") == QLatin1String("1")) {
        Test::setOutputConfig({Rect(0, 0, 1100, 820), Rect(1100, 0, 800, 600)});
    } else {
        Test::setOutputConfig({Rect(0, 0, 1100, 820)});
    }
    if (!m_nativeInput) {
        QTRY_VERIFY_WITH_TIMEOUT(server->captureAuthority()
                                 && server->captureAuthority()->testBrokerReady(), 10000);
    } else {
        QVERIFY(server->captureAuthority());
        QVERIFY(!server->captureAuthority()->testBrokerReady());
    }
}

void NativeCaptureAuthorityTest::serveNativeCapture()
{
    // AGENT-CONTRACT: the separately built consumer runner owns the one
    // private D-Bus/PipeWire environment and calls its real frontend matrix
    // while this virtual compositor stays alive. This fixture is never
    // registered as a standalone CTest success or installed.
    // AGENT-GUARD: input mode uses real core admission/lock/attachment, while
    // no capture broker exists to authorize publication. Default unchanged.
    std::fputs(m_nativeInput ? "NATIVE_INPUT_READY qindaqt-7 1000x760\n"
                            : "CAPTURE_AUTHORITY_READY qindaqt-8 1100x820\n", stdout);
    std::fflush(stdout);
    QEventLoop loop;
    bool endedByPeer = false;
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    connect(&input, &QSocketNotifier::activated, &loop, [&] {
        char ignored[64];
        const ssize_t count = read(STDIN_FILENO, ignored, sizeof(ignored));
        if (count == 0) {
            endedByPeer = true;
            loop.quit();
        } else if (count < 0) {
            loop.quit();
        }
    });
    QTimer deadline;
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    deadline.start(240000);
    loop.exec();
    QVERIFY(endedByPeer);
}
}

WAYLANDTEST_MAIN(KWin::NativeCaptureAuthorityTest)
#include "native_capture_authority_test.moc"
