// SPDX-License-Identifier: GPL-2.0-or-later
#include "effect/effecthandler.h"
#include "inputmethod.h"
#include "inputpanelv1window.h"
#include "native_lock_fixture.h"
#include "native_session_lock_test.h"
#include "wayland/seat.h"
#include <KWayland/Client/keyboard.h>
#include <KWayland/Client/pointer.h>
#include <KWayland/Client/seat.h>
#include <KWayland/Client/touch.h>
#include <linux/input.h>
namespace KWin
{
void NativeSessionLockTest::inputIsolation()
{
    auto ordinary = NativeClient::create(false, Test::AdditionalWaylandInterface::Seat);
    QVERIFY(ordinary && ordinary->connection->seat);
    auto seat = ordinary->connection->seat;
    QTRY_VERIFY(seat->hasKeyboard() && seat->hasPointer() && seat->hasTouch());
    std::unique_ptr<KWayland::Client::Keyboard> keyboard(seat->createKeyboard());
    std::unique_ptr<KWayland::Client::Pointer> pointer(seat->createPointer());
    std::unique_ptr<KWayland::Client::Touch> touch(seat->createTouch());
    Test::XdgToplevelWindow window(ordinary->connection.get());
    QVERIFY(window.show());
    workspace()->activateWindow(window.m_window, true);
    const auto position = window.m_window->frameGeometry().center();
    QSignalSpy keys(keyboard.get(), &KWayland::Client::Keyboard::keyChanged);
    QSignalSpy buttons(pointer.get(), &KWayland::Client::Pointer::buttonStateChanged);
    QSignalSpy touches(touch.get(), &KWayland::Client::Touch::sequenceStarted);
    QSignalSpy motions(touch.get(), &KWayland::Client::Touch::pointMoved);
    QSignalSpy removed(touch.get(), &KWayland::Client::Touch::pointRemoved);
    QSignalSpy canceled(touch.get(), &KWayland::Client::Touch::sequenceCanceled);
    uint time = 1;
    QVERIFY(ordinary->connection->sync());
    QTRY_COMPARE(keyboard->enteredSurface(), window.m_surface.get());
    Test::keyboardKeyPressed(KEY_A, time++);
    Test::keyboardKeyReleased(KEY_A, time++);
    QVERIFY(ordinary->connection->sync());
    QTRY_COMPARE(keys.count(), 2);
    Test::pointerMotion(position, time++);
    QVERIFY(ordinary->connection->sync());
    QTRY_COMPARE(pointer->enteredSurface(), window.m_surface.get());
    Test::pointerButtonPressed(BTN_LEFT, time++);
    Test::pointerButtonReleased(BTN_LEFT, time++);
    QVERIFY(ordinary->connection->sync());
    QTRY_COMPARE(buttons.count(), 2);
    Test::touchDown(1, position, time++);
    QVERIFY(ordinary->connection->sync());
    QTRY_COMPARE(touches.count(), 1);

    auto locker = NativeClient::create(true, Test::AdditionalWaylandInterface::Seat);
    QVERIFY(locker && locker->manager);
    NativeLock lock(locker->manager->lock());
    QTRY_VERIFY(lock.locked);
    QVERIFY(ordinary->connection->sync());
    QTRY_COMPARE(canceled.count(), 1);
    Test::touchMotion(1, position + QPointF(1, 1), time++);
    Test::touchUp(1, time++);
    Test::keyboardKeyPressed(KEY_B, time++);
    Test::keyboardKeyReleased(KEY_B, time++);
    Test::pointerMotion(position + QPointF(1, 1), time++);
    Test::pointerButtonPressed(BTN_LEFT, time++);
    Test::pointerButtonReleased(BTN_LEFT, time++);
    Test::touchDown(2, position, time++);
    Test::touchMotion(2, position + QPointF(1, 1), time++);
    Test::touchUp(2, time++);
    QVERIFY(ordinary->connection->sync());
    QCOMPARE(keys.count(), 2);
    QCOMPARE(buttons.count(), 2);
    QCOMPARE(touches.count(), 1);
    QCOMPARE(motions.count(), 0);
    QCOMPARE(removed.count(), 0);

    auto lockSeat = locker->connection->seat;
    QTRY_VERIFY(lockSeat->hasKeyboard() && lockSeat->hasPointer() && lockSeat->hasTouch());
    std::unique_ptr<KWayland::Client::Keyboard> lockKeyboard(lockSeat->createKeyboard());
    std::unique_ptr<KWayland::Client::Pointer> lockPointer(lockSeat->createPointer());
    std::unique_ptr<KWayland::Client::Touch> lockTouch(lockSeat->createTouch());
    QSignalSpy lockKeys(lockKeyboard.get(), &KWayland::Client::Keyboard::keyChanged);
    QSignalSpy lockButtons(lockPointer.get(), &KWayland::Client::Pointer::buttonStateChanged);
    QSignalSpy lockTouches(lockTouch.get(), &KWayland::Client::Touch::sequenceStarted);
    auto surface = Test::createSurface(locker->connection->compositor);
    NativeSurface role(lock.get_lock_surface(*surface, *locker->connection->outputs[0]));
    QTRY_VERIFY(role.serial);
    role.ack_configure(role.serial);
    auto lockWindow = Test::renderAndWaitForShown(locker->connection->shm, surface.get(), role.size, Qt::black);
    QVERIFY(lockWindow);
    const QPointF lockPosition = lockWindow->frameGeometry().center();
    QVERIFY(locker->connection->sync());
    QTRY_COMPARE(lockKeyboard->enteredSurface(), surface.get());
    Test::keyboardKeyPressed(KEY_C, time++);
    Test::keyboardKeyReleased(KEY_C, time++);
    QVERIFY(locker->connection->sync());
    QTRY_COMPARE(lockKeys.count(), 2);
    Test::pointerMotion(lockPosition, time++);
    QVERIFY(locker->connection->sync());
    QTRY_COMPARE(lockPointer->enteredSurface(), surface.get());
    Test::pointerButtonPressed(BTN_LEFT, time++);
    Test::pointerButtonReleased(BTN_LEFT, time++);
    QVERIFY(locker->connection->sync());
    QTRY_COMPARE(lockButtons.count(), 2);
    Test::touchDown(3, lockPosition, time++);
    Test::touchUp(3, time++);
    QVERIFY(locker->connection->sync());
    QTRY_COMPARE(lockTouches.count(), 1);
    QCOMPARE(keys.count(), 2);
    QCOMPARE(buttons.count(), 2);
    QCOMPARE(touches.count(), 1);
    lock.unlock();
    QTRY_VERIFY(!waylandServer()->isScreenLocked());
    workspace()->activateWindow(window.m_window, true);
    Test::keyboardKeyPressed(KEY_D, time++);
    Test::keyboardKeyReleased(KEY_D, time++);
    Test::pointerMotion(position, time++);
    Test::pointerButtonPressed(BTN_LEFT, time++);
    Test::pointerButtonReleased(BTN_LEFT, time++);
    Test::touchDown(4, position, time++);
    Test::touchUp(4, time++);
    QVERIFY(ordinary->connection->sync());
    QTRY_COMPARE(keys.count(), 4);
    QTRY_COMPARE(buttons.count(), 4);
    QTRY_COMPARE(touches.count(), 2);
}
void NativeSessionLockTest::inputMethodIsolation()
{
    auto locker = NativeClient::create(true);
    QVERIFY(locker && locker->manager);
    auto im = Test::Connection::setupFromSocket(waylandServer()->createInputMethodConnection(), Test::AdditionalWaylandInterface::InputMethodV1);
    QVERIFY(im && im->inputPanelV1);
    auto imSurface = Test::createSurface(im->compositor);
    QtWayland::zwp_input_panel_surface_v1 imRole(im->inputPanelV1->get_input_panel_surface(*imSurface));
    imRole.set_overlay_panel();
    auto panel = Test::renderAndWaitForShown(im->shm, imSurface.get(), QSize(300, 100), Qt::blue);
    QVERIFY(panel && panel->isInputMethod());
    NativeLock lock(locker->manager->lock());
    QTRY_VERIFY(lock.locked);
    QVERIFY(!waylandServer()->isLockScreenInputSurface(panel->surface()));
    QVERIFY(!panel->windowItem()->isVisible());
    auto surface = Test::createSurface(locker->connection->compositor);
    NativeSurface role(lock.get_lock_surface(*surface, *locker->connection->outputs[0]));
    QTRY_VERIFY(role.serial);
    role.ack_configure(role.serial);
    auto lockWindow = Test::renderAndWaitForShown(locker->connection->shm, surface.get(), role.size, Qt::black);
    QVERIFY(lockWindow);
    QTRY_COMPARE(waylandServer()->seat()->focusedKeyboardSurface(), lockWindow->surface());
    QVERIFY(waylandServer()->isLockScreenInputSurface(panel->surface()));
    QTRY_VERIFY(panel->windowItem()->isVisible());
    role.destroy();
    QTRY_VERIFY(!waylandServer()->seat()->focusedKeyboardSurface());
    QVERIFY(!waylandServer()->isLockScreenInputSurface(panel->surface()));
    QTRY_VERIFY(!panel->windowItem()->isVisible());
    wl_proxy_destroy(reinterpret_cast<wl_proxy *>(imRole.object()));
    imSurface.reset();
    lock.unlock();
    QTRY_VERIFY(!waylandServer()->isScreenLocked());
    im.reset();
    waylandServer()->destroyInputMethodConnection();
}
class LockGrabEffect : public Effect
{
public:
    int keys = 0;
    int pointers = 0;
    void grabbedKeyboardEvent(QKeyEvent *) override
    {
        ++keys;
    }
    void windowInputMouseEvent(QEvent *) override
    {
        ++pointers;
    }
};
void NativeSessionLockTest::effectsGrabIsolation()
{
    LockGrabEffect effect;
    QVERIFY(effects->grabKeyboard(&effect));
    effects->startMouseInterception(&effect, Qt::ArrowCursor);
    struct Release
    {
        LockGrabEffect *effect;
        ~Release()
        {
            effects->ungrabKeyboard();
            effects->stopMouseInterception(effect);
        }
    } release{&effect};
    uint time = 1;
    Test::keyboardKeyPressed(KEY_A, time++);
    Test::keyboardKeyReleased(KEY_A, time++);
    Test::pointerMotion(QPointF(100, 100), time++);
    QCOMPARE(effect.keys, 2);
    QVERIFY(effect.pointers > 0);
    const int pointers = effect.pointers;
    auto client = NativeClient::create(true);
    QVERIFY(client && client->manager);
    NativeLock lock(client->manager->lock());
    QTRY_VERIFY(lock.locked);
    Test::keyboardKeyPressed(KEY_B, time++);
    Test::keyboardKeyReleased(KEY_B, time++);
    Test::pointerMotion(QPointF(200, 200), time++);
    QCOMPARE(effect.keys, 2);
    QCOMPARE(effect.pointers, pointers);
    lock.unlock();
    QTRY_VERIFY(!waylandServer()->isScreenLocked());
}
}
