/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2020 Méven Car <meven.car@enioka.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

// kwin
#include "utils/executable_path.h"
// Qt
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
// KF
#include <KApplicationTrader>
#include <KConfigGroup>
#include <KDesktopFile>

namespace KWin
{

// QindaQt: a client's desktop file grants privileged Wayland globals and D-Bus
// interfaces through qindaqt-kwin's own keys. AGENT-GUARD: stock KDE keys do
// not grant privileged access in a QindaQt session (container-wm ADR-0340).
const static QString s_waylandInterfaceName = QStringLiteral("X-QindaQt-KWin-Wayland-Interfaces");
const static QString s_dbusRestrictedInterfaceName = QStringLiteral("X-QindaQt-KWin-DBus-Restricted-Interfaces");

// AGENT-CONTRACT: native privileged-interface fields are XDG desktop-entry
// string lists (container-wm ADR-0340), not KConfig's comma-separated lists.
// Read the selected service's source with KDE's XDG decoder so separators and
// escaped literal values have the same meaning for discovery and private launch.
// The caller retains its identity/trust checks; missing source grants nothing.
static inline QStringList readServicePermissionList(const KService &service, const QString &fieldName)
{
    if (!service.isValid() || service.entryPath().isEmpty()) {
        return {};
    }
    const KDesktopFile desktopFile(service.entryPath());
    return desktopFile.desktopGroup().readXdgListEntry(fieldName);
}

static QStringList fetchProcessServiceField(const QString &executablePath, const QString &fieldName)
{
    // needed to be able to use the logging category in a header static function
    static QLoggingCategory KWIN_UTILS("KWIN_UTILS", QtWarningMsg);
    const auto servicesFound = KApplicationTrader::query([&executablePath](const KService::Ptr &service) {
        const auto splitCommandList = QProcess::splitCommand(service->exec());
        if (splitCommandList.isEmpty()) {
            return false;
        }
        return QFileInfo(splitCommandList.first()).canonicalFilePath() == executablePath;
    });

    if (servicesFound.isEmpty()) {
        qCDebug(KWIN_UTILS) << "Could not find the desktop file for" << executablePath;
        return {};
    }

    const auto fieldValues = readServicePermissionList(*servicesFound.first(), fieldName);
    if (KWIN_UTILS().isDebugEnabled()) {
        qCDebug(KWIN_UTILS) << "Interfaces found for" << executablePath << fieldName << ":" << fieldValues;
    }
    return fieldValues;
}

static inline QStringList fetchRequestedInterfacesForDesktopId(const QString &desktopId)
{
    const auto service = KService::serviceByDesktopName(desktopId);
    if (!service) {
        return {};
    }
    return readServicePermissionList(*service, s_waylandInterfaceName);
}

static inline QStringList fetchRequestedInterfaces(const QString &executablePath)
{
    return fetchProcessServiceField(executablePath, s_waylandInterfaceName);
}

static inline QStringList fetchRestrictedDBusInterfacesFromPid(const uint pid)
{
    const auto executablePath = executablePathFromPid(pid);
    if (executablePath.isEmpty()) {
        return QStringList();
    }
    return fetchProcessServiceField(executablePath, s_dbusRestrictedInterfaceName);
}

} // namespace
