// SPDX-License-Identifier: GPL-2.0-or-later
#include "eisportaladmission.h"

#include "wayland_server.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>

#include <unistd.h>

namespace KWin
{
bool eisPortalBackendCaller(const QDBusContext &context)
{
    if (!context.calledFromDBus()) {
        return false;
    }
    const QString sender = context.message().service();
    auto *daemon = context.connection().interface();
    if (!daemon || !sender.startsWith(QLatin1Char(':'))) {
        return false;
    }
    const QDBusReply<uint> uid = daemon->serviceUid(sender);
    if (!uid.isValid() || uid.value() != static_cast<uint>(geteuid())) {
        return false;
    }
    // Combined sharing/input sessions live in the compositor-launched native
    // capture broker. The ordinary resident owns no input grant (ADR-0341).
    for (const auto *name : {"org.freedesktop.impl.portal.desktop.qindaqt.capture"}) {
        const QDBusReply<QString> owner = daemon->serviceOwner(QString::fromLatin1(name));
        if (owner.isValid() && owner.value() == sender) {
            return true;
        }
    }
    return false;
}

bool eisNativeLockActive()
{
    return waylandServer() && waylandServer()->isScreenLocked();
}
}
