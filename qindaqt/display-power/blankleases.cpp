// SPDX-License-Identifier: GPL-2.0-or-later
#include "blankleases.h"
namespace KWin
{
namespace {
constexpr qint64 MaximumLifetime = 60000;
constexpr qsizetype MaximumLeases = 64;
constexpr qsizetype MaximumRetired = 256;
}
BlankLeases::BlankLeases(bool externalOff) : m_externalOff(externalOff) {}
void BlankLeases::prune(qint64 now)
{
    for (auto it = m_retired.begin(); it != m_retired.end();) {
        if (it.value() <= now) it = m_retired.erase(it);
        else ++it;
    }
}
bool BlankLeases::acquire(const QString &id, qint64 now, qint64 lifetime)
{
    prune(now);
    expire(now);
    if (id.isEmpty() || lifetime < 1000 || lifetime > MaximumLifetime || m_retired.contains(id)) return false;
    if (!m_leases.contains(id) && (m_leases.size() >= MaximumLeases || m_retired.size() >= MaximumRetired)) return false;
    m_leases.insert(id, now + lifetime);
    return true;
}
bool BlankLeases::release(const QString &id, qint64 now)
{
    prune(now);
    const bool owned = m_leases.remove(id) != 0;
    // A cancel-before-acquire tombstone prevents late delivery resurrecting a
    // retired episode. Capacity exhaustion denies new work, never evicts one.
    if (owned || m_retired.contains(id) || m_retired.size() < MaximumRetired)
        m_retired.insert(id, now + MaximumLifetime);
    return owned;
}
void BlankLeases::retireAll(qint64 now)
{
    const auto active = m_leases.keys();
    for (const auto &id : active) release(id, now);
}
void BlankLeases::expire(qint64 now)
{
    const auto active = m_leases.keys();
    for (const auto &id : active) {
        if (m_leases.value(id) <= now) release(id, now);
    }
}
void BlankLeases::externalRequest(bool off, qint64 now)
{
    m_externalOff = off;
    if (!off) retireAll(now); // Physical wake consumes every owned episode.
}
bool BlankLeases::effectiveOff() const { return m_externalOff || !m_leases.isEmpty(); }
bool BlankLeases::externallyOff() const { return m_externalOff; }
QStringList BlankLeases::ids() const { return m_leases.keys(); }
}
