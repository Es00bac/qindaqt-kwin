// SPDX-License-Identifier: GPL-2.0-or-later
#include "nativelockdbusinterface.h"
#include "sessionlockcontroller.h"
#include <QDBusConnection>
#include <QRegularExpression>
namespace KWin
{
NativeLockDBusInterface::NativeLockDBusInterface(SessionLockController *controller)
    : QObject(controller)
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
    m_registered = QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/qindaqt/KWin/NativeLock"), this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals | QDBusConnection::ExportAllProperties);
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

namespace
{
bool validReceiptNonce(const QString &nonce)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{32}$"));
    return pattern.match(nonce).hasMatch();
}
}

void NativeLockDBusInterface::RequestLockWithReceipt(const QString &nonce)
{
    const QDBusMessage call = message();
    const QString caller = call.service();
    if (!validReceiptNonce(nonce) || caller.isEmpty() || !caller.startsWith(QLatin1Char(':'))) {
        return;
    }
    const bool admitted = m_controller->requestNativeLock();
    auto receipt = QDBusMessage::createTargetedSignal(
        caller, QStringLiteral("/org/qindaqt/KWin/NativeLock"),
        QStringLiteral("org.qindaqt.KWin.NativeLock1"),
        QStringLiteral("lockAdmissionReceipt"));
    receipt << nonce << admitted;
    QDBusConnection::sessionBus().send(receipt);
}

void NativeLockDBusInterface::RequestStateWithReceipt(const QString &nonce)
{
    const QDBusMessage call = message();
    const QString caller = call.service();
    if (!validReceiptNonce(nonce) || caller.isEmpty() || !caller.startsWith(QLatin1Char(':'))) {
        return;
    }
    auto receipt = QDBusMessage::createTargetedSignal(
        caller, QStringLiteral("/org/qindaqt/KWin/NativeLock"),
        QStringLiteral("org.qindaqt.KWin.NativeLock1"),
        QStringLiteral("stateReceipt"));
    receipt << nonce << isLocked() << isProtected();
    QDBusConnection::sessionBus().send(receipt);
}
}
