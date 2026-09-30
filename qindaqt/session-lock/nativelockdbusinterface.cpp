// SPDX-License-Identifier: GPL-2.0-or-later
#include "nativelockdbusinterface.h"
#include "sessionlockcontroller.h"
#include <QDBusConnection>
namespace KWin
{
NativeLockDBusInterface::NativeLockDBusInterface(SessionLockController *controller)
    : QDBusAbstractAdaptor(controller)
    , m_controller(controller)
{
    m_reportedLocked = isLocked();
    connect(controller, &SessionLockController::lockStateChanged, this, [this] {
        const bool locked = isLocked();
        if (m_reportedLocked != locked) {
            m_reportedLocked = locked;
            Q_EMIT lockedChanged(locked);
        }
    });
    connect(controller, &SessionLockController::presentationProtectionChanged, this, &NativeLockDBusInterface::protectedChanged);
    m_registered = QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/qindaqt/KWin/NativeLock"), m_controller, QDBusConnection::ExportAdaptors);
}
NativeLockDBusInterface::~NativeLockDBusInterface()
{
    if (m_registered) {
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/org/qindaqt/KWin/NativeLock"));
    }
}
bool NativeLockDBusInterface::isLocked() const
{
    return m_controller->isLocked();
}
bool NativeLockDBusInterface::isProtected() const
{
    return m_controller->isPresentationProtected();
}
bool NativeLockDBusInterface::RequestLock()
{
    return m_controller->requestNativeLock();
}
}
