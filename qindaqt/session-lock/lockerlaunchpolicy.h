// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "kwin_export.h"
#include <QProcessEnvironment>
#include <QString>
namespace KWin
{
// Root-managed launch policy; no client app ID, PATH lookup or inherited loader
// knobs can supply a trusted locker. Files and all parent aliases are checked.
KWIN_EXPORT bool isRootManagedLockerFile(const QString &path);
KWIN_EXPORT bool hasRestrictedPtracePolicy();
KWIN_EXPORT bool protectLockAuthorityProcess();
KWIN_EXPORT QProcessEnvironment lockerEnvironment(const QProcessEnvironment &session, int ownedWaylandFd);
}
