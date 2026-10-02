// SPDX-License-Identifier: GPL-2.0-or-later
#include "displaypowerauthority.h"
#include "core/backendoutput.h"
#include "core/outputbackend.h"
#include "core/output.h"
#include "main.h"
#include "workspace.h"
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <unistd.h>
namespace KWin
{
namespace {
const QString Path = QStringLiteral("/org/qindaqt/KWin/DisplayPower");
const QString Interface = QStringLiteral("org.qindaqt.KWin.DisplayPower1");
const QString Session = QStringLiteral("org.qindaqt.Session1");
const QString Legacy = QStringLiteral("org.kde.Solid.PowerManagement"); // qindaqt-identity: keep
bool validId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{32}$"));
    return pattern.match(id).hasMatch();
}
}
DisplayPowerAuthority::DisplayPowerAuthority(Workspace *workspace)
    : QObject(workspace), m_workspace(workspace), m_bus(QDBusConnection::sessionBus()),
      m_owners(Session, m_bus, QDBusServiceWatcher::WatchForOwnerChange),
      m_leases(workspace->dpmsState() != Workspace::DpmsState::On)
{
    m_clock.start();
    do { m_epoch = QRandomGenerator::system()->generate64(); } while (m_epoch == 0);
    m_owners.addWatchedService(Legacy);
    connect(&m_owners, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] { refreshOwners(); });
    refreshOwners();
    completeInventory(&m_topology, false);
    connect(workspace, &Workspace::outputsChanged, this, &DisplayPowerAuthority::inventoryChanged);
    const auto watchOutput = [this](BackendOutput *output) {
        connect(output, &BackendOutput::capabilitiesChanged, this, &DisplayPowerAuthority::inventoryChanged);
    };
    for (auto *output : kwinApp()->outputBackend()->outputs()) watchOutput(output);
    connect(kwinApp()->outputBackend(), &OutputBackend::outputAdded, this, watchOutput);
    m_expiry.setInterval(250);
    connect(&m_expiry, &QTimer::timeout, this, [this] {
        refreshOwners();
        const bool before = m_leases.effectiveOff();
        const auto old = m_leases.ids();
        m_leases.expire(now());
        settle(before, old, QStringLiteral("expired"));
    });
    m_expiry.start();
    m_registered = m_bus.registerObject(Path, this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
}
DisplayPowerAuthority::~DisplayPowerAuthority()
{
    m_expiry.stop();
    revoke(QStringLiteral("shutdown"));
    if (m_registered) m_bus.unregisterObject(Path);
}
bool DisplayPowerAuthority::completeInventory(QByteArray *inventory, bool modes) const
{
    QJsonArray records;
    bool complete = true;
    for (const auto *output : kwinApp()->outputBackend()->outputs()) {
        if (!output->isEnabled() || output->isPlaceholder()) continue;
        const bool capable = output->capabilities().testFlag(BackendOutput::Capability::Dpms);
        QJsonObject record{{QStringLiteral("id"), output->uuid()},
            {QStringLiteral("name"), output->name()}, {QStringLiteral("dpms"), capable}};
        if (modes) record.insert(QStringLiteral("on"), output->dpmsMode() == BackendOutput::DpmsMode::On);
        bool located = false;
        for (const auto *logical : m_workspace->outputs()) {
            if (logical->backendOutput() != output) continue;
            const auto geometry = logical->geometry();
            record.insert(QStringLiteral("geometry"), QJsonArray{geometry.x(), geometry.y(), geometry.width(), geometry.height()});
            located = geometry.width() > 0 && geometry.height() > 0;
            break;
        }
        records.append(record);
        complete = complete && located;
        complete = complete && capable && !output->uuid().isEmpty();
        // AGENT-GUARD: upstream DPMS is global. A mixed unexplained physical
        // baseline has no safe all-On restore; refuse rather than guess it.
        if (!m_leases.effectiveOff() && output->dpmsMode() != BackendOutput::DpmsMode::On) complete = false;
    }
    if (inventory) *inventory = QJsonDocument(records).toJson(QJsonDocument::Compact);
    return complete && !records.isEmpty();
}
void DisplayPowerAuthority::inventoryChanged()
{
    QByteArray topology;
    completeInventory(&topology, false);
    if (topology != m_topology) {
        m_topology = topology;
        // Ordinary DPMS mode changes also emit outputsChanged upstream. Only
        // an actual identity/geometry/capability change withdraws composition.
        revoke(QStringLiteral("outputs-changed"));
    }
    Q_EMIT InventoryChanged();
}
void DisplayPowerAuthority::refreshOwners()
{
    QString owner;
    bool absent = false;
    if (auto *bus = m_bus.interface()) {
        const QDBusReply<QString> resolved = bus->serviceOwner(Session);
        if (resolved.isValid() && !resolved.value().isEmpty()) {
            const QDBusReply<uint> uid = bus->serviceUid(resolved.value());
            if (uid.isValid() && uid.value() == static_cast<uint>(::getuid())) owner = resolved.value();
        }
        const QDBusReply<bool> legacy = bus->isServiceRegistered(Legacy);
        absent = legacy.isValid() && !legacy.value();
    }
    if (owner != m_sessionOwner || (m_legacyAbsent && !absent)) {
        revoke(QStringLiteral("authority-lost"));
        Q_EMIT InventoryChanged();
    }
    m_sessionOwner = owner;
    m_legacyAbsent = absent;
}
bool DisplayPowerAuthority::authorized()
{
    refreshOwners();
    return calledFromDBus() && !m_sessionOwner.isEmpty() && message().service() == m_sessionOwner && m_legacyAbsent;
}
void DisplayPowerAuthority::RequestInventoryWithReceipt(const QString &nonce)
{
    if (!calledFromDBus() || !validId(nonce)) return;
    refreshOwners();
    QByteArray inventory;
    const bool complete = completeInventory(&inventory);
    auto receipt = QDBusMessage::createTargetedSignal(message().service(), Path, Interface, QStringLiteral("InventoryReceipt"));
    receipt << nonce << m_epoch << (m_registered && m_legacyAbsent && complete) << m_enabled << inventory;
    m_bus.send(receipt);
}
bool DisplayPowerAuthority::SetNativeAdmission(qulonglong epoch, bool enabled)
{
    if (!authorized() || epoch != m_epoch || (enabled && !completeInventory())) return false;
    if (!enabled) revoke(QStringLiteral("admission-withdrawn"));
    m_enabled = enabled;
    return true;
}
bool DisplayPowerAuthority::AcquireBlank(qulonglong epoch, const QString &requestId, qulonglong deadlineMs)
{
    if (!authorized() || epoch != m_epoch || !m_enabled || !validId(requestId) || !completeInventory()) return false;
    const bool before = m_leases.effectiveOff();
    const auto old = m_leases.ids();
    const qint64 current = now();
    if (deadlineMs <= static_cast<qulonglong>(current) || deadlineMs > static_cast<qulonglong>(current + 60000)) return false;
    const bool accepted = m_leases.acquire(m_sessionOwner + QLatin1Char('/') + requestId, current, static_cast<qint64>(deadlineMs) - current);
    settle(before, old, QStringLiteral("expired"));
    return accepted;
}
bool DisplayPowerAuthority::ReleaseBlank(qulonglong epoch, const QString &requestId)
{
    if (!authorized() || epoch != m_epoch || !validId(requestId)) return false;
    const bool before = m_leases.effectiveOff();
    const auto old = m_leases.ids();
    const bool released = m_leases.release(m_sessionOwner + QLatin1Char('/') + requestId, now());
    settle(before, old, QStringLiteral("released"));
    return released;
}
void DisplayPowerAuthority::settle(bool before, const QStringList &oldIds, const QString &reason)
{
    const auto current = m_leases.ids();
    for (const auto &id : oldIds) {
        if (current.contains(id)) continue;
        const auto separator = id.indexOf(QLatin1Char('/'));
        auto ended = QDBusMessage::createTargetedSignal(id.left(separator), Path, Interface, QStringLiteral("LeaseEnded"));
        ended << id.mid(separator + 1) << reason;
        m_bus.send(ended);
    }
    if (before == m_leases.effectiveOff()) return;
    m_internal = true;
    m_workspace->requestDpmsState(m_leases.effectiveOff() ? Workspace::DpmsState::Off : Workspace::DpmsState::On);
    m_internal = false;
}
void DisplayPowerAuthority::revoke(const QString &reason)
{
    m_enabled = false;
    const bool before = m_leases.effectiveOff();
    const auto old = m_leases.ids();
    m_leases.retireAll(now());
    settle(before, old, reason);
}
void DisplayPowerAuthority::externalRequest(bool off)
{
    if (m_internal) return;
    const auto old = m_leases.ids();
    m_leases.externalRequest(off, now());
    // The caller's existing Workspace route performs the physical transition.
    settle(m_leases.effectiveOff(), old, QStringLiteral("woken"));
}
qint64 DisplayPowerAuthority::now() const { return m_clock.msecsSinceReference() + m_clock.elapsed(); }
bool DisplayPowerAuthority::internalRequest() const { return m_internal; }
}
