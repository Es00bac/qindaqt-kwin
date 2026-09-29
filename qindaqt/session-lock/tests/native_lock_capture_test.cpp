// SPDX-License-Identifier: GPL-2.0-or-later
#include "backends/virtual/virtual_egl_backend.h"
#include "compositor.h"
#include "core/output.h"
#include "cursor.h"
#include "native_lock_fixture.h"
#include "native_session_lock_test.h"
#include "opengl/eglbackend.h"
#include "opengl/glframebuffer.h"
#include "opengl/gltexture.h"
#include "outputscreencastsource.h"
#include "regionscreencastsource.h"
#include "screenshot.h"
#include "windowscreencastsource.h"
namespace KWin
{
void NativeSessionLockTest::captureIsolation()
{
    auto client = NativeClient::create(true);
    QVERIFY(client && client->manager);
    Test::XdgToplevelWindow ordinary(client->connection.get());
    QVERIFY(ordinary.show(QSize(200, 200), Qt::red));
    auto backend = dynamic_cast<EglBackend *>(Compositor::self()->backend());
    QVERIFY(backend && backend->openglContext()->makeCurrent());
    ScreenShotManager screenshot;
    const auto baseline = screenshot.takeScreenShot(ordinary.m_window);
    QVERIFY(baseline && !baseline->isNull());
    QCOMPARE(baseline->pixelColor(baseline->width() / 2, baseline->height() / 2), QColor(Qt::red));
    WindowScreenCastSource window(ordinary.m_window);
    OutputScreenCastSource output(workspace()->outputs()[0], {});
    RegionScreenCastSource region(Rect(0, 0, 200, 200), 1, {});
    QImage image(QSize(200, 200), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::magenta);
    QVERIFY(!window.render(&image, Region::infinite()).isEmpty());
    QCOMPARE(image.pixelColor(100, 100), QColor(Qt::red));
    NativeLock lock(client->manager->lock());
    QTRY_VERIFY(lock.locked);
    QVERIFY(!screenshot.takeScreenShot(ordinary.m_window));
    QVERIFY(!screenshot.takeScreenShot(workspace()->outputs()[0], {}, {}));
    QVERIFY(!screenshot.takeScreenShot(Rect(0, 0, 200, 200), {}, {}));
    // All production source types are exercised with stale colored contents.
    // There is no PipeWire daemon, stream mock or physical capture involved.
    for (ScreenCastSource *source : std::array<ScreenCastSource *, 3>{&window, &output, &region}) {
        image.fill(Qt::magenta);
        QVERIFY(!source->render(&image, Region::infinite()).isEmpty());
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                QCOMPARE(image.pixelColor(x, y), QColor(Qt::black));
            }
        }
        auto texture = GLTexture::allocate(GL_RGBA8, QSize(32, 32));
        QVERIFY(texture);
        GLFramebuffer framebuffer(texture.get());
        QVERIFY(framebuffer.valid());
        GLFramebuffer::pushFramebuffer(&framebuffer);
        glClearColor(1, 0, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 1, 1);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        GLFramebuffer::popFramebuffer();
        QVERIFY(!source->render(&framebuffer, Region::infinite()).isEmpty());
        const QImage pixels = texture->toImage();
        QVERIFY(!pixels.isNull());
        for (int y = 0; y < pixels.height(); ++y) {
            for (int x = 0; x < pixels.width(); ++x) {
                QCOMPARE(pixels.pixelColor(x, y), QColor(Qt::black));
            }
        }
        QVERIFY(glIsEnabled(GL_SCISSOR_TEST));
        GLboolean mask[4];
        glGetBooleanv(GL_COLOR_WRITEMASK, mask);
        for (auto value : mask) {
            QCOMPARE(value, GLboolean(GL_FALSE));
        }
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    lock.unlock();
    QTRY_VERIFY(!waylandServer()->isScreenLocked());
    QVERIFY(screenshot.takeScreenShot(ordinary.m_window));
}
void NativeSessionLockTest::blackFallbackPixels()
{
    Cursors::self()->hideCursor();
    struct RestoreCursor
    {
        ~RestoreCursor()
        {
            Cursors::self()->showCursor();
        }
    } restore;
    auto client = NativeClient::create(true);
    QVERIFY(client && client->manager);
    auto ordinaryClient = NativeClient::create(false);
    QVERIFY(ordinaryClient);
    Test::XdgToplevelWindow ordinary(ordinaryClient->connection.get());
    QVERIFY(ordinary.show(QSize(200, 200), Qt::red));
    auto backend = dynamic_cast<EglBackend *>(Compositor::self()->backend());
    QVERIFY(backend);
    auto lock = std::make_unique<NativeLock>(client->manager->lock());
    QTRY_VERIFY(lock->locked);
    auto checkBlack = [backend]() {
        QVERIFY(backend->openglContext()->makeCurrent());
        for (auto output : kwinApp()->outputBackend()->outputs()) {
            const auto layers = backend->compatibleOutputLayers(output);
            QVERIFY(!layers.empty());
            auto layer = dynamic_cast<VirtualEglLayer *>(layers[0]);
            QVERIFY(layer && layer->texture());
            const QImage pixels = layer->texture()->toImage();
            QVERIFY(!pixels.isNull());
            for (int y = 0; y < pixels.height(); ++y) {
                for (int x = 0; x < pixels.width(); ++x) {
                    QCOMPARE(pixels.pixelColor(x, y), QColor(Qt::black));
                }
            }
        }
    };
    checkBlack();
    auto surface = Test::createSurface(client->connection->compositor);
    auto role = std::make_unique<NativeSurface>(lock->get_lock_surface(*surface, *client->connection->outputs[0]));
    QTRY_VERIFY(role->serial);
    role->ack_configure(role->serial);
    QVERIFY(Test::renderAndWaitForShown(client->connection->shm, surface.get(), role->size, Qt::green));
    const auto outputs = kwinApp()->outputBackend()->outputs();
    std::vector<std::unique_ptr<QSignalSpy>> presented;
    for (auto output : outputs) {
        presented.push_back(std::make_unique<QSignalSpy>(output->renderLoop(), &RenderLoop::framePresented));
    }
    // Destroy graphics and connection without sending unlock. The ordinary
    // red window survives, so this checks actual fallback composition.
    role.reset();
    surface.reset();
    lock.reset();
    client.reset();
    QTRY_VERIFY(!waylandServer()->sessionLock()->client());
    QVERIFY(waylandServer()->isScreenLocked());
    for (const auto &spy : presented) {
        QTRY_VERIFY(!spy->isEmpty());
    }
    checkBlack();
}
}
