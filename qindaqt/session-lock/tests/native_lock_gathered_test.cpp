// SPDX-License-Identifier: GPL-2.0-or-later
// AGENT-CONTRACT: load an exact consumer artifact only inside this non-installable
// private test. Public development input creates real gathered scene items;
// lock admission/authentication retain the owned connection and native PAM path.
#include "native_lock_fixture.h"
#include "native_session_lock_test.h"
#include "lockerlauncher.h"
#include "plugin.h"
#include "backends/virtual/virtual_egl_backend.h"
#include "compositor.h"
#include "opengl/eglbackend.h"
#include "opengl/glframebuffer.h"
#include "opengl/gltexture.h"
#include "outputscreencastsource.h"
#include "regionscreencastsource.h"
#include "windowscreencastsource.h"
#include "screenshot.h"
#include "cursor.h"
#include <QPluginLoader>
#include <QApplication>
#include <QWidget>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QEventLoop>
#include <QTimer>
#include <QRegularExpression>

namespace KWin {
namespace {
QJsonObject control(const QString &method, const QVariantList &arguments = {})
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.qindaqt.Compositor"),
        QStringLiteral("/org/qindaqt/Compositor"), QStringLiteral("org.qindaqt.Compositor1"), method);
    message.setArguments(arguments);
    QDBusPendingCallWatcher pending(QDBusConnection::sessionBus().asyncCall(message, 3000));
    QEventLoop loop;
    QObject::connect(&pending, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
    if (!pending.isFinished()) loop.exec();
    if (pending.isError() || pending.reply().arguments().isEmpty()) return {};
    return QJsonDocument::fromJson(pending.reply().arguments()[0].toByteArray()).object();
}
QJsonObject hybrid() { return control(QStringLiteral("Capabilities")).value(QStringLiteral("hybrid")).toObject(); }
QJsonObject key(const char *name, bool pressed)
{
    return {{"type", "key"}, {"key", name}, {"pressed", pressed}};
}
QJsonObject pointer(const QPointF &point)
{
    return {{"type", "pointer-absolute"}, {"x", point.x()}, {"y", point.y()}};
}
bool inject(const QJsonArray &events)
{
    const auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"events", events}}).toJson(QJsonDocument::Compact);
    return control(QStringLiteral("InjectTestInput"), {bytes}).value("status") == QStringLiteral("injected");
}
}
void NativeSessionLockTest::nativeGatheredFence()
{
    Test::setOutputConfig({Rect(0, 0, 1280, 1024)});
    qputenv("QINDAQT_DEVELOPMENT_CONTROL", "1");
    qputenv("QINDAQT_TEST_SCENARIO", "private-owned-lock-gathered-fence");
    struct Environment {
        ~Environment() { qunsetenv("QINDAQT_DEVELOPMENT_CONTROL"); qunsetenv("QINDAQT_TEST_SCENARIO");
            Test::setOutputConfig({Rect(0, 0, 1280, 1024), Rect(1280, 0, 1280, 1024)}); }
    } environment;
    QPluginLoader loader(QStringLiteral(QINDAQT_PRIVATE_GATHERED_PLUGIN));
    QCOMPARE(loader.metaData().value("IID").toString(), QStringLiteral(PluginFactory_iid));
    auto *factory = qobject_cast<PluginFactory *>(loader.instance());
    QVERIFY2(factory, qPrintable(loader.errorString()));
    auto plugin = factory->create();
    QVERIFY(plugin);
    QTRY_COMPARE(control(QStringLiteral("Windows")).value("status").toString(), QStringLiteral("ok"));
    auto ordinary = NativeClient::create(false);
    QVERIFY(ordinary);
    Test::XdgToplevelWindow left(ordinary->connection.get(), [](Test::XdgToplevel *top) { top->set_title("Lock group left"); });
    Test::XdgToplevelWindow right(ordinary->connection.get(), [](Test::XdgToplevel *top) { top->set_title("Lock group right"); });
    Test::XdgToplevelWindow icon(ordinary->connection.get(), [](Test::XdgToplevel *top) { top->set_title("Lock gathered icon"); });
    QVERIFY(left.show(QSize(240, 200), Qt::red));
    QVERIFY(right.show(QSize(240, 200), Qt::green));
    QVERIFY(icon.show(QSize(240, 200), Qt::blue));
    left.m_window->move(QPointF(50, 150));
    right.m_window->move(QPointF(330, 150));
    icon.m_window->move(QPointF(750, 250));
    // A real xdg client must honor resized container configurations.
    auto autoresize = [&](Test::XdgToplevelWindow &window, const QColor &colour) {
        auto size = std::make_shared<QSize>(240, 200);
        connect(window.m_toplevel.get(), &Test::XdgToplevel::configureRequested, window.m_toplevel.get(),
            [size](const QSize &next) { if (!next.isEmpty()) *size = next; });
        connect(window.m_toplevel->xdgSurface(), &Test::XdgSurface::configureRequested, window.m_toplevel.get(),
            [&, size, colour, top = window.m_toplevel.get(), surface = window.m_surface.get()](quint32 serial) {
                top->xdgSurface()->ack_configure(serial);
                Test::render(ordinary->connection->shm, surface, *size, colour);
            });
    };
    autoresize(left, Qt::red); autoresize(right, Qt::green); autoresize(icon, Qt::blue);
    auto inventory = [&] { return control(QStringLiteral("Windows")).value("windows").toArray(); };
    auto idFor = [&](const QString &title) {
        for (const auto &entry : inventory()) if (entry.toObject().value("title") == title) return entry.toObject().value("id").toString();
        return QString{};
    };
    QTRY_VERIFY(!idFor(QStringLiteral("Lock group left")).isEmpty());
    const auto start = right.m_window->frameGeometry().center();
    const auto target = left.m_window->frameGeometry();
    const auto end = QPointF(target.x() + target.width() * 0.92, target.y() + target.height() / 2);
    QVERIFY(inject({pointer(start), key("left-meta", true), key("left-shift", true)}));
    QVERIFY(inject({QJsonObject{{"type", "button"}, {"button", "left"}, {"pressed", true}}}));
    for (int step = 1; step <= 12; ++step) {
        const auto progress = step / 12.0;
        QVERIFY(inject({pointer(start + (end - start) * progress)}));
        QTest::qWait(20);
    }
    QVERIFY(inject({QJsonObject{{"type", "button"}, {"button", "left"}, {"pressed", false}},
        key("left-shift", false), key("left-meta", false)}));
    QTRY_COMPARE(hybrid().value("containerCount").toInt(), 1);
    // Meta+wheel is the public window roll-up intent even for undecorated clients.
    QVERIFY(inject({pointer(icon.m_window->frameGeometry().center()), key("left-meta", true),
        QJsonObject{{"type", "pointer-axis"}, {"axis", "vertical"}, {"delta", -15.0}}, key("left-meta", false)}));
    QTRY_COMPARE(hybrid().value("iconifiedWindows").toArray().size(), 1);
    QTest::qWait(250);
    QRectF row;
    for (const auto &entry : inventory()) {
        const auto item = entry.toObject();
        if (item.value("title") == QStringLiteral("Lock group left")) {
            const auto geometry = item.value("targetGeometry").toObject();
            row = QRectF(geometry.value("x").toDouble(), geometry.value("y").toDouble(),
                geometry.value("width").toDouble(), geometry.value("height").toDouble());
        }
    }
    QVERIFY(!row.isEmpty());
    const auto rowPoint = QPointF(row.x() + row.width(), row.y() - 14);
    QVERIFY(inject({pointer(rowPoint)}));
    QTest::qWait(50);
    QVERIFY(inject({QJsonObject{{"type", "button"}, {"button", "right"}, {"pressed", true}}}));
    QTest::qWait(50);
    QVERIFY(inject({QJsonObject{{"type", "button"}, {"button", "right"}, {"pressed", false}}}));
    QTRY_VERIFY(QApplication::activePopupWidget());
    // The compositor test QPA is offscreen. Deliver to the public QWidget
    // popup rather than inventing a production context-menu command seam.
    auto *menu = QApplication::activePopupWidget();
    for (int i = 0; i < 6; ++i) QTest::keyClick(menu, Qt::Key_Down);
    QTest::keyClick(menu, Qt::Key_Return);
    QTRY_COMPARE(hybrid().value("shadedStripFrames").toArray().size(), 1);
    const auto gathered = hybrid();
    qInfo() << "GATHER_BASELINE" << gathered;
    auto *backend = dynamic_cast<EglBackend *>(Compositor::self()->backend());
    QVERIFY(backend);
    auto frame = [&] {
        waylandServer()->display()->flush();
        if (!backend->openglContext()->makeCurrent()) return QImage{};
        const auto layers = backend->compatibleOutputLayers(kwinApp()->outputBackend()->outputs().first());
        if (layers.empty()) return QImage{};
        auto *layer = dynamic_cast<VirtualEglLayer *>(layers[0]);
        return layer && layer->texture() ? layer->texture()->toImage() : QImage{};
    };
    auto *launcher = waylandServer()->lockerLauncher();
    launcher->setTestProgram(QStringLiteral(QINDAQT_PRIVATE_QT_LOCK_GREETER));
    struct Restore { LockerLauncher *launcher; ~Restore() { launcher->stopTestFixture(); launcher->setTestProgram({}); } } restore{launcher};
    ScreenShotManager screenshot;
    OutputScreenCastSource output(workspace()->outputs()[0], {});
    WindowScreenCastSource window(icon.m_window);
    RegionScreenCastSource region(Rect(0, 0, 1280, 1024), 1, {});
    auto jsonRect = [](const QJsonObject &value) {
        return QRect(value.value("x").toInt(), value.value("y").toInt(),
            value.value("width").toInt(), value.value("height").toInt());
    };
    const auto chip = jsonRect(gathered.value("iconifiedWindows").toArray().first().toObject().value("chipFrame").toObject());
    const auto strip = jsonRect(gathered.value("shadedStripFrames").toArray().first().toObject());
    QVERIFY(!chip.isEmpty() && !strip.isEmpty());
    auto ink = [](const QImage &pixels, const QRect &rect) {
        int count = 0;
        for (int y=rect.top(); y<=rect.bottom(); ++y) for(int x=rect.left(); x<=rect.right(); ++x)
            if (pixels.rect().contains(x,y) && pixels.pixelColor(x,y) != QColor(Qt::black)) ++count;
        return count;
    };
    auto gatheredPainted = [&] { const auto pixels = frame(); return !pixels.isNull()
        && ink(pixels, chip) > 20 && ink(pixels, strip) > 20; };
    QTRY_VERIFY(gatheredPainted());
    const auto before = frame();
    const QRect caption(chip.right()+8, chip.center().y()-16, 232, 32);
    QVERIFY(frame().rect().contains(caption));
    QCOMPARE(ink(before,caption),0);
    QVERIFY(inject({pointer(chip.center())}));
    auto captionPainted = [&] {
        const auto pixels = frame(); if (pixels.isNull() || ink(pixels,caption)<100) return false;
        // Probe the interior text band, away from the bubble's bright border.
        int textPixels=0;
        const auto interior=caption.adjusted(8,8,-8,-8);
        for (int y=interior.top();y<=interior.bottom();++y) for(int x=interior.left();x<=interior.right();++x) {
            const auto colour=pixels.pixelColor(x,y);
            if (colour.red()>180&&colour.green()>180&&colour.blue()>180) ++textPixels;
        }
        return textPixels>15;
    };
    QTRY_VERIFY(captionPainted());
    QVERIFY(frame().save(QStringLiteral(QINDAQT_PRIVATE_QT_LOCK_GREETER) + QStringLiteral("-gathered-hover-before.png")));
    QVERIFY(before.save(QStringLiteral(QINDAQT_PRIVATE_QT_LOCK_GREETER) + QStringLiteral("-gathered-before.png")));
    // Hover leaves the allowed native cursor inside the chip probe. Hide only
    // that cursor so uniform-background assertions measure desktop chrome.
    Cursors::self()->hideCursor();
    struct Cursor { ~Cursor() { Cursors::self()->showCursor(); } } cursor;
    QVERIFY(waylandServer()->sessionLock()->requestNativeLock());
    QByteArray messages;
    auto receive = [&](const QByteArray &marker) { waylandServer()->display()->flush(); messages += launcher->readTestOutput(); return messages.contains(marker); };
    QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_UI_READY"), messages.constData(), 10000);
    QTRY_VERIFY2_WITH_TIMEOUT(([&] {
        receive("LAYOUT=");
        return QRegularExpression(QStringLiteral("LAYOUT=([^\\r\\n]+)")).match(QString::fromUtf8(messages)).hasMatch();
    })(), messages.constData(), 5000);
    const auto layout = QRegularExpression(QStringLiteral("LAYOUT=([^\\r\\n]+)")).match(QString::fromUtf8(messages)).captured(1);
    qInfo() << "NATIVE_STANDARD_LAYOUT" << layout;
    // UI_READY acknowledges the client's rendered buffer, not that the
    // compositor has sampled/presented that next buffer yet.
    QSignalSpy uiPresented(kwinApp()->outputBackend()->outputs().first()->renderLoop(), &RenderLoop::framePresented);
    QTRY_VERIFY(!uiPresented.isEmpty());
    QVERIFY(waylandServer()->isScreenLocked());
    QVERIFY(waylandServer()->sessionLock()->isPresentationProtected());
    auto greeterCoversGathered = [&] {
        const auto pixels = frame(); if (pixels.isNull()) return false;
        const auto background = pixels.pixelColor(1,1);
        if (background == QColor(Qt::black)) return false;
        for (const auto &rect : {chip, strip, caption})
            for (int y=rect.top();y<=rect.bottom();++y) for(int x=rect.left();x<=rect.right();++x)
                if (pixels.pixelColor(x,y) != background) return false;
        return true;
    };
    QTRY_VERIFY(greeterCoversGathered());
    QVERIFY(frame().save(QStringLiteral(QINDAQT_PRIVATE_QT_LOCK_GREETER) + QStringLiteral("-gathered-locked.png")));
    QVERIFY(!screenshot.takeScreenShot(icon.m_window));
    QVERIFY(!screenshot.takeScreenShot(workspace()->outputs()[0], {}, {}));
    QVERIFY(!screenshot.takeScreenShot(Rect(0, 0, 1280, 1024), {}, {}));
    for (ScreenCastSource *source : std::array<ScreenCastSource *, 3>{&window, &output, &region}) {
        QImage pixels(QSize(32, 32), QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::magenta);
        QVERIFY(!source->render(&pixels, Region::infinite()).isEmpty());
        for (int y=0;y<pixels.height();++y) for (int x=0;x<pixels.width();++x) QCOMPARE(pixels.pixelColor(x,y), QColor(Qt::black));
        auto texture = GLTexture::allocate(GL_RGBA8, QSize(32, 32)); QVERIFY(texture);
        GLFramebuffer framebuffer(texture.get()); QVERIFY(framebuffer.valid());
        QVERIFY(!source->render(&framebuffer, Region::infinite()).isEmpty());
        const auto glPixels = texture->toImage(); QVERIFY(!glPixels.isNull());
        for (int y=0;y<glPixels.height();++y) for (int x=0;x<glPixels.width();++x) QCOMPARE(glPixels.pixelColor(x,y), QColor(Qt::black));
    }
    // Actual output after owned greeter crash must contain neither ordinary
    // client pixels nor gathered chrome. Geometry inventory may remain cached.
    launcher->stopTestFixture();
    QTRY_VERIFY(!waylandServer()->sessionLock()->client());
    QTRY_VERIFY(waylandServer()->sessionLock()->isPresentationProtected());
    auto allBlack = [&] { const auto image = frame(); if (image.isNull()) return false;
        for (int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) if(image.pixelColor(x,y) != QColor(Qt::black)) return false;
        return true; };
    QTRY_VERIFY(allBlack());
    QVERIFY(waylandServer()->isScreenLocked());
    QVERIFY(frame().save(QStringLiteral(QINDAQT_PRIVATE_QT_LOCK_GREETER) + QStringLiteral("-gathered-crash-black.png")));
    messages.clear();
    QVERIFY(waylandServer()->sessionLock()->requestNativeLock());
    QTRY_VERIFY2_WITH_TIMEOUT(receive("NATIVE_GREETER_UI_READY"), messages.constData(), 10000);
    QVERIFY(launcher->writeTestInput("touch-password\n") == 15);
    QTRY_VERIFY_WITH_TIMEOUT(!waylandServer()->isScreenLocked(), 5000);
    QTRY_VERIFY(([&] { receive("NATIVE_GREETER_FINISHED"); return !launcher->testIsRunning(); })());
    QTRY_COMPARE(hybrid().value("iconifiedWindows").toArray().size(), 1);
    QTRY_COMPARE(hybrid().value("shadedStripFrames").toArray().size(), 1);
    QTRY_VERIFY(gatheredPainted());
    QVERIFY(inject({pointer(chip.center())}));
    QTRY_VERIFY(captionPainted());
    QVERIFY(frame().save(QStringLiteral(QINDAQT_PRIVATE_QT_LOCK_GREETER) + QStringLiteral("-gathered-restored.png")));
}
}
