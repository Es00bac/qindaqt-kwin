// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/output.h"
#include "core/outputconfiguration.h"
#include "native_lock_fixture.h"
#include "native_session_lock_test.h"
namespace KWin
{
void NativeSessionLockTest::mirroredPresentation()
{
    Test::setOutputConfig({Rect(0, 0, 1280, 1024), Rect(1280, 0, 1280, 1024)});
    const auto outputs = kwinApp()->outputBackend()->outputs();
    QCOMPARE(outputs.size(), 2);
    OutputConfiguration config;
    config.changeSet(outputs[1])->replicationSource = outputs[0]->uuid();
    QCOMPARE(workspace()->applyOutputConfiguration(config), OutputConfigurationError::None);
    QCOMPARE(workspace()->outputs().size(), 1);
    auto client = NativeClient::create(true);
    QVERIFY(client && client->manager);
    auto loop = outputs[1]->renderLoop();
    loop->inhibit();
    struct Resume
    {
        RenderLoop *loop;
        ~Resume()
        {
            if (loop) {
                loop->uninhibit();
            }
        }
    } resume{loop};
    NativeLock lock(client->manager->lock());
    QTRY_VERIFY(waylandServer()->isScreenLocked());
    QTest::qWait(1200);
    const bool premature = lock.locked;
    loop->uninhibit();
    resume.loop = nullptr;
    QVERIFY(!premature);
    QTRY_VERIFY(lock.locked);
    lock.unlock();
    QTRY_VERIFY(!waylandServer()->isScreenLocked());
    Test::setOutputConfig({Rect(0, 0, 1280, 1024), Rect(1280, 0, 1280, 1024)});
}
void NativeSessionLockTest::lockedHotplug()
{
    auto client = NativeClient::create(true);
    QVERIFY(client && client->manager);
    Test::XdgToplevelWindow ordinary(client->connection.get());
    QVERIFY(ordinary.show(QSize(200, 200), Qt::red));
    NativeLock lock(client->manager->lock());
    QTRY_VERIFY(lock.locked);
    QVERIFY(!ordinary.m_window->windowItem()->isVisible());
    Test::setOutputConfig({Rect(0, 0, 800, 600), Rect(800, 0, 800, 600), Rect(1600, 0, 800, 600)});
    QCOMPARE(kwinApp()->outputBackend()->outputs().size(), 3);
    QVERIFY(waylandServer()->isScreenLocked());
    QVERIFY(!ordinary.m_window->windowItem()->isVisible());
    const auto outputs = kwinApp()->outputBackend()->outputs();
    std::vector<std::unique_ptr<QSignalSpy>> presented;
    for (auto output : outputs) {
        presented.push_back(std::make_unique<QSignalSpy>(output->renderLoop(), &RenderLoop::framePresented));
        output->renderLoop()->scheduleRepaint();
    }
    for (const auto &spy : presented) {
        QTRY_VERIFY(!spy->isEmpty());
    }
    QVERIFY(waylandServer()->isScreenLocked());
    lock.unlock();
    QTRY_VERIFY(!waylandServer()->isScreenLocked());
    Test::setOutputConfig({Rect(0, 0, 1280, 1024), Rect(1280, 0, 1280, 1024)});
}
}
