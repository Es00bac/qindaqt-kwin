// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace KWin::CaptureLaunchPolicy
{
bool isRootManagedRegularFile(const QString &path);
bool hasRestrictedPtracePolicy();
bool isInstalledCaptureEntry(const QString &desktopPath, const QString &executablePath, bool helper);
QProcessEnvironment environment(const QProcessEnvironment &session, bool helper);
}
