/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compatibility_endpoint.h"
#include "compatibility_endpoint_wire.h"
#include <QDBusMessage>
#include <QDBusVariant>

namespace KWin::Shortcuts
{
namespace {
QString actionKey(const QStringList &id)
{ return id.size() == 4 ? id.at(0) + QChar(0x1f) + id.at(1) : QString(); }
}
bool CompatibilityEndpoint::handleComponent(const QDBusMessage &message, const QDBusConnection &connection)
{
    const auto args = message.arguments();
    const QString member = message.member();
    const QString path = message.path();
    const auto sendReply = [&connection, &message](QVariantList values = {}) { connection.send(message.createReply(values)); };
    const auto failUnsupported = [&connection, &message]() {
        connection.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"), QStringLiteral("Unsupported shortcut method")));
    };
        QString component;
        for (const auto &id : m_actionIds)
            if (componentPath(id.at(0)) == path) {
                component = id.at(0);
                break;
            }
        if (member == QLatin1String("isActive")) {
            bool active = false;
            for (const auto &binding : m_registry->bindings())
                active |= binding.component == component && binding.active;
            sendReply({active});
            return true;
        }
        if (message.interface() == QLatin1String("org.freedesktop.DBus.Properties")
            && member == QLatin1String("Get") && args.size() == 2) {
            const QString property = args.at(1).toString();
            if (property == QLatin1String("friendlyName")) {
                QString friendly;
                for (const auto &id : m_actionIds)
                    if (id.at(0) == component) { friendly = id.at(2); break; }
                sendReply({QVariant::fromValue(QDBusVariant(friendly))});
            } else if (property == QLatin1String("uniqueName")) {
                sendReply({QVariant::fromValue(QDBusVariant(component))});
            } else {
                connection.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownProperty"), property));
            }
            return true;
        }
        if (member == QLatin1String("getShortcutContexts")) {
            sendReply({QStringList{QStringLiteral("default")}});
            return true;
        }
        if (member == QLatin1String("cleanUp")) {
            for (const auto &id : m_actionIds)
                if (id.at(0) == component)
                    m_registry->setActive(actionKey(id), false);
            sendReply({true});
            return true;
        }
        if (member == QLatin1String("shortcutNames") && (args.isEmpty() || args.size() == 1)) {
            QStringList names;
            for (const auto &id : m_actionIds)
                if (id.at(0) == component)
                    names.append(id.at(1));
            sendReply({names});
            return true;
        }
        if (member == QLatin1String("allShortcutInfos") && (args.isEmpty() || args.size() == 1)) {
            QList<ShortcutInfoWire> rows;
            for (const auto &id : m_actionIds) {
                if (id.at(0) != component)
                    continue;
                const auto binding = m_registry->binding(actionKey(id));
                if (!binding)
                    continue;
                rows.append({id.at(1), id.at(3), id.at(0), id.at(2), QStringLiteral("default"),
                             QStringLiteral("Default Context"), firstKeys(binding->shortcuts),
                             firstKeys(binding->defaultShortcuts)});
            }
            sendReply({QVariant::fromValue(rows)});
            return true;
        }
        if (member == QLatin1String("invokeShortcut") && args.size() == 1) {
            const auto binding = m_registry->binding(component + QChar(0x1f) + args.first().toString());
            if (binding && binding->active && admitted()) notifyShortcut(*binding, ShortcutEvent::Pressed, 0);
            sendReply();
            return true;
        }
        failUnsupported();
        return true;

}
}
