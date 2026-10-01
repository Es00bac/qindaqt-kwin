// SPDX-License-Identifier: GPL-2.0-or-later
#include "captureauthority.h"
#include "core/outputbackend.h"
#include "kwin_wayland_test.h"
#include "main.h"
#include "wayland/clientconnection.h"
#include "wayland/display.h"
#include "wayland_server.h"

#include <QEventLoop>
#include <QProcess>
#include <QSet>
#include <QTimer>

#include <array>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-server-core.h>

namespace KWin
{
namespace
{
// A real wl_global for every dangerous category prevents the test from
// passing simply because a plugin happened not to register that interface.
const std::array<wl_interface, 17> testInterfaces{{
    {"wl_output", 1, 0, nullptr, 0, nullptr},
    {"zxdg_output_manager_v1", 1, 0, nullptr, 0, nullptr},
    {"zkde_screencast_unstable_v1", 1, 0, nullptr, 0, nullptr},
    {"org_kde_plasma_window_management", 1, 0, nullptr, 0, nullptr},
    {"org_kde_kwin_fake_input", 1, 0, nullptr, 0, nullptr},
    {"org_kde_plasma_activation_feedback", 1, 0, nullptr, 0, nullptr},
    {"kde_lockscreen_overlay_v1", 1, 0, nullptr, 0, nullptr},
    {"qindaqt_capture_test_restricted_v1", 1, 0, nullptr, 0, nullptr},
    {"ext_session_lock_manager_v1", 1, 0, nullptr, 0, nullptr},
    {"zwp_input_panel_v1", 1, 0, nullptr, 0, nullptr},
    {"zwp_input_method_v1", 1, 0, nullptr, 0, nullptr},
    {"xwayland_shell_v1", 1, 0, nullptr, 0, nullptr},
    {"zwp_xwayland_keyboard_grab_manager_v1", 1, 0, nullptr, 0, nullptr},
    {"wp_security_context_manager_v1", 1, 0, nullptr, 0, nullptr},
    {"qindaqt_capture_unknown_v1", 1, 0, nullptr, 0, nullptr},
    {"wl_compositor", 1, 0, nullptr, 0, nullptr},
    {"xdg_wm_base", 1, 0, nullptr, 0, nullptr},
}};

void bindTestGlobal(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    const auto *interface = static_cast<const wl_interface *>(data);
    if (wl_resource *resource = wl_resource_create(client, interface, int(version), id)) {
        wl_resource_set_implementation(resource, nullptr, nullptr, nullptr);
    }
}

struct ProbeResult
{
    bool completed = false;
    int exitCode = -1;
    pid_t serverPid = 0;
    QByteArray output;
};

QSet<QByteArray> namesFrom(const QByteArray &output)
{
    QSet<QByteArray> result;
    for (const QByteArray &line : output.split('\n')) {
        const qsizetype space = line.indexOf(' ');
        if (space > 0) {
            result.insert(line.mid(space + 1).trimmed());
        }
    }
    return result;
}
}

class CaptureInterfaceFilterTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void exactCaptureAndGuiFilterBeforeSelfPid();

private:
    ProbeResult probe(bool publicSocket, bool tag, bool gui,
                      CaptureWire::Public::Scope scope,
                      uint32_t bindId = 0, const char *bindName = nullptr);
    std::array<wl_global *, testInterfaces.size()> m_globals{};
};

void CaptureInterfaceFilterTest::initTestCase()
{
    QVERIFY(!qEnvironmentVariableIsSet("KWIN_WAYLAND_NO_PERMISSION_CHECKS"));
    const rlimit noCore{0, 0};
    QVERIFY(setrlimit(RLIMIT_CORE, &noCore) == 0);
    QVERIFY(prctl(PR_SET_DUMPABLE, 0L) == 0);
    auto *server = waylandServer();
    QVERIFY(server->init(QStringLiteral("qindaqt-8")));
    static_cast<WaylandTestApplication *>(kwinApp())->setInputMethodServerToStart(QStringLiteral("internal"));
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    kwinApp()->start();
    Test::setOutputConfig({Rect(0, 0, 1100, 820)});
    QTRY_VERIFY(server->captureAuthority() && server->captureAuthority()->privacyAllows());
    wl_display *native = *server->display();
    for (size_t i = 0; i < testInterfaces.size(); ++i) {
        m_globals[i] = wl_global_create(native, &testInterfaces[i], 1,
                                         const_cast<wl_interface *>(&testInterfaces[i]), bindTestGlobal);
        QVERIFY(m_globals[i]);
    }
}

void CaptureInterfaceFilterTest::cleanupTestCase()
{
    for (wl_global *global : m_globals) {
        if (global) {
            wl_global_destroy(global);
        }
    }
}

ProbeResult CaptureInterfaceFilterTest::probe(bool publicSocket, bool tag, bool gui,
                                              CaptureWire::Public::Scope scope,
                                              uint32_t bindId, const char *bindName)
{
    ProbeResult result;
    int childFd = -1;
    QPointer<ClientConnection> client;
    if (!publicSocket) {
        int pair[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair)) {
            return result;
        }
        client = waylandServer()->display()->createClient(pair[0]);
        if (!client) {
            close(pair[0]);
            close(pair[1]);
            return result;
        }
        childFd = pair[1];
        result.serverPid = client->processId();
        if (tag) {
            waylandServer()->captureAuthority()->tagTestClient(client, scope, gui);
        }
    }

    QProcess process;
    process.setProgram(QStringLiteral(QINDAQT_CAPTURE_INTERFACE_PROBE_PATH));
    if (publicSocket) {
        process.setArguments({QStringLiteral("--public")});
    } else if (bindId && bindName) {
        process.setArguments({QString::number(bindId), QString::fromLatin1(bindName)});
    }
    if (childFd >= 0) {
        QProcess::UnixProcessParameters parameters;
        parameters.flags = QProcess::UnixProcessFlag::CloseFileDescriptors
            | QProcess::UnixProcessFlag::DisableCoreDumps;
        parameters.lowestFileDescriptorToClose = 4;
        process.setUnixProcessParameters(parameters);
        process.setChildProcessModifier([childFd] {
            if (dup2(childFd, 3) < 0 || fcntl(3, F_SETFD, 0) < 0) {
                _exit(127);
            }
        });
    }
    process.start();
    const bool started = process.waitForStarted(3000);
    if (childFd >= 0) {
        close(childFd);
    }
    if (started) {
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         &loop, &QEventLoop::quit);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        deadline.start(5000);
        if (process.state() != QProcess::NotRunning) {
            loop.exec();
        }
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000);
        } else {
            result.completed = true;
            result.exitCode = process.exitCode();
            result.output = process.readAllStandardOutput();
        }
    }
    if (client) {
        client->destroy();
    }
    return result;
}

void CaptureInterfaceFilterTest::exactCaptureAndGuiFilterBeforeSelfPid()
{
    const auto screenCast = CaptureWire::Public::Scope::ScreenCast;
    const auto screenshot = CaptureWire::Public::Scope::Screenshot;
    const auto reference = probe(false, false, false, screenCast);
    QVERIFY(reference.completed);
    QCOMPARE(reference.exitCode, 0);
    QCOMPARE(reference.serverPid, getpid());
    const auto referenceNames = namesFrom(reference.output);
    QVERIFY(referenceNames.contains("org_kde_plasma_window_management"));
    QVERIFY(referenceNames.contains("zkde_screencast_unstable_v1"));

    const auto publicClient = probe(true, false, false, screenCast);
    QVERIFY(publicClient.completed);
    QCOMPARE(publicClient.exitCode, 0);
    QVERIFY(!namesFrom(publicClient.output).contains("zkde_screencast_unstable_v1"));

    const auto capture = probe(false, true, false, screenCast);
    QVERIFY(capture.completed);
    QCOMPARE(capture.exitCode, 0);
    QCOMPARE(capture.serverPid, getpid());
    const auto captureNames = namesFrom(capture.output);
    for (const auto *name : {"wl_output", "zxdg_output_manager_v1", "zkde_screencast_unstable_v1"}) {
        QVERIFY(captureNames.contains(name));
    }
    for (const auto &name : captureNames) {
        QVERIFY(name == "wl_output" || name == "zxdg_output_manager_v1"
                || name == "zkde_screencast_unstable_v1");
    }

    const auto still = probe(false, true, false, screenshot);
    QVERIFY(still.completed);
    QCOMPARE(still.exitCode, 0);
    const auto stillNames = namesFrom(still.output);
    QVERIFY(stillNames.contains("wl_output"));
    QVERIFY(stillNames.contains("zxdg_output_manager_v1"));
    QVERIFY(!stillNames.contains("zkde_screencast_unstable_v1"));
    QCOMPARE(stillNames.size(), 2);

    const auto gui = probe(false, true, true, screenCast);
    QVERIFY(gui.completed);
    QCOMPARE(gui.exitCode, 0);
    QCOMPARE(gui.serverPid, getpid());
    const auto guiNames = namesFrom(gui.output);
    QVERIFY(guiNames.contains("wl_compositor"));
    QVERIFY(guiNames.contains("xdg_wm_base"));
    QVERIFY(guiNames.contains("qindaqt_capture_unknown_v1"));
    for (size_t i = 2; i <= 13; ++i) {
        QVERIFY2(!guiNames.contains(testInterfaces[i].name), testInterfaces[i].name);
        if (i != 2) { // ScreenCast alone is granted to this capture scope.
            QVERIFY2(!captureNames.contains(testInterfaces[i].name), testInterfaces[i].name);
        }
    }
    QVERIFY(!captureNames.contains("qindaqt_capture_unknown_v1"));

    // Guessed binds use the ID of a global that an untagged self-PID client
    // could enumerate. The tagged disposable peer must be disconnected.
    int pair[2];
    QVERIFY(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    auto *referenceClient = waylandServer()->display()->createClient(pair[0]);
    QVERIFY(referenceClient);
    close(pair[1]);
    for (size_t i : {size_t(2), size_t(3), size_t(4), size_t(5), size_t(7), size_t(9), size_t(10), size_t(11), size_t(12), size_t(13)}) {
        const uint32_t id = wl_global_get_name(m_globals[i], referenceClient->client());
        QVERIFY(id != 0);
        if (i != 2) {
            const auto denied = probe(false, true, false, screenCast, id, testInterfaces[i].name);
            QVERIFY(denied.completed);
            QCOMPARE(denied.exitCode, 10);
        }
        const auto deniedGui = probe(false, true, true, screenCast, id, testInterfaces[i].name);
        QVERIFY(deniedGui.completed);
        QCOMPARE(deniedGui.exitCode, 10);
    }
    referenceClient->destroy();
}
}

WAYLANDTEST_MAIN(KWin::CaptureInterfaceFilterTest)
#include "capture_interface_filter_test.moc"
