// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDBusAbstractAdaptor>
namespace KWin
{
class SessionLockController;
// Thin compositor-thread backend seam for PF8. The controller is borrowed and
// must outlive this adapter. The user session bus grants only lock/recovery
// requests; it cannot authenticate or unlock. Protected is physical feedback,
// never process launch or receipt of an authentication-related message.
class NativeLockDBusInterface final : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.qindaqt.KWin.NativeLock1")
    Q_PROPERTY(bool Locked READ isLocked NOTIFY lockedChanged)
    Q_PROPERTY(bool Protected READ isProtected NOTIFY protectedChanged)
public:
    explicit NativeLockDBusInterface(SessionLockController *controller);
    ~NativeLockDBusInterface() override;
    bool isLocked() const;
    bool isProtected() const;
public Q_SLOTS:
    bool RequestLock();
Q_SIGNALS:
    void lockedChanged(bool locked);
    void protectedChanged(bool protectedPresentation);

private:
    SessionLockController *m_controller;
    bool m_registered = false;
    bool m_reportedLocked = false;
};
}
