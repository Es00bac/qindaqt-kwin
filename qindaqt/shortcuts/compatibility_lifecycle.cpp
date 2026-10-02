/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compatibility_endpoint.h"
#include "runtime_store.h"
#include "desktop_actions.h"
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <unistd.h>

namespace KWin::Shortcuts
{
std::function<void()> CompatibilityEndpoint::snapshotRestore()
{
    const auto registry = *m_registry;
    const auto actions = m_actionIds;
    const auto owners = m_owners;
    const auto transient = m_transient, blockers = m_blockers;
    const auto watched = m_watch->watchedServices();
    const auto desktop = *m_desktop;
    const auto storage = m_store ? m_store->checkpoint() : RuntimeStore::State{};
    return [this, registry, actions, owners, transient, blockers, watched, storage, desktop] {
        *m_registry = registry; m_actionIds = actions; m_owners = owners;
        m_transient = transient; m_blockers = blockers; m_blocked = !blockers.isEmpty();
        m_watch->setWatchedServices(watched);
        *m_desktop = desktop;
        if (m_store) m_store->restore(storage);
    };
}
bool CompatibilityEndpoint::authenticated(const QDBusMessage &message) const
{
    if (!message.service().startsWith(QLatin1Char(':')) || !m_bus.interface()) return false;
    const QDBusReply<uint> uid = m_bus.interface()->serviceUid(message.service());
    return uid.isValid() && uid.value() == uint(geteuid());
}
void CompatibilityEndpoint::ownerLost(const QString &owner)
{
    // AGENT-GUARD: Friendly labels are not bus credentials. Persisted keys
    // remain reserved but disconnected actions cannot dispatch until reclaimed.
    for (auto it = m_owners.begin(); it != m_owners.end();) {
        if (it.value() == owner) {
            const auto binding = m_registry->binding(it.key());
            m_registry->setActive(it.key(), binding && m_desktop->available(*binding));
            if (m_transient.contains(it.key())) {
                m_registry->unregisterBinding(it.key()); m_actionIds.remove(it.key());
                if (m_store) m_store->setTransient(it.key(), false);
                m_transient.remove(it.key());
            }
            it = m_owners.erase(it);
        } else ++it;
    }
    m_blockers.remove(owner); m_blocked = !m_blockers.isEmpty();
    m_watch->removeWatchedService(owner);
}
void CompatibilityEndpoint::setAdmission(std::function<bool()> admission) { m_admission = std::move(admission); }
bool CompatibilityEndpoint::admitted() const { return !m_blocked && (!m_admission || m_admission()); }
bool CompatibilityEndpoint::persist(QString *error) { return !m_store || m_store->save(error); }
}
