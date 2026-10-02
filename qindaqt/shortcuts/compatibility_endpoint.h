/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "shortcuts/shortcut_registry.h"

#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QHash>
#include <QSet>
#include <QDBusServiceWatcher>
#include <memory>
#include <functional>

#include <optional>

namespace KWin::Shortcuts
{
// The injected registry must outlive this endpoint; every call runs on the
// owning bus thread. Runtime registration is limited to the experimental
// source option; KGlobalAccelD remains the default until all migration gates pass.
class RuntimeStore;
class DesktopActions;
enum class ShortcutEvent { Pressed, Repeated, Released };

class CompatibilityEndpoint final : public QDBusVirtualObject
{
public:
    explicit CompatibilityEndpoint(QDBusConnection bus, Registry *registry, RuntimeStore *store = nullptr);
    ~CompatibilityEndpoint() override;

    bool start(const QString &serviceName);
    void setAdmission(std::function<bool()> admission);
    bool admitted() const;
    bool shortcutsBlocked() const noexcept;
    void notifyShortcut(const Binding &binding, ShortcutEvent event, qint64 timestamp);
    QString introspect(const QString &path) const override;
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;

private:
    QString nativeIntrospection() const;
    std::function<void()> snapshotRestore();
    void announceChanges(const Registry &before);
    bool authenticated(const QDBusMessage &) const;
    bool handleNative(const QDBusMessage &, const QDBusConnection &);
    void ownerLost(const QString &owner);
    bool persist(QString *error);
    bool handleComponent(const QDBusMessage &, const QDBusConnection &);
    QString componentPath(const QString &component) const;
    bool ensureBinding(const QStringList &actionId, const QString &owner = {});
    QDBusConnection m_bus;
    Registry *m_registry = nullptr;
    QHash<QString, QStringList> m_actionIds;
    QSet<QString> m_componentPaths;
    QString m_serviceName;
    RuntimeStore *m_store = nullptr;
    std::unique_ptr<DesktopActions> m_desktop;
    std::unique_ptr<QDBusServiceWatcher> m_watch;
    QHash<QString, QString> m_owners;
    QSet<QString> m_blockers;
    QSet<QString> m_transient;
    std::function<bool()> m_admission;
    bool m_blocked = false;
};
} // namespace KWin::Shortcuts
