// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "kwin_export.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <memory>
namespace KWin
{
class ClientConnection;
class Display;
class LockerLauncher;
class LogicalOutput;
class BackendOutput;
class OutputFrame;
class SessionLockManagerV1Interface;
class SessionLockV1Interface;
class SessionLockSurfaceV1Interface;
class SessionLockWindow;
class SurfaceInterface;
// Main-thread compositor-owned authority. Protocol disconnect never clears the
// fence; only the current trusted lock object may request valid unlock.
class KWIN_EXPORT SessionLockController : public QObject
{
    Q_OBJECT
public:
    explicit SessionLockController(Display *display, LockerLauncher *launcher, QObject *parent = nullptr);
    bool isLocked() const
    {
        return m_locked;
    }
    bool isPresentationProtected() const
    {
        return m_presentationProtected;
    }
    ClientConnection *client() const;
    bool isTrustedLocker(ClientConnection *client) const;
    bool isLockSurface(SurfaceInterface *surface) const;
    void lockWithoutClient();
    // Request/recovery may fence and launch, but confers no unlock authority.
    bool requestNativeLock();
    // AGENT-CONTRACT: Compositor passes only frames composed AFTER the fence
    // became active; actual presentation, never a timeout, acknowledges locking.
    void trackFrame(BackendOutput *output, const std::shared_ptr<OutputFrame> &frame);
#if defined(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
    // Only compiled into explicitly uninstalled native test builds.
    void authorizeTestClient(ClientConnection *client);
#endif
Q_SIGNALS:
    void aboutToLock();
    void lockStateChanged();
    void presentationProtectionChanged(bool protectedPresentation);

private:
    void acquire(SessionLockV1Interface *lock);
    void lostLocker();
    void clearWindows();
    void requirePresentation();
    void framePresented(BackendOutput *output, quint64 generation);
    void acknowledge();
    void setPresentationProtected(bool value);
    void watchOutput(BackendOutput *output);
    friend class SessionLockPresentationFeedback;
    LockerLauncher *m_launcher; // Borrowed compositor-owned launch authority.
    QPointer<SessionLockV1Interface> m_lock;
    bool m_locked = false;
    bool m_lockAcknowledged = false;
    bool m_presentationProtected = false;
    quint64 m_generation = 0;
    QSet<BackendOutput *> m_pendingOutputs;
    QHash<SessionLockSurfaceV1Interface *, QPointer<SessionLockWindow>> m_windows;
#if defined(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION)
    QSet<ClientConnection *> m_testClients;
#endif
};
}
