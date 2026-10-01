/*
    SPDX-FileCopyrightText: 2010 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Méven Car <meven.car@enioka.com>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "screenshotdbusinterface2.h"
#include "../../../qindaqt/capture-authority/captureauthority.h"
#include "core/output.h"
#include "effect/effecthandler.h"
#include "screenshot2adaptor.h"
#include "screenshotlogging.h"
#include "utils/filedescriptor.h"
#include "utils/serviceutils.h"
#include "window.h"
#include "workspace.h"
#include "wayland_server.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusConnectionInterface>

#include <fcntl.h>
#include <poll.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace KWin
{

static ScreenShotFlags screenShotFlagsFromOptions(const QVariantMap &options)
{
    ScreenShotFlags flags = ScreenShotFlags();

    const QVariant includeDecoration = options.value(QStringLiteral("include-decoration"));
    if (includeDecoration.toBool()) {
        flags |= ScreenShotIncludeDecoration;
    }

    const QVariant includeShadow = options.value(QStringLiteral("include-shadow"), true);
    if (includeShadow.toBool()) {
        flags |= ScreenShotIncludeShadow;
    }

    const QVariant includeCursor = options.value(QStringLiteral("include-cursor"));
    if (includeCursor.toBool()) {
        flags |= ScreenShotIncludeCursor;
    }

    const QVariant nativeResolution = options.value(QStringLiteral("native-resolution"));
    if (nativeResolution.toBool()) {
        flags |= ScreenShotNativeResolution;
    }

    return flags;
}

static std::optional<pid_t> pidToHide(std::optional<pid_t> callerPid, const QVariantMap &options)
{
    if (!callerPid.has_value()) {
        return std::nullopt;
    }
    const bool hideCallerWindows = options.value(QStringLiteral("hide-caller-windows"), true).toBool();
    if (!hideCallerWindows) {
        return std::nullopt;
    }
    return callerPid;
}

static const QString s_dbusServiceName = QStringLiteral("org.kde.KWin.ScreenShot2");
static const QString s_dbusInterface = QStringLiteral("org.kde.KWin.ScreenShot2");
static const QString s_dbusObjectPath = QStringLiteral("/org/kde/KWin/ScreenShot2");

static const QString s_errorNotAuthorized = QStringLiteral("org.kde.KWin.ScreenShot2.Error.NoAuthorized");
static const QString s_errorNotAuthorizedMessage = QStringLiteral("The process is not authorized to take a screenshot");
static const QString s_errorInvalidWindow = QStringLiteral("org.kde.KWin.ScreenShot2.Error.InvalidWindow");
static const QString s_errorInvalidWindowMessage = QStringLiteral("Invalid window requested");
static const QString s_errorNoActiveWindow = QStringLiteral("org.kde.KWin.ScreenShot2.Error.NoActiveWindow");
static const QString s_errorNoActiveWindowMessage = QStringLiteral("No active window");
static const QString s_errorInvalidArea = QStringLiteral("org.kde.KWin.ScreenShot2.Error.InvalidArea");
static const QString s_errorInvalidAreaMessage = QStringLiteral("Invalid area requested");
static const QString s_errorInvalidScreen = QStringLiteral("org.kde.KWin.ScreenShot2.Error.InvalidScreen");
static const QString s_errorInvalidScreenMessage = QStringLiteral("Invalid screen requested");
static const QString s_errorFileDescriptor = QStringLiteral("org.kde.KWin.ScreenShot2.Error.FileDescriptor");
static const QString s_errorFileDescriptorMessage = QStringLiteral("No valid file descriptor");

ScreenShotDBusInterface2::ScreenShotDBusInterface2(ScreenShotManager *manager)
    : QObject(manager)
    , m_effect(manager)
{
    new ScreenShot2Adaptor(this);

    QDBusConnection::sessionBus().registerObject(s_dbusObjectPath, this);
    QDBusConnection::sessionBus().registerService(s_dbusServiceName);
}

ScreenShotDBusInterface2::~ScreenShotDBusInterface2()
{
    QDBusConnection::sessionBus().unregisterService(s_dbusServiceName);
    QDBusConnection::sessionBus().unregisterObject(s_dbusObjectPath);
}

int ScreenShotDBusInterface2::version() const
{
    return 5;
}

std::optional<pid_t> ScreenShotDBusInterface2::determineCallerPid() const
{
    if (!calledFromDBus()) {
        return std::nullopt;
    }
    const QDBusReply<uint> reply = connection().interface()->servicePid(message().service());
    if (reply.isValid()) {
        return reply.value();
    } else {
        return std::nullopt;
    }
}

std::optional<ScreenShotInvocation> ScreenShotDBusInterface2::beginInvocation(std::optional<pid_t> pid) const
{
    auto *authority = waylandServer()->captureAuthority();
    if (!calledFromDBus() || !authority || !authority->privacyAllows() || !pid) {
        sendErrorReply(s_errorNotAuthorized, QStringLiteral("Session is locked or identity is unknown"));
        return std::nullopt;
    }
    auto *daemon = connection().interface();
    const QString sender = message().service();
    if (!daemon || !sender.startsWith(QLatin1Char(':'))) {
        sendErrorReply(s_errorNotAuthorized, s_errorNotAuthorizedMessage);
        return std::nullopt;
    }
    const QDBusReply<QString> owner = daemon->serviceOwner(sender);
    const QDBusReply<uint> uid = daemon->serviceUid(sender);
    const QDBusReply<uint> actualPid = daemon->servicePid(sender);
    if (!owner.isValid() || owner.value() != sender || !uid.isValid()
        || uid.value() != uint(geteuid()) || !actualPid.isValid()
        || actualPid.value() != uint(*pid)) {
        sendErrorReply(s_errorNotAuthorized, s_errorNotAuthorizedMessage);
        return std::nullopt;
    }
    FileDescriptor pidfd(int(syscall(SYS_pidfd_open, *pid, 0)));
    const QDBusReply<uint> again = daemon->servicePid(sender);
    pollfd event{pidfd.get(), POLLIN, 0};
    if (!pidfd || !again.isValid() || again.value() != uint(*pid)
        || poll(&event, 1, 0) != 0) {
        sendErrorReply(s_errorNotAuthorized, s_errorNotAuthorizedMessage);
        return std::nullopt;
    }
    ScreenShotInvocation invocation;
    invocation.sender = sender;
    invocation.pid = *pid;
    invocation.uid = uid.value();
    invocation.pidfd = std::move(pidfd);
    invocation.protectedJob = authority->beginScreenshot(sender, *pid);
    if (!invocation.protectedJob) {
        // AGENT-GUARD: a consumed or revoked helper must not fall through to
        // its legacy desktop permission, even if /proc later becomes readable.
        if (authority->isProtectedHelperSender(sender, *pid)) {
            sendErrorReply(s_errorNotAuthorized, s_errorNotAuthorizedMessage);
            return std::nullopt;
        }
#if defined(QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION) || defined(QINDAQT_CAPTURE_AUTHORITY_TEST_AUTHORIZATION)
        invocation.legacyTestPermission = qEnvironmentVariableIntValue("KWIN_SCREENSHOT_NO_PERMISSION_CHECKS") == 1;
#endif
        if (!invocation.legacyTestPermission
            && !KWin::fetchRestrictedDBusInterfacesFromPid(*pid).contains(s_dbusInterface)) {
            sendErrorReply(s_errorNotAuthorized, s_errorNotAuthorizedMessage);
            return std::nullopt;
        }
    }
    return invocation;
}

QVariantMap ScreenShotDBusInterface2::CaptureActiveWindow(const QVariantMap &options,
                                                          QDBusUnixFileDescriptor pipe)
{
    auto invocation = beginInvocation(determineCallerPid());
    if (!invocation) {
        return QVariantMap();
    }

    Window *window = workspace()->activeWindow();
    if (!window) {
        sendErrorReply(s_errorNoActiveWindow, s_errorNoActiveWindowMessage);
        return QVariantMap();
    }

    const int fileDescriptor = fcntl(pipe.fileDescriptor(), F_DUPFD_CLOEXEC, 0);
    if (fileDescriptor == -1) {
        sendErrorReply(s_errorFileDescriptor, s_errorFileDescriptorMessage);
        return QVariantMap();
    }

    takeScreenShot(window, screenShotFlagsFromOptions(options),
                   new ScreenShotSinkPipe2(fileDescriptor, message(), std::move(*invocation),
                                            waylandServer()->captureAuthority(), this));

    setDelayedReply(true);
    return QVariantMap();
}

QVariantMap ScreenShotDBusInterface2::CaptureWindow(const QString &handle,
                                                    const QVariantMap &options,
                                                    QDBusUnixFileDescriptor pipe)
{
    auto invocation = beginInvocation(determineCallerPid());
    if (!invocation) {
        return QVariantMap();
    }

    Window *window = workspace()->findWindow(QUuid(handle));
    if (!window) {
        sendErrorReply(s_errorInvalidWindow, s_errorInvalidWindowMessage);
        return QVariantMap();
    }

    const int fileDescriptor = fcntl(pipe.fileDescriptor(), F_DUPFD_CLOEXEC, 0);
    if (fileDescriptor == -1) {
        sendErrorReply(s_errorFileDescriptor, s_errorFileDescriptorMessage);
        return QVariantMap();
    }

    takeScreenShot(window, screenShotFlagsFromOptions(options),
                   new ScreenShotSinkPipe2(fileDescriptor, message(), std::move(*invocation),
                                            waylandServer()->captureAuthority(), this));

    setDelayedReply(true);
    return QVariantMap();
}

QVariantMap ScreenShotDBusInterface2::CaptureArea(int x, int y, int width, int height,
                                                  const QVariantMap &options,
                                                  QDBusUnixFileDescriptor pipe)
{
    const auto pid = determineCallerPid();
    auto invocation = beginInvocation(pid);
    if (!invocation) {
        return QVariantMap();
    }

    const Rect area(x, y, width, height);
    if (area.isEmpty()) {
        sendErrorReply(s_errorInvalidArea, s_errorInvalidAreaMessage);
        return QVariantMap();
    }

    const int fileDescriptor = fcntl(pipe.fileDescriptor(), F_DUPFD_CLOEXEC, 0);
    if (fileDescriptor == -1) {
        sendErrorReply(s_errorFileDescriptor, s_errorFileDescriptorMessage);
        return QVariantMap();
    }

    takeScreenShot(area, screenShotFlagsFromOptions(options),
                   new ScreenShotSinkPipe2(fileDescriptor, message(), std::move(*invocation),
                                            waylandServer()->captureAuthority(), this), pidToHide(pid, options));

    setDelayedReply(true);
    return QVariantMap();
}

QVariantMap ScreenShotDBusInterface2::CaptureScreen(const QString &name,
                                                    const QVariantMap &options,
                                                    QDBusUnixFileDescriptor pipe)
{
    const auto pid = determineCallerPid();
    auto invocation = beginInvocation(pid);
    if (!invocation) {
        return QVariantMap();
    }

    LogicalOutput *screen = workspace()->findOutput(name);
    if (!screen) {
        sendErrorReply(s_errorInvalidScreen, s_errorInvalidScreenMessage);
        return QVariantMap();
    }

    const int fileDescriptor = fcntl(pipe.fileDescriptor(), F_DUPFD_CLOEXEC, 0);
    if (fileDescriptor == -1) {
        sendErrorReply(s_errorFileDescriptor, s_errorFileDescriptorMessage);
        return QVariantMap();
    }

    takeScreenShot(screen, screenShotFlagsFromOptions(options),
                   new ScreenShotSinkPipe2(fileDescriptor, message(), std::move(*invocation),
                                            waylandServer()->captureAuthority(), this), pidToHide(pid, options));

    setDelayedReply(true);
    return QVariantMap();
}

QVariantMap ScreenShotDBusInterface2::CaptureActiveScreen(const QVariantMap &options,
                                                          QDBusUnixFileDescriptor pipe)
{
    const auto pid = determineCallerPid();
    auto invocation = beginInvocation(pid);
    if (!invocation) {
        return QVariantMap();
    }

    LogicalOutput *screen = workspace()->activeOutput();
    if (!screen) {
        sendErrorReply(s_errorInvalidScreen, s_errorInvalidScreenMessage);
        return QVariantMap();
    }

    const int fileDescriptor = fcntl(pipe.fileDescriptor(), F_DUPFD_CLOEXEC, 0);
    if (fileDescriptor == -1) {
        sendErrorReply(s_errorFileDescriptor, s_errorFileDescriptorMessage);
        return QVariantMap();
    }

    takeScreenShot(screen, screenShotFlagsFromOptions(options),
                   new ScreenShotSinkPipe2(fileDescriptor, message(), std::move(*invocation),
                                            waylandServer()->captureAuthority(), this), pidToHide(pid, options));

    setDelayedReply(true);
    return QVariantMap();
}

QVariantMap ScreenShotDBusInterface2::CaptureInteractive(uint kind,
                                                         const QVariantMap &options,
                                                         QDBusUnixFileDescriptor pipe)
{
    const auto pid = determineCallerPid();
    auto invocation = beginInvocation(pid);
    if (!invocation) {
        return QVariantMap();
    }
    const int fileDescriptor = fcntl(pipe.fileDescriptor(), F_DUPFD_CLOEXEC, 0);
    if (fileDescriptor == -1) {
        sendErrorReply(s_errorFileDescriptor, s_errorFileDescriptorMessage);
        return QVariantMap();
    }

    auto *sink = new ScreenShotSinkPipe2(fileDescriptor, message(), std::move(*invocation),
                                         waylandServer()->captureAuthority(), this);
    const QPointer<ScreenShotSinkPipe2> pending(sink);

    if (kind == 0) {
        kwinApp()->startInteractiveWindowSelection([=, this](Window *window) {
            effects->hideOnScreenMessage(EffectsHandler::OnScreenMessageHideFlag::SkipsCloseAnimation);
            if (!pending) return;
            if (!pending->live()) { pending->cancel(); return; }
            if (!window) {
                pending->cancel();
            } else {
                takeScreenShot(window, screenShotFlagsFromOptions(options), pending);
            }
        });
        effects->showOnScreenMessage(i18n("Select window to screen shot with left click or enter.\n"
                                          "Escape or right click to cancel."),
                                     QStringLiteral("spectacle"));
    } else {
        kwinApp()->startInteractivePositionSelection([=, this](const QPointF &point) {
            effects->hideOnScreenMessage(EffectsHandler::OnScreenMessageHideFlag::SkipsCloseAnimation);
            if (!pending) return;
            if (!pending->live()) { pending->cancel(); return; }
            if (point == QPoint(-1, -1)) {
                pending->cancel();
            } else {
                LogicalOutput *screen = effects->screenAt(point.toPoint());
                if (!screen) { pending->cancel(); return; }
                takeScreenShot(screen, screenShotFlagsFromOptions(options), pending, pidToHide(pid, options));
            }
        });
        effects->showOnScreenMessage(i18n("Create screen shot with left click or enter.\n"
                                          "Escape or right click to cancel."),
                                     QStringLiteral("spectacle"));
    }

    setDelayedReply(true);
    return QVariantMap();
}

QVariantMap ScreenShotDBusInterface2::CaptureWorkspace(const QVariantMap &options, QDBusUnixFileDescriptor pipe)
{
    const auto pid = determineCallerPid();
    auto invocation = beginInvocation(pid);
    if (!invocation) {
        return QVariantMap();
    }

    const int fileDescriptor = fcntl(pipe.fileDescriptor(), F_DUPFD_CLOEXEC, 0);
    if (fileDescriptor == -1) {
        sendErrorReply(s_errorFileDescriptor, s_errorFileDescriptorMessage);
        return QVariantMap();
    }

    takeScreenShot(effects->virtualScreenGeometry(), screenShotFlagsFromOptions(options),
                   new ScreenShotSinkPipe2(fileDescriptor, message(), std::move(*invocation),
                                            waylandServer()->captureAuthority(), this), pidToHide(pid, options));

    setDelayedReply(true);
    return QVariantMap();
}

void ScreenShotDBusInterface2::takeScreenShot(LogicalOutput *screen, ScreenShotFlags flags,
                                              ScreenShotSinkPipe2 *sink, std::optional<pid_t> pid)
{
    if (!sink->live()) {
        sink->cancel();
        return;
    }
    if (const auto result = m_effect->takeScreenShot(screen, flags, pid)) {
        sink->flush(*result, QVariantMap{
                                 {QStringLiteral("screen"), screen->name()},
                             });
    } else {
        sink->cancel();
    }
}

void ScreenShotDBusInterface2::takeScreenShot(const Rect &area, ScreenShotFlags flags,
                                              ScreenShotSinkPipe2 *sink, std::optional<pid_t> pid)
{
    if (!sink->live()) {
        sink->cancel();
        return;
    }
    if (const auto result = m_effect->takeScreenShot(area, flags, pid)) {
        sink->flush(*result, {});
    } else {
        sink->cancel();
    }
}

void ScreenShotDBusInterface2::takeScreenShot(Window *window, ScreenShotFlags flags,
                                              ScreenShotSinkPipe2 *sink)
{
    if (!sink->live()) {
        sink->cancel();
        return;
    }
    if (const auto result = m_effect->takeScreenShot(window, flags)) {
        sink->flush(*result, QVariantMap{
                                 {QStringLiteral("windowId"), window->internalId().toString()},
                             });
    } else {
        sink->cancel();
    }
}

} // namespace KWin

#include "screenshotdbusinterface2.moc"

#include "moc_screenshotdbusinterface2.cpp"
