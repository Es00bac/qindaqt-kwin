// SPDX-License-Identifier: GPL-2.0-or-later
#include "captureauthority.h"

#include <QDBusConnection>
#include <QDBusServiceWatcher>

namespace KWin
{
void CaptureAuthority::watchPortalOwners()
{
    const QString frontend = QStringLiteral("org.freedesktop.portal.Desktop");
    const QString backend = QStringLiteral("org.freedesktop.impl.portal.desktop.qindaqt.capture");
    auto *watcher = new QDBusServiceWatcher(this);
    watcher->setConnection(QDBusConnection::sessionBus());
    watcher->setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    watcher->addWatchedService(frontend);
    watcher->addWatchedService(backend);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this, frontend, backend](const QString &name, const QString &oldOwner, const QString &newOwner) {
        // AGENT-GUARD: loss of a well-known role revokes even while its old
        // unique sender/pidfd stays alive. Match the event's old actor so a
        // delayed event cannot revoke jobs admitted for a different owner.
        if (oldOwner.isEmpty() || oldOwner == newOwner) {
            return;
        }
        if (name == backend) {
            if (oldOwner == m_brokerSender) {
                retireBroker();
            }
        } else if (name == frontend) {
            QList<quint64> affected;
            for (const auto &[id, job] : m_jobs) {
                if (job->frontend.sender == oldOwner) {
                    affected.append(id);
                }
            }
            for (const auto id : affected) {
                revokeJob(id);
            }
        }
    }, Qt::DirectConnection);
}
}
