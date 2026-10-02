/*
    SPDX-FileCopyrightText: 2008 Michael Jansen <kde@michael-jansen.biz>
    SPDX-FileCopyrightText: 2016 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2021 David Redondo <kde@david-redondo.de>
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#include "desktop_actions.h"
#include <KService>
#include <KServiceAction>
#include <KIO/ApplicationLauncherJob>
#include <KIO/UntrustedProgramHandlerInterface>
#include <KJobUiDelegate>
#include <QStandardPaths>
#include <QFileInfo>
#include <algorithm>
namespace KWin::Shortcuts
{
namespace
{
KService::Ptr service(const QString &component)
{
    if (!component.endsWith(QLatin1String(".desktop")) || component.contains('/') || component.contains(QChar(0x1f))) return {};
    const QString file = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kglobalaccel/") + component);
    if (!file.isEmpty()) return KService::Ptr(new KService(file));
    return KService::serviceByStorageId(component);
}
class AcceptedDesktopAction : public KIO::UntrustedProgramHandlerInterface
{
public:
    explicit AcceptedDesktopAction(QObject *parent) : KIO::UntrustedProgramHandlerInterface(parent) {}
    void showUntrustedProgramWarning(KJob *, const QString &) override { Q_EMIT result(true); }
};
}
bool desktopActionAvailable(const Binding &binding)
{
    const auto application = service(binding.component); if (!application || !application->isValid()) return false;
    if (binding.action == QLatin1String("_launch")) return true;
    const auto actions = application->actions();
    return std::any_of(actions.cbegin(), actions.cend(), [&binding](const KServiceAction &action) { return action.name() == binding.action; });
}
bool launchDesktopAction(const Binding &binding)
{
    // Adapted KGlobalAccelD v6.6.6 KServiceActionComponent, same admitted
    // desktop-file semantics. Framework launch parser supplies field codes and
    // startup handling; no shell rewriting, ambient command or Plasma service.
    const auto application = service(binding.component); if (!application || !application->isValid()) return false;
    KIO::ApplicationLauncherJob *job = nullptr;
    if (binding.action == QLatin1String("_launch")) job = new KIO::ApplicationLauncherJob(application);
    else {
        const auto actions = application->actions();
        const auto it = std::find_if(actions.cbegin(), actions.cend(), [&binding](const KServiceAction &action) { return action.name() == binding.action; });
        if (it == actions.cend()) return false;
        job = new KIO::ApplicationLauncherJob(*it);
    }
    auto *delegate = new KJobUiDelegate;
    new AcceptedDesktopAction(delegate);
    job->setUiDelegate(delegate); job->start(); return true;
}
}
