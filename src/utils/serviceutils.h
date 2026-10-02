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
// interfaces through qindaqt-kwin's own keys. AGENT-GUARD: stock KDE keys do
// not grant privileged access in a QindaQt session (container-wm ADR-0340).
const static QString s_waylandInterfaceName = QStringLiteral("X-QindaQt-KWin-Wayland-Interfaces");
const static QString s_dbusRestrictedInterfaceName = QStringLiteral("X-QindaQt-KWin-DBus-Restricted-Interfaces");

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

    const auto fieldValues = servicesFound.first()->property<QStringList>(fieldName);
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
    return service->property<QStringList>(s_waylandInterfaceName);
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
