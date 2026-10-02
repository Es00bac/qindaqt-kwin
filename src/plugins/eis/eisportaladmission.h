// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusContext>

namespace KWin
{
// AGENT-CONTRACT: EIS RemoteDesktop/InputCapture D-Bus methods hand out live
// input transports. Only the current same-UID unique owner of a desktop portal
// backend name may request them; the portal backend owns application consent,
// Request/Session lifetime and native-lock retirement (QindaQt
// docs/wiki/architecture/portal-remote-input.md). The KDE backend name remains
// admitted only while the qindaqt-portals.conf selector still routes
// RemoteDesktop/InputCapture to `kde`; remove it when that route is retired.
bool eisPortalBackendCaller(const QDBusContext &context);

// AGENT-GUARD: no new EIS transport may be issued while the native session lock
// is active; existing contexts are destroyed on WaylandServer::screenAboutToLock.
bool eisNativeLockActive();
}
