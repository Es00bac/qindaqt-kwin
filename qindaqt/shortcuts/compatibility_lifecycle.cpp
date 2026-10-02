/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compatibility_endpoint.h"
#include "runtime_store.h"
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <unistd.h>

namespace KWin::Shortcuts
{
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
            m_registry->setActive(it.key(), false);
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
bool CompatibilityEndpoint::persist(QString *error) { return !m_store || m_store->save(error); }
}
