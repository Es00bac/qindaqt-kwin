// SPDX-License-Identifier: GPL-2.0-or-later
#include "capturelaunchpolicy.h"

#include "utils/serviceutils.h"

#include <KService>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <sys/stat.h>

namespace KWin::CaptureLaunchPolicy
{
namespace
{
bool managedParents(QString path)
{
    for (;;) {
        struct stat st{};
        if (stat(QFile::encodeName(path).constData(), &st) || !S_ISDIR(st.st_mode)
            || st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH))) {
            return false;
        }
        if (path == QStringLiteral("/")) {
            return true;
        }
        path = QFileInfo(path).absolutePath();
    }
}
}

bool isRootManagedRegularFile(const QString &path)
{
    const QFileInfo original(path);
    const QString canonical = original.canonicalFilePath();
    if (!original.isAbsolute() || canonical.isEmpty()) {
        return false;
    }
    struct stat originalStat{};
    struct stat canonicalStat{};
    // AGENT-GUARD: A symlink or writable ancestor would let a same-UID
    // process replace the trusted image during the exec/dumpability gap.
    return !lstat(QFile::encodeName(path).constData(), &originalStat)
        && S_ISREG(originalStat.st_mode) && originalStat.st_uid == 0
        && !(originalStat.st_mode & (S_IWGRP | S_IWOTH))
        && !stat(QFile::encodeName(canonical).constData(), &canonicalStat)
        && S_ISREG(canonicalStat.st_mode) && canonicalStat.st_uid == 0
        && !(canonicalStat.st_mode & (S_IWGRP | S_IWOTH))
        && managedParents(original.absolutePath())
        && managedParents(QFileInfo(canonical).absolutePath());
}

bool hasRestrictedPtracePolicy()
{
    QFile file(QStringLiteral("/proc/sys/kernel/yama/ptrace_scope"));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    bool ok = false;
    const int mode = file.readAll().trimmed().toInt(&ok);
    // execve can restore dumpability before the protected child reaches main.
    return ok && mode >= 1 && mode <= 3;
}

bool isInstalledCaptureEntry(const QString &desktopPath, const QString &executablePath, bool helper)
{
    if (!isRootManagedRegularFile(desktopPath) || !isRootManagedRegularFile(executablePath)
        || !QFileInfo(executablePath).isExecutable()) {
        return false;
    }
    const KService entry(desktopPath);
    const QStringList command = QProcess::splitCommand(entry.exec());
    if (command.size() != 1 || command.constFirst() != executablePath) {
        return false;
    }
    const auto dbus = readServicePermissionList(entry, s_dbusRestrictedInterfaceName);
    const auto wayland = readServicePermissionList(entry, s_waylandInterfaceName);
    if (helper) {
        return dbus == QStringList{QStringLiteral("org.qindaqt.KWin.ScreenShot2")}
            && wayland.isEmpty();
    }
    return dbus.isEmpty() && wayland.isEmpty();
}

QProcessEnvironment environment(const QProcessEnvironment &session, bool helper)
{
    QProcessEnvironment env;
    for (const auto *name : {"HOME", "XDG_RUNTIME_DIR", "XDG_CONFIG_HOME", "XDG_STATE_HOME",
                             "XDG_DATA_HOME", "XDG_CACHE_HOME", "DBUS_SESSION_BUS_ADDRESS"}) {
        const auto key = QLatin1String(name);
        if (session.contains(key)) {
            env.insert(key, session.value(key));
        }
    }
    const QRegularExpression locale(QStringLiteral("^[A-Za-z0-9_.@-]{1,64}$"));
    const QString language = session.value(QStringLiteral("LANG"));
    env.insert(QStringLiteral("LANG"), locale.match(language).hasMatch() ? language : QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
    if (helper) {
        env.insert(QStringLiteral("WAYLAND_SOCKET"), QStringLiteral("4"));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland"));
        env.insert(QStringLiteral("QT_QUICK_CONTROLS_STYLE"), QStringLiteral("Basic"));
    }
    // AGENT-GUARD: no inherited LD_*, Qt plugin/QML override, public
    // WAYLAND_DISPLAY, or environment permission-bypass variable.
    return env;
}
}
