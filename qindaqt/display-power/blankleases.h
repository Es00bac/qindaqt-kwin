// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QHash>
#include <QStringList>
namespace KWin
{
// Compositor-thread provenance, independent of the physical DPMS adapter.
// AGENT-CONTRACT: ADR-0338. External Off is recorded even when pixels are
// already off; releasing our cause must never translate that request into On.
class BlankLeases final
{
public:
    explicit BlankLeases(bool externalOff = false);
    bool acquire(const QString &id, qint64 now, qint64 lifetime);
    bool release(const QString &id, qint64 now);
    void retireAll(qint64 now);
    void expire(qint64 now);
    void externalRequest(bool off, qint64 now);
    bool effectiveOff() const;
    bool externallyOff() const;
    QStringList ids() const;
private:
    void prune(qint64 now);
    bool m_externalOff;
    QHash<QString, qint64> m_leases;
    QHash<QString, qint64> m_retired;
};
}
