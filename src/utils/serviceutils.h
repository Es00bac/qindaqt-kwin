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

namespace KWin
{

// QindaQt: a client's desktop file grants privileged Wayland globals and D-Bus
// interfaces through qindaqt-kwin's own keys. AGENT-NOTE: the X-KDE-* keys are
// honoured as well only until PF21 (container-wm ADR-0289); afterwards stock KDE
// applications get no privileged access in a QindaQt session.
const static QString s_waylandInterfaceName = QStringLiteral("X-QindaQt-KWin-Wayland-Interfaces");
const static QString s_dbusRestrictedInterfaceName = QStringLiteral("X-QindaQt-KWin-DBus-Restricted-Interfaces");
const static QString s_legacyWaylandInterfaceName = QStringLiteral("X-KDE-Wayland-Interfaces");
const static QString s_legacyDbusRestrictedInterfaceName = QStringLiteral("X-KDE-DBUS-Restricted-Interfaces");

static QStringList fetchProcessServiceField(const QString &executablePath, const QString &fieldName, const QString &legacyFieldName)
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

    const auto fieldValues = servicesFound.first()->property<QStringList>(fieldName)
        + servicesFound.first()->property<QStringList>(legacyFieldName);
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
    return service->property<QStringList>(s_waylandInterfaceName) + service->property<QStringList>(s_legacyWaylandInterfaceName);
}

static inline QStringList fetchRequestedInterfaces(const QString &executablePath)
{
    return fetchProcessServiceField(executablePath, s_waylandInterfaceName, s_legacyWaylandInterfaceName);
}

static inline QStringList fetchRestrictedDBusInterfacesFromPid(const uint pid)
{
    const auto executablePath = executablePathFromPid(pid);
    if (executablePath.isEmpty()) {
        return QStringList();
    }
    return fetchProcessServiceField(executablePath, s_dbusRestrictedInterfaceName, s_legacyDbusRestrictedInterfaceName);
}

} // namespace
