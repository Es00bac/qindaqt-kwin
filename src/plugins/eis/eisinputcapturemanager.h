/*
    SPDX-FileCopyrightText: 2024 David Redondo <kde@david-redondo>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "utils/ramfile.h"

#include <QDBusContext>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QObject>
#include <QPointF>
#include <QRect>

#include <memory>

class QAction;
class QDBusServiceWatcher;

namespace KWin
{
class BarrierSpy;
class EisInputCapture;
class EisInputCaptureFilter;

struct EisInputCaptureBarrier
{
    const Qt::Orientation orientation;
    const int position;
    const int start;
    const int end;
    bool hitTest(const QPointF &point) const;
};

class EisInputCaptureManager : public QObject, public QDBusContext
{
    Q_OBJECT
public:
    EisInputCaptureManager();
    ~EisInputCaptureManager();

    Q_INVOKABLE QDBusObjectPath addInputCapture(uint capabilities);
    Q_INVOKABLE void removeInputCapture(const QDBusObjectPath &capture);
    // AGENT-CONTRACT: compositor-authoritative logical output rectangles used
    // by the QindaQt portal InputCapture GetZones/barrier validation; the same
    // coordinates BarrierSpy enforces. zonesChanged follows layout changes.
    Q_INVOKABLE QList<QRect> zones();

    const RamFile &keyMap() const;

    void barrierHit(EisInputCapture *capture, const QPointF &position);
    EisInputCapture *activeCapture();
    void deactivate(const std::optional<QPointF> &releasePosition = std::nullopt);

Q_SIGNALS:
    void zonesChanged();

private:
    RamFile m_keymapFile;
    QDBusServiceWatcher *m_serviceWatcher;
    std::unique_ptr<BarrierSpy> m_barrierSpy;
    std::unique_ptr<EisInputCaptureFilter> m_inputFilter;
    std::vector<std::unique_ptr<EisInputCapture>> m_inputCaptures;
    EisInputCapture *m_activeCapture = nullptr;
    QAction *m_disableCaptureAction;
    friend class BarrierSpy;
};
}
