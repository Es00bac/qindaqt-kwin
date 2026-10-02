// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "blankleases.h"
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
namespace KWin
{
class Workspace;
// Owned by Workspace, compositor thread only. Raw requests remain functional;
// authenticated finite causes only add own provenance to that existing route.
class DisplayPowerAuthority final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.qindaqt.KWin.DisplayPower1")
public:
    explicit DisplayPowerAuthority(Workspace *workspace);
    ~DisplayPowerAuthority() override;
    void externalRequest(bool off);
    bool internalRequest() const;
public Q_SLOTS:
    void RequestInventoryWithReceipt(const QString &nonce);
    bool SetNativeAdmission(qulonglong epoch, bool enabled);
    bool AcquireBlank(qulonglong epoch, const QString &requestId, qulonglong deadlineMs);
    bool ReleaseBlank(qulonglong epoch, const QString &requestId);
Q_SIGNALS:
    void InventoryChanged();
    void InventoryReceipt(const QString &nonce, qulonglong epoch, bool available, bool admitted, const QByteArray &inventory);
    void LeaseEnded(const QString &requestId, const QString &reason);
private:
    qint64 now() const;
    bool authorized();
    bool completeInventory(QByteArray *inventory = nullptr, bool modes = true) const;
    void inventoryChanged();
    void refreshOwners();
    void settle(bool before, const QStringList &oldIds, const QString &reason);
    void revoke(const QString &reason);
    Workspace *m_workspace;
    QDBusConnection m_bus;
    QDBusServiceWatcher m_owners;
    QElapsedTimer m_clock;
    QTimer m_expiry;
    BlankLeases m_leases;
    QString m_sessionOwner;
    QByteArray m_topology;
    bool m_legacyAbsent = false;
    bool m_enabled = false;
    bool m_internal = false;
    bool m_registered = false;
    qulonglong m_epoch = 0;
};
}
