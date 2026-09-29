// SPDX-License-Identifier: GPL-2.0-or-later
// Real protected QtQuick greeter + disposable native PAM worker. Commands
// supply synthetic user intent; no boolean approval/unlock transport exists.
#include "core/outputconfiguration.h"
#include "backends/virtual/virtual_egl_backend.h"
#include "compositor.h"
#include "opengl/eglbackend.h"
#include "opengl/gltexture.h"
#include <QFileInfo>
#include <QFile>
#include <QRegularExpression>
#include "lockerlauncher.h"
#include "native_lock_fixture.h"
#include "native_session_lock_test.h"
namespace KWin {
void NativeSessionLockTest::nativeQtAuthentication_data()
{
    QTest::addColumn<double>("scale");
    QTest::newRow("normal") << 1.0;
    QTest::newRow("150-percent") << 1.5;
}
void NativeSessionLockTest::nativeQtAuthentication()
{
    QFETCH(double, scale);
    Test::setOutputConfig({Rect(0, 0, 1280, 1024), Rect(1280, 0, 1280, 1024)});
    OutputConfiguration configuration;
    for (auto *output : kwinApp()->outputBackend()->outputs()) configuration.changeSet(output)->scaleSetting = scale;
    QCOMPARE(workspace()->applyOutputConfiguration(configuration), OutputConfigurationError::None);
    for (auto *physical : kwinApp()->outputBackend()->outputs()) QCOMPARE(physical->scale(), scale);
    auto *launcher = waylandServer()->lockerLauncher();
    launcher->setTestProgram(QStringLiteral(QINDAQT_PRIVATE_QT_LOCK_GREETER));
    struct Restore {
        LockerLauncher *launcher;
        ~Restore() {
            launcher->stopTestFixture(); launcher->setTestProgram({});
            Test::setOutputConfig({Rect(0, 0, 1280, 1024), Rect(1280, 0, 1280, 1024)});
        }
    } restore{launcher};
    QVERIFY(waylandServer()->sessionLock()->requestNativeLock());
    QByteArray output;
    auto receive = [&](const QByteArray &marker) {
        waylandServer()->display()->flush(); output += launcher->readTestOutput();
        return output.contains(marker);
    };
    QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_PROMPT"), output.constData(), 10000);
    auto count = [] {
        waylandServer()->display()->flush();
        int mapped = 0;
        for (auto *window : workspace()->windows()) {
            if (window->isLockScreen() && window->windowItem()->isVisible() &&
                waylandServer()->isLockScreenInputSurface(window->surface())) ++mapped;
        }
        return mapped;
    };
    QTRY_COMPARE(count(), 2);
    QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_UI_READY"), output.constData(), 5000);
    QTRY_VERIFY2_WITH_TIMEOUT(receive(scale == 1.0 ? "DPR=1.00" : "DPR=1.50"), output.constData(), 5000);
    QVERIFY(waylandServer()->isScreenLocked());
    QVERIFY(waylandServer()->sessionLock()->isPresentationProtected());
    auto *backend = dynamic_cast<EglBackend *>(Compositor::self()->backend());
    QVERIFY(backend);
    auto painted = [&] {
        if (!backend->openglContext()->makeCurrent()) return false;
        const auto layers = backend->compatibleOutputLayers(kwinApp()->outputBackend()->outputs().first());
        if (layers.empty()) return false;
        auto *layer = dynamic_cast<VirtualEglLayer *>(layers[0]);
        if (!layer || !layer->texture()) return false;
        const auto image = layer->texture()->toImage();
        if (image.isNull() || image.pixelColor(image.width()/2, image.height()/2) == QColor(Qt::black)) return false;
        const auto file = QFileInfo(QStringLiteral(QINDAQT_PRIVATE_QT_LOCK_GREETER)).absolutePath() +
                          QStringLiteral("/native-lock-greeter-%1.png").arg(scale == 1.0 ? 100 : 150);
        return image.save(file);
    };
    QTRY_VERIFY(painted());
    auto send = [&](const QByteArray &command) {
        output.clear(); return launcher->writeTestInput(command + '\n') == command.size() + 1;
    };
    QVERIFY(send("saver-patrol"));
    QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_SAVER_READY"), output.constData(), 10000);
    QVERIFY(waylandServer()->isScreenLocked());
    QVERIFY(send("saver-reef"));
    QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_SAVER_READY"), output.constData(), 10000);
    QVERIFY(waylandServer()->isScreenLocked());
    if (scale == 1.0) {
        QVERIFY(send("bad-password"));
        QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_DENIED"), output.constData(), 5000);
        QVERIFY(waylandServer()->isScreenLocked());
        QVERIFY(send("begin"));
        QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_PROMPT"), output.constData(), 5000);
        QVERIFY(send("cancel"));
        QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_CANCELLED"), output.constData(), 5000);
        QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_IDLE"), output.constData(), 5000);
        QVERIFY(send("password")); // A late response after cancel cannot consume approval.
        QTest::qWait(50); waylandServer()->display()->flush();
        QVERIFY(waylandServer()->isScreenLocked());
        QVERIFY(send("account-deny"));
        QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_PROMPT"), output.constData(), 5000);
        QVERIFY(send("password"));
        QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_DENIED"), output.constData(), 5000);
        QVERIFY(waylandServer()->isScreenLocked());
        QVERIFY(send("account-permit"));
        QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_PROMPT"), output.constData(), 5000);
    }
    QVERIFY(send("touch-password"));
    QTRY_VERIFY2_WITH_TIMEOUT(([&] { receive("NATIVE_GREETER_FINISHED"); return !waylandServer()->isScreenLocked(); })(),
                             output.constData(), 5000);
    QTRY_VERIFY(!launcher->testIsRunning());
    QVERIFY(!waylandServer()->sessionLock()->isPresentationProtected());
}
void NativeSessionLockTest::nativeQtCrashPendingWorker()
{
    Test::setOutputConfig({Rect(0, 0, 1280, 1024), Rect(1280, 0, 1280, 1024)});
    auto *launcher = waylandServer()->lockerLauncher();
    launcher->setTestProgram(QStringLiteral(QINDAQT_PRIVATE_QT_LOCK_GREETER));
    struct Restore {
        LockerLauncher *launcher;
        ~Restore() { launcher->stopTestFixture(); launcher->setTestProgram({}); }
    } restore{launcher};
    QVERIFY(waylandServer()->sessionLock()->requestNativeLock());
    QByteArray output;
    auto receive = [&](const QByteArray &marker) {
        waylandServer()->display()->flush(); output += launcher->readTestOutput();
        return output.contains(marker);
    };
    QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_PROMPT"), output.constData(), 5000);
    QVERIFY(launcher->writeTestInput("cancel\n") == 7);
    QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_CANCELLED"), output.constData(), 5000);
    QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_IDLE"), output.constData(), 5000);
    output.clear();
    QVERIFY(launcher->writeTestInput("block-worker\n") == 13);
    QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_WORKER_BLOCKED="), output.constData(), 5000);
    const auto match = QRegularExpression(QStringLiteral("NATIVE_WORKER_BLOCKED=([0-9]+)")).match(QString::fromUtf8(output));
    QVERIFY(match.hasMatch());
    const auto pid = match.captured(1).toLongLong(); QVERIFY(pid > 1);
    auto alive = [&] {
        QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
        if (!stat.open(QIODevice::ReadOnly)) return false;
        const auto bytes = stat.readAll();
        const auto nameEnd = bytes.lastIndexOf(')');
        return nameEnd >= 0 && bytes.mid(nameEnd + 2, 1) != "Z";
    };
    QVERIFY(alive());
    launcher->stopTestFixture();
    // The module sleeps for a whole second; parent death must kill it before
    // that callback returns. Never signal a PID observed outside this fixture.
    QTRY_VERIFY_WITH_TIMEOUT(!alive(), 150);
    QTRY_VERIFY(!waylandServer()->sessionLock()->client());
    QTRY_VERIFY(waylandServer()->sessionLock()->isPresentationProtected());
    QVERIFY(waylandServer()->isScreenLocked());
}

}
