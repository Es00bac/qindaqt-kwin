// SPDX-License-Identifier: GPL-2.0-or-later
#include "sessionlockcontroller.h"
#include "compositor.h"
#include "core/backendoutput.h"
#include "core/output.h"
#include "core/outputbackend.h"
#include "core/renderbackend.h"
#include "lockerlauncher.h"
#include "main.h"
#include "scene/workspacescene.h"
#include "sessionlock_v1.h"
#include "sessionlockwindow.h"
#include "wayland/clientconnection.h"
#include "wayland/output.h"
#include "wayland/seat.h"
#include "wayland/surface.h"
#include "wayland_server.h"
#include "workspace.h"
#include <unistd.h>
namespace KWin
{
class SessionLockPresentationFeedback : public PresentationFeedback
{
public:
    SessionLockPresentationFeedback(SessionLockController *controller, BackendOutput *output, quint64 generation)
        : controller(controller)
        , output(output)
        , generation(generation)
    {
    }
    void presented(std::chrono::nanoseconds, std::chrono::nanoseconds, PresentationMode) override
    {
        if (controller && output) {
            controller->framePresented(output, generation);
        }
    }
    QPointer<SessionLockController> controller;
    QPointer<BackendOutput> output;
    quint64 generation;
};
SessionLockController::SessionLockController(Display *display, LockerLauncher *launcher, QObject *parent)
    : QObject(parent)
    , m_launcher(launcher)
{
    auto manager = new SessionLockManagerV1Interface(display, this);
    connect(manager, &SessionLockManagerV1Interface::lockRequested, this, &SessionLockController::acquire);
    for (auto output : kwinApp()->outputBackend()->outputs()) {
        watchOutput(output);
    }
    connect(kwinApp()->outputBackend(), &OutputBackend::outputAdded, this, &SessionLockController::watchOutput);
    connect(kwinApp()->outputBackend(), &OutputBackend::outputRemoved, this, [this](BackendOutput *output) {
        m_pendingOutputs.remove(output);
        acknowledge();
    });
}
void SessionLockController::watchOutput(BackendOutput *output)
{
    auto changed = [this, output]() {
        if (!m_locked) {
            return;
        }
        if (output->isEnabled() && !output->isNonDesktop()) {
            m_pendingOutputs.insert(output);
            setPresentationProtected(false);
            Compositor::self()->scene()->addRepaintFull();
        } else {
            m_pendingOutputs.remove(output);
            acknowledge();
        }
    };
    connect(output, &BackendOutput::enabledChanged, this, changed);
    changed();
}
bool SessionLockController::isTrustedLocker(ClientConnection *client) const
{
#if defined(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
    if (client && m_testClients.contains(client)) {
        return true;
    }
#endif
    // AGENT-GUARD: a self-launched copy of the same root-owned executable is
    // untrusted. Only the compositor's exact private socketpair can bind or
    // unlock; environment/app IDs and the public display cannot confer trust.
    return m_launcher && m_launcher->ownsConnection(client) && client->userId() == geteuid()
        && client->securityContextAppId().isEmpty();
}
#if defined(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
void SessionLockController::authorizeTestClient(ClientConnection *client)
{
    m_testClients.insert(client);
    connect(client, &QObject::destroyed, this, [this, client]() {
        m_testClients.remove(client);
    });
}
#endif
ClientConnection *SessionLockController::client() const
{
    return m_lock ? m_lock->client() : nullptr;
}
bool SessionLockController::isLockSurface(SurfaceInterface *surface) const
{
    if (!m_locked || !m_lock || !m_lockAcknowledged || !surface || surface->client() != client()) {
        return false;
    }
    for (const auto &window : m_windows) {
        if (window && !window->isDeleted() && window->surface() == surface && window->readyForPainting()) {
            return true;
        }
    }
    return false;
}
void SessionLockController::acquire(SessionLockV1Interface *lock)
{
    if (m_lock || !isTrustedLocker(lock->client())) {
        lock->sendFinished();
        return;
    }
    if (!m_locked) {
        m_locked = true;
        Q_EMIT aboutToLock();
        waylandServer()->seat()->cancelDrag();
        Q_EMIT lockStateChanged();
    }
    m_lock = lock;
    m_lockAcknowledged = false;
    connect(lock, &SessionLockV1Interface::aboutToBeDestroyed, this, [this, lock]() {
        if (m_lock == lock) {
            lostLocker();
        }
    });
    connect(lock, &SessionLockV1Interface::unlockRequested, this, [this, lock]() {
        if (m_lock != lock || !isTrustedLocker(lock->client())) {
            return;
        }
        m_lock = nullptr;
        m_lockAcknowledged = false;
        ++m_generation;
        clearWindows();
        m_locked = false;
        setPresentationProtected(false);
        Q_EMIT lockStateChanged();
        Compositor::self()->scene()->addRepaintFull();
    });
    connect(lock, &SessionLockV1Interface::surfaceCreated, this, [this, lock](SessionLockSurfaceV1Interface *surface) {
        if (m_lock != lock || !surface->output() || surface->output()->isRemoved()) {
            return;
        }
        auto window = new SessionLockWindow(surface, surface->output()->handle(), this);
        m_windows.insert(surface, window);
        waylandServer()->registerSessionLockWindow(window);
        connect(surface, &QObject::destroyed, this, [this, surface]() {
            m_windows.remove(surface);
        });
    });
    requirePresentation();
}
void SessionLockController::clearWindows()
{
    const auto windows = std::exchange(m_windows, {});
    for (const auto &window : windows) {
        if (window) {
            window->destroyWindow();
        }
    }
}
void SessionLockController::lostLocker()
{
    // AGENT-GUARD: destroying the authenticated locker removes only its
    // graphics; the lock fence survives until a valid replacement unlocks.
    m_lock = nullptr;
    m_lockAcknowledged = false;
    setPresentationProtected(false);
    Q_EMIT lockStateChanged();
    clearWindows();
    requirePresentation();
}
bool SessionLockController::requestNativeLock()
{
    lockWithoutClient();
    return m_launcher && m_launcher->start();
}
void SessionLockController::lockWithoutClient()
{
    if (m_locked) {
        return;
    }
    m_locked = true;
    Q_EMIT aboutToLock();
    waylandServer()->seat()->cancelDrag();
    Q_EMIT lockStateChanged();
    requirePresentation();
}
void SessionLockController::requirePresentation()
{
    setPresentationProtected(false);
    ++m_generation;
    m_pendingOutputs.clear();
    for (auto output : kwinApp()->outputBackend()->outputs()) {
        if (output->isEnabled() && !output->isNonDesktop()) {
            m_pendingOutputs.insert(output);
        }
    }
    workspace()->requestDpmsState(Workspace::DpmsState::On);
    Compositor::self()->scene()->addRepaintFull();
    acknowledge();
}
void SessionLockController::trackFrame(BackendOutput *output, const std::shared_ptr<OutputFrame> &frame)
{
    if (m_locked && m_pendingOutputs.contains(output)) {
        frame->addFeedback(std::make_shared<SessionLockPresentationFeedback>(this, output, m_generation));
    }
}
void SessionLockController::framePresented(BackendOutput *output, quint64 generation)
{
    if (!m_locked || generation != m_generation) {
        return;
    }
    m_pendingOutputs.remove(output);
    acknowledge();
}
void SessionLockController::setPresentationProtected(bool value)
{
    if (m_presentationProtected != value) {
        m_presentationProtected = value;
        Q_EMIT presentationProtectionChanged(value);
    }
}
void SessionLockController::acknowledge()
{
    // PF8 needs the actual current-generation fence even without a greeter,
    // including black fallback after crash; protocol locked is a separate ack.
    setPresentationProtected(m_locked && m_pendingOutputs.empty());
    if (m_locked && m_lock && !m_lockAcknowledged && m_pendingOutputs.empty()) {
        m_lockAcknowledged = true;
        m_lock->sendLocked();
        // Lock graphics/input become eligible only after black presentation
        // established the protocol locked boundary on every physical output.
        Q_EMIT lockStateChanged();
    }
}
}
