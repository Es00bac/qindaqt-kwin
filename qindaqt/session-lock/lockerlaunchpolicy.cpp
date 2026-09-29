// SPDX-License-Identifier: GPL-2.0-or-later
#include "lockerlaunchpolicy.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
namespace KWin
{
namespace
{
bool managedParents(QString path)
{
    for (;;) {
        struct stat st{};
        if (stat(path.toLocal8Bit().constData(), &st) || !S_ISDIR(st.st_mode)
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
bool isRootManagedLockerFile(const QString &path)
{
    const QFileInfo file(path);
    const QString canonical = file.canonicalFilePath();
    struct stat st{};
    return !canonical.isEmpty() && !stat(canonical.toLocal8Bit().constData(), &st)
        && S_ISREG(st.st_mode) && st.st_uid == 0 && !(st.st_mode & (S_IWGRP | S_IWOTH))
        && managedParents(file.absolutePath()) && managedParents(QFileInfo(canonical).absolutePath());
}
bool hasRestrictedPtracePolicy()
{
    QFile policy(QStringLiteral("/proc/sys/kernel/yama/ptrace_scope"));
    if (!policy.open(QIODevice::ReadOnly)) {
        return false;
    }
    bool ok = false;
    const int mode = policy.readAll().trimmed().toInt(&ok);
    // execve resets dumpability before main. Yama protects the owned child in
    // this gap; afterwards each authority process disables dumpability itself.
    return ok && mode >= 1 && mode <= 3;
}
bool protectLockAuthorityProcess()
{
    const rlimit noCore{0, 0};
    return setrlimit(RLIMIT_CORE, &noCore) == 0 && prctl(PR_SET_DUMPABLE, 0L) == 0
        && prctl(PR_GET_DUMPABLE) == 0;
}
QProcessEnvironment lockerEnvironment(const QProcessEnvironment &session, int ownedWaylandFd)
{
    QProcessEnvironment env;
    for (const auto &key : {"HOME", "XDG_RUNTIME_DIR", "XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "DBUS_SESSION_BUS_ADDRESS"}) {
        if (session.contains(QLatin1String(key))) {
            env.insert(QLatin1String(key), session.value(QLatin1String(key)));
        }
    }
    const auto language = session.value(QStringLiteral("LANG"));
    const QRegularExpression locale(QStringLiteral("^[A-Za-z0-9_.@-]{1,64}$"));
    env.insert(QStringLiteral("LANG"), locale.match(language).hasMatch() ? language : QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
    env.insert(QStringLiteral("WAYLAND_SOCKET"), QString::number(ownedWaylandFd));
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland"));
    env.insert(QStringLiteral("QT_WAYLAND_SHELL_INTEGRATION"), QStringLiteral("qindaqt-session-lock"));
    env.insert(QStringLiteral("QT_QUICK_CONTROLS_STYLE"), QStringLiteral("Basic"));
    // AGENT-GUARD: LD_*, QT_PLUGIN_PATH, QML*_IMPORT_PATH, platform arguments,
    // user Qt styles and public WAYLAND_DISPLAY are absent by construction.
    return env;
}
}
