/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compatibility_endpoint.h"
#include "runtime_store.h"
#include <QDBusMessage>
#include <QDBusArgument>
#include <QVariantMap>
#include <QMap>

namespace KWin::Shortcuts
{
namespace
{
std::optional<QList<QKeySequence>> nativeKeys(const QVariant &value)
{
    const QStringList strings = qdbus_cast<QStringList>(value);
    if (strings.size() > 4) return std::nullopt;
    QList<QKeySequence> keys;
    for (const auto &text : strings) {
        if (text.size() > 256) return std::nullopt;
        const QKeySequence key(text, QKeySequence::PortableText);
        if (key.isEmpty() || key.toString(QKeySequence::PortableText) != text || keys.contains(key)) return std::nullopt;
        keys.append(key);
    }
    return keys;
}
QStringList nativeTexts(const QList<QKeySequence> &keys)
{
    QStringList text; for (const auto &key : keys) text.append(key.toString(QKeySequence::PortableText)); return text;
}
}
QString CompatibilityEndpoint::nativeIntrospection() const
{
    return QStringLiteral("<interface name='org.qindaqt.Shortcuts1'>"
        "<method name='ListBindings'><arg type='a{sv}' direction='out'/></method>"
        "<method name='Register'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='as' direction='in'/><arg type='b' direction='in'/><arg type='b' direction='in'/><arg type='b' direction='out'/><arg type='s' direction='out'/></method>"
        "<method name='SetShortcuts'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='as' direction='in'/><arg type='b' direction='out'/><arg type='s' direction='out'/></method>"
        "<method name='Conflicts'><arg type='as' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='as' direction='out'/></method>"
        "<method name='Unregister'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
        "<signal name='Activated'><arg type='s'/><arg type='s'/><arg type='t'/></signal>"
        "<signal name='Deactivated'><arg type='s'/><arg type='s'/><arg type='t'/></signal>"
        "<signal name='BindingsChanged'/></interface>");
}
bool CompatibilityEndpoint::handleNative(const QDBusMessage &message, const QDBusConnection &connection)
{
    const auto args = message.arguments(); const auto member = message.member();
    const QMap<QString, QString> signatures{{"ListBindings", ""}, {"Register", "sssasbb"}, {"SetShortcuts", "ssas"}, {"Conflicts", "asss"}, {"Unregister", "ss"}};
    if (!signatures.contains(member) || signatures.value(member) != message.signature()) {
        connection.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("Native shortcut signature mismatch"))); return true;
    }
    const Registry before = *m_registry;
    const auto restore = snapshotRestore();
    const auto reply = [this, &connection, &message, restore, before](QVariantList values, bool changed = false) {
        QString error;
        if (changed && !persist(&error)) {
            restore();
            connection.send(message.createErrorReply(QStringLiteral("org.qindaqt.Shortcuts1.Error.Persistence"), error));
            return;
        }
        connection.send(message.createReply(values));
        if (changed) announceChanges(before);
    };
    if (message.interface() != QLatin1String("org.qindaqt.Shortcuts1")) return false;
    if (member == QLatin1String("ListBindings") && args.isEmpty()) {
        QVariantMap rows;
        for (const auto &binding : m_registry->bindings()) {
            rows.insert(binding.id(), QVariantMap{{"component", binding.component}, {"action", binding.action},
                {"description", binding.description}, {"componentLabel", binding.owners.value(0, binding.component)},
                {"keys", nativeTexts(binding.shortcuts)}, {"defaults", nativeTexts(binding.defaultShortcuts)},
                {"active", binding.active}, {"repeat", binding.repeat}});
        }
        reply({rows}); return true;
    }
    if (member == QLatin1String("Conflicts") && args.size() == 3) {
        const auto keys = nativeKeys(args.at(0)); QStringList conflicts;
        const QString except = args.at(1).toString() + QChar(0x1f) + args.at(2).toString();
        if (keys) for (const auto &binding : m_registry->bindings()) {
            if (binding.id() == except) continue;
            for (const auto &key : *keys) if (binding.shortcuts.contains(key)) { conflicts.append(binding.id()); break; }
        }
        if (!keys) conflicts = {QStringLiteral("invalid-key-sequence")};
        reply({conflicts}); return true;
    }
    if (member == QLatin1String("Register") && args.size() == 6) {
        const auto keys = nativeKeys(args.at(3));
        const QStringList id{args.at(0).toString(), args.at(1).toString(), args.at(0).toString(), args.at(2).toString()};
        if (!keys || !ensureBinding(id, message.service())) { restore(); reply({false, QStringLiteral("Invalid or already owned binding")}); return true; }
        const auto result = m_registry->setShortcuts(id.at(0) + QChar(0x1f) + id.at(1), *keys);
        if (result.status != RegisterStatus::Registered) { restore(); reply({false, result.conflictingOwner}); return true; }
        const QString key = id.at(0) + QChar(0x1f) + id.at(1);
        m_registry->setRepeat(key, args.at(4).toBool());
        if (args.at(5).toBool()) m_transient.insert(key); else m_transient.remove(key);
        if (m_store) { m_store->setTransient(key, args.at(5).toBool()); m_store->keysAssigned(key); }
        reply({true, QString()}, true); return true;
    }
    if (member == QLatin1String("SetShortcuts") && args.size() == 3) {
        const auto keys = nativeKeys(args.at(2));
        const QString id = args.at(0).toString() + QChar(0x1f) + args.at(1).toString();
        if (m_transient.contains(id) && m_owners.value(id) != message.service()) {
            reply({false, QStringLiteral("Transient shortcut belongs to another caller")}); return true;
        }
        const auto result = keys ? m_registry->setShortcuts(id, *keys) : RegisterResult{};
        if (result.status == RegisterStatus::Registered && m_store) m_store->keysAssigned(id);
        reply({result.status == RegisterStatus::Registered, result.conflictingOwner}, result.status == RegisterStatus::Registered); return true;
    }
    if (member == QLatin1String("Unregister") && args.size() == 2) {
        const QString key = args.at(0).toString() + QChar(0x1f) + args.at(1).toString();
        if (m_owners.contains(key) && m_owners.value(key) != message.service() && m_transient.contains(key)) {
            reply({false}); return true;
        }
        const bool removed = m_registry->unregisterBinding(key);
        m_actionIds.remove(key); m_owners.remove(key); m_transient.remove(key);
        if (m_store) m_store->setTransient(key, false);
        reply({removed}, removed); return true;
    }
    connection.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"), QStringLiteral("Unsupported native shortcut method")));
    return true;
}
}
