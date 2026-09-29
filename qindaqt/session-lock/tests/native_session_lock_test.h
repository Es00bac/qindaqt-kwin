// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
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
};
}
