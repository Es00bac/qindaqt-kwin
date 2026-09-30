// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDBusMessage>
#include <QObject>
#include <QString>
namespace KWin
{
class NativeSessionLockTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void authorization();
    void presentationFenceAndCrashRecovery();
    void explicitSurfaceRole();
    void protocolErrors_data();
    void protocolErrors();
    void inputIsolation();
    void inputMethodIsolation();
    void mirroredPresentation();
    void lockedHotplug();
    void captureRequestsBlocked();
    void captureIsolation();
    void blackFallbackPixels();
    void effectsGrabIsolation();
    void launcherPolicy();
    void launcherEnvironment();
    void launcherConnectionIsolation();
    void nativeLockBackendProtection();
    void captureLockReceipt(const QString &nonce, bool admitted, const QDBusMessage &message);
    void captureStateReceipt(const QString &nonce, bool locked, bool protectedPresentation, const QDBusMessage &message);
#if defined(QINDAQT_PRIVATE_QT_LOCK_PROBE)
    void nativeQtSurfaces_data();
    void nativeQtSurfaces();
#endif
#if defined(QINDAQT_PRIVATE_QT_LOCK_GREETER)
    void nativeQtAuthentication_data();
    void nativeQtAuthentication();
    void nativeQtCrashPendingWorker();
#if defined(QINDAQT_PRIVATE_GATHERED_PLUGIN)
    void nativeGatheredFence();
#endif
#endif
private:
    QString m_receiptNonce;
    bool m_receiptAdmitted = false;
    bool m_receiptLocked = false;
    bool m_receiptProtected = false;
    QDBusMessage m_receiptMessage;
};
}
