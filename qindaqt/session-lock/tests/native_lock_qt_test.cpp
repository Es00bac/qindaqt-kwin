// SPDX-License-Identifier: GPL-2.0-or-later
// External Qt role adapter runs only on this private, non-installable server.
#include "backends/virtual/virtual_egl_backend.h"
#include "backends/virtual/virtual_backend.h"
#include "compositor.h"
#include "core/output.h"
#include "core/outputconfiguration.h"
#include "cursor.h"
#include "lockerlauncher.h"
#include "native_lock_fixture.h"
#include "native_session_lock_test.h"
#include "opengl/eglbackend.h"
#include "opengl/gltexture.h"
namespace KWin {
void NativeSessionLockTest::nativeQtSurfaces_data()
{
    QTest::addColumn<double>("scale");
    QTest::newRow("normal") << 1.0;
    QTest::newRow("150-percent") << 1.5;
}
void NativeSessionLockTest::nativeQtSurfaces()
{
    QFETCH(double, scale);
    Test::setOutputConfig({Rect(0, 0, 1280, 1024), Rect(1280, 0, 1280, 1024)});
    OutputConfiguration configuration;
    for (auto *output : kwinApp()->outputBackend()->outputs()) configuration.changeSet(output)->scale = scale;
    QCOMPARE(workspace()->applyOutputConfiguration(configuration), OutputConfigurationError::None);
    auto *launcher = waylandServer()->lockerLauncher();
    launcher->setTestProgram(QStringLiteral(QINDAQT_PRIVATE_QT_LOCK_PROBE));
    Cursors::self()->hideCursor();
    struct Restore {
        LockerLauncher *launcher;
        ~Restore() {
            launcher->stopTestFixture(); launcher->setTestProgram({});
            Cursors::self()->showCursor();
            Test::setOutputConfig({Rect(0, 0, 1280, 1024), Rect(1280, 0, 1280, 1024)});
        }
    } restore{launcher};
    QVERIFY(waylandServer()->sessionLock()->requestNativeLock());
    QByteArray output;
    QTRY_VERIFY2_WITH_TIMEOUT(([&] {
        waylandServer()->display()->flush(); output += launcher->readTestOutput();
        return output.contains("NATIVE_PROTOCOL_LOCKED") && output.count("NATIVE_ROLE_PAINTED") >= 2;
    })(), output.constData(), 5000);
    QVERIFY(launcher->testIsRunning());
    QVERIFY(waylandServer()->sessionLock()->isPresentationProtected());
    auto count = [] {
        waylandServer()->display()->flush();
        int count = 0;
        for (auto *window : workspace()->windows()) {
            if (window->isLockScreen() && window->windowItem()->isVisible() &&
                waylandServer()->isLockScreenInputSurface(window->surface())) ++count;
        }
        return count;
    };
    QTRY_COMPARE(count(), 2);
    auto *backend = dynamic_cast<EglBackend *>(Compositor::self()->backend());
    QVERIFY(backend);
    auto painted = [backend] {
        if (!backend->openglContext()->makeCurrent()) return false;
        for (auto *output : kwinApp()->outputBackend()->outputs()) {
            const auto layers = backend->compatibleOutputLayers(output);
            if (layers.empty()) return false;
            auto *layer = dynamic_cast<VirtualEglLayer *>(layers[0]);
            if (!layer || !layer->texture()) return false;
            const auto image = layer->texture()->toImage();
            if (image.isNull() || image.pixelColor(image.width()/2, image.height()/2) != QColor("#345678")) return false;
        }
        return true;
    };
    QTRY_VERIFY(painted());
    if (scale == 1.0) {
        auto *virtualBackend = static_cast<VirtualBackend *>(kwinApp()->outputBackend());
        VirtualBackend::OutputInfo info; info.geometry = Rect(2560, 0, 800, 600);
        auto *added = virtualBackend->addOutput(info);
        QVERIFY(added);
        QTRY_VERIFY2_WITH_TIMEOUT(([&] { output += launcher->readTestOutput(); return count() == 3; })(), output.constData(), 5000);
        QTRY_VERIFY(painted());
        virtualBackend->removeVirtualOutput(added);
        QTRY_COMPARE(count(), 2);
        QVERIFY(launcher->testIsRunning());
    }
    launcher->stopTestFixture();
    QTRY_VERIFY(!waylandServer()->sessionLock()->client());
    QTRY_VERIFY(waylandServer()->sessionLock()->isPresentationProtected());
    QVERIFY(waylandServer()->isScreenLocked());
}
}
