/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compatibility_endpoint.h"
#include "compatibility_endpoint_wire.h"
#include "runtime_store.h"
#include "desktop_actions.h"
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <unistd.h>
#include <time.h>

#include <QDBusArgument>
#include <QDBusMetaType>

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QMap>
#include <QSet>

#include <utility>

#include <algorithm>

namespace KWin::Shortcuts
{
namespace
{
constexpr auto RootInterface = "org.kde.KGlobalAccel";
constexpr auto ComponentInterface = "org.kde.kglobalaccel.Component";

QString actionKey(const QStringList &id)
{
    return id.size() == 4 ? id.at(0) + QChar(0x1f) + id.at(1) : QString();
}
}

CompatibilityEndpoint::CompatibilityEndpoint(QDBusConnection bus, Registry *registry, RuntimeStore *store)
    : m_bus(std::move(bus))
    , m_registry(registry)
    , m_store(store)
    , m_desktop(std::make_unique<DesktopActions>())
    , m_watch(std::make_unique<QDBusServiceWatcher>(QString(), m_bus, QDBusServiceWatcher::WatchForUnregistration))
{
    connect(m_watch.get(), &QDBusServiceWatcher::serviceUnregistered, this, &CompatibilityEndpoint::ownerLost);
    qDBusRegisterMetaType<SequenceWire>();
    qDBusRegisterMetaType<QList<SequenceWire>>();
    qDBusRegisterMetaType<ShortcutInfoWire>();
    qDBusRegisterMetaType<QList<ShortcutInfoWire>>();
}

CompatibilityEndpoint::~CompatibilityEndpoint()
{
    m_bus.unregisterObject(QStringLiteral("/org/qindaqt/Shortcuts1"));
    m_bus.unregisterService(QStringLiteral("org.qindaqt.Shortcuts1"));
    m_bus.unregisterObject(QStringLiteral("/kglobalaccel"));
    for (const QString &path : std::as_const(m_componentPaths))
        m_bus.unregisterObject(path);
    if (!m_serviceName.isEmpty())
        m_bus.unregisterService(m_serviceName);
}

bool CompatibilityEndpoint::start(const QString &serviceName)
{
    if (!m_registry || !m_bus.registerService(serviceName))
        return false;
    m_serviceName = serviceName;
    if (!m_bus.registerVirtualObject(QStringLiteral("/kglobalaccel"), this)
        || !m_bus.registerService(QStringLiteral("org.qindaqt.Shortcuts1"))
        || !m_bus.registerVirtualObject(QStringLiteral("/org/qindaqt/Shortcuts1"), this)) return false;
    for (const auto &binding : m_registry->bindings()) {
        if (m_desktop->registerAction(binding)) m_registry->setActive(binding.id(), true);
        m_actionIds.insert(binding.id(), {binding.component, binding.action,
            binding.owners.value(0, binding.component), binding.description});
    }
    return true;
}

bool CompatibilityEndpoint::shortcutsBlocked() const noexcept
{
    return m_blocked;
}

void CompatibilityEndpoint::notifyShortcut(const Binding &binding, ShortcutEvent event, qint64 timestamp)
{
    if (event != ShortcutEvent::Released && (!admitted() || !binding.active)) return;
    if (!timestamp) { timespec now{}; if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) timestamp = qint64(now.tv_sec) * 1000 + now.tv_nsec / 1000000; }
    if (event == ShortcutEvent::Pressed && m_desktop->launch(binding)) return;
    if (m_desktop->available(binding)) return;
    const QString path = componentPath(binding.component);
    if (!m_componentPaths.contains(path)) {
        if (!m_bus.registerVirtualObject(path, this))
            return;
        m_componentPaths.insert(path);
    }
    const char *signalName = event == ShortcutEvent::Pressed ? "globalShortcutPressed"
        : event == ShortcutEvent::Repeated ? "globalShortcutRepeated" : "globalShortcutReleased";
    QDBusMessage signal = QDBusMessage::createSignal(path, ComponentInterface, QString::fromLatin1(signalName));
    signal.setArguments({binding.component, binding.action, timestamp});
    m_bus.send(signal);
    // AGENT-CONTRACT: Shortcuts1 has one activation signal for initial and
    // repeated presses. The registry admits repeats only for opted-in actions.
    auto native = QDBusMessage::createSignal(QStringLiteral("/org/qindaqt/Shortcuts1"), QStringLiteral("org.qindaqt.Shortcuts1"),
        event == ShortcutEvent::Released ? QStringLiteral("Deactivated") : QStringLiteral("Activated"));
    native.setArguments({binding.component, binding.action, quint64(timestamp)}); m_bus.send(native);
}

QString CompatibilityEndpoint::componentPath(const QString &component) const
{
    QString path = component;
    for (QChar &ch : path)
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_'))
            ch = QLatin1Char('_');
    return QStringLiteral("/component/") + path;
}

bool CompatibilityEndpoint::ensureBinding(const QStringList &id, const QString &owner)
{
    if (id.size() != 4 || id.at(0).trimmed().isEmpty() || id.at(1).trimmed().isEmpty()
        || id.at(0).size() > 512 || id.at(1).size() > 512 || id.at(0).contains(QChar(0x1f)) || id.at(1).contains(QChar(0x1f)))
        return false;
    const QString key = actionKey(id);
    if (!owner.isEmpty()) {
        const QString incumbent = m_owners.value(key);
        if (!incumbent.isEmpty() && incumbent != owner) return false;
        m_owners.insert(key, owner);
        m_watch->addWatchedService(owner);
    }
    if (m_actionIds.contains(key)) {
        if (const auto current = m_registry->binding(key)) m_desktop->registerAction(*current);
        if (!owner.isEmpty()) m_registry->setActive(key, true);
        return true;
    }
    if (m_actionIds.size() >= 4096) return false;
    Binding binding;
    binding.component = id.at(0);
    binding.action = id.at(1);
    binding.description = id.at(3);
    binding.owners = {id.at(2)};
    // KGlobalAccel dispatches held shortcuts repeatedly unless a higher-level
    // client opts out; this wire format has no per-action repeat switch.
    binding.repeat = true;
    binding.active = !owner.isEmpty();
    if (m_registry->registerBinding(binding).status != RegisterStatus::Registered) { m_owners.remove(key); return false; }
    m_desktop->registerAction(binding);
    m_actionIds.insert(key, id);
    return true;
}

QString CompatibilityEndpoint::introspect(const QString &path) const
{
    const auto standard = QStringLiteral("<interface name='org.freedesktop.DBus.Introspectable'>"
        "<method name='Introspect'><arg type='s' direction='out'/></method></interface>");
    if (path == QLatin1String("/org/qindaqt/Shortcuts1")) return standard + nativeIntrospection();
    if (path.startsWith(QLatin1String("/component/"))) {
        return standard + QStringLiteral("<interface name='org.kde.kglobalaccel.Component'>"
            "<property name='friendlyName' type='s' access='read'/>"
            "<property name='uniqueName' type='s' access='read'/>"
            "<method name='cleanUp'><arg type='b' direction='out'/></method>"
            "<method name='isActive'><arg type='b' direction='out'/></method>"
            "<method name='shortcutNames'><arg type='as' direction='out'/></method>"
            "<method name='shortcutNames'><arg type='s' direction='in'/><arg type='as' direction='out'/></method>"
            "<method name='allShortcutInfos'><arg type='a(ssssssaiai)' direction='out'/></method>"
            "<method name='allShortcutInfos'><arg type='s' direction='in'/><arg type='a(ssssssaiai)' direction='out'/></method>"
            "<method name='getShortcutContexts'><arg type='as' direction='out'/></method>"
            "<method name='invokeShortcut'><arg type='s' direction='in'/></method>"
            "<signal name='globalShortcutPressed'><arg type='s'/><arg type='s'/><arg type='x'/></signal>"
            "<signal name='globalShortcutRepeated'><arg type='s'/><arg type='s'/><arg type='x'/></signal>"
            "<signal name='globalShortcutReleased'><arg type='s'/><arg type='s'/><arg type='x'/></signal>"
            "</interface>");
    }
    return standard + QStringLiteral("<interface name='org.kde.KGlobalAccel'>"
        "<method name='allMainComponents'><arg type='aas' direction='out'/></method>"
        "<method name='allComponents'><arg type='ao' direction='out'/></method>"
        "<method name='allActionsForComponent'><arg type='as' direction='in'/><arg type='aas' direction='out'/></method>"
        "<method name='globalShortcutsByKey'><arg type='(ai)' direction='in'/><arg type='(i)' direction='in'/><arg type='a(ssssssaiai)' direction='out'/></method>"
        "<method name='doRegister'><arg type='as' direction='in'/></method>"
        "<method name='setShortcutKeys'><arg type='as' direction='in'/><arg type='a(ai)' direction='in'/><arg type='u' direction='in'/><arg type='a(ai)' direction='out'/></method>"
        "<method name='shortcutKeys'><arg type='as' direction='in'/><arg type='a(ai)' direction='out'/></method>"
        "<method name='defaultShortcutKeys'><arg type='as' direction='in'/><arg type='a(ai)' direction='out'/></method>"
        "<method name='setForeignShortcutKeys'><arg type='as' direction='in'/><arg type='a(ai)' direction='in'/></method>"
        "<method name='setInactive'><arg type='as' direction='in'/></method>"
        "<method name='getComponent'><arg type='s' direction='in'/><arg type='o' direction='out'/></method>"
        "<method name='globalShortcutAvailable'><arg type='(ai)' direction='in'/><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
        "<method name='unregister'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
        "<method name='setForeignShortcut'><arg type='as' direction='in'/><arg type='ai' direction='in'/></method>"
        "<method name='blockGlobalShortcuts'><arg type='b' direction='in'/></method>"
        "<method name='activateGlobalShortcutContext'><arg type='s' direction='in'/><arg type='s' direction='in'/></method>"
        "<method name='action'><arg type='i' direction='in'/><arg type='as' direction='out'/></method>"
        "<method name='actionList'><arg type='(ai)' direction='in'/><arg type='as' direction='out'/></method>"
        "<method name='shortcut'><arg type='as' direction='in'/><arg type='ai' direction='out'/></method>"
        "<method name='defaultShortcut'><arg type='as' direction='in'/><arg type='ai' direction='out'/></method>"
        "<method name='setShortcut'><arg type='as' direction='in'/><arg type='ai' direction='in'/><arg type='u' direction='in'/><arg type='ai' direction='out'/></method>"
        "<method name='unRegister'><arg type='as' direction='in'/></method>"
        "<method name='getGlobalShortcutsByKey'><arg type='i' direction='in'/><arg type='a(ssssssaiai)' direction='out'/></method>"
        "<method name='isGlobalShortcutAvailable'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
        "<signal name='yourShortcutsChanged'><arg type='as'/><arg type='a(ai)'/></signal>"
        "</interface>");
}

bool CompatibilityEndpoint::handleMessage(const QDBusMessage &message, const QDBusConnection &connection)
{
    if (!authenticated(message)) {
        connection.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"), QStringLiteral("Shortcut caller is not in this user session")));
        return true;
    }
    if (message.interface() == QLatin1String("org.freedesktop.DBus.Introspectable")
        && message.member() == QLatin1String("Introspect") && message.signature().isEmpty()) {
        const QString path = message.path();
        if (path != QLatin1String("/kglobalaccel")
            && path != QLatin1String("/org/qindaqt/Shortcuts1")
            && !m_componentPaths.contains(path)) {
            connection.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"),
                                                     QStringLiteral("Shortcut object does not exist")));
        } else {
            // AGENT-CONTRACT: Virtual objects need an explicit standard wire
            // reply; Qt uses introspect(path) for XML but routes bus calls here.
            connection.send(message.createReply(QVariantList{QStringLiteral("<node>") + introspect(path)
                                                               + QStringLiteral("</node>")}));
        }
        return true;
    }
    if (message.path() == QLatin1String("/org/qindaqt/Shortcuts1")) return handleNative(message, connection);
    const auto args = message.arguments();
    const QString member = message.member();
    const QString path = message.path();
    const Registry before = *m_registry;
    const auto restore = snapshotRestore();
    const auto sendReply = [this, &connection, &message, restore, before](QVariantList values = {}) {
        QString error;
        if (!persist(&error)) {
            restore();
            connection.send(message.createErrorReply(QStringLiteral("org.qindaqt.Shortcuts1.Error.Persistence"), error));
        } else { connection.send(message.createReply(values)); announceChanges(before); }
    };
    const auto failUnsupported = [&connection, &message]() {
        connection.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
                                                 QStringLiteral("Unsupported shortcut method")));
    };
    if (path.startsWith(QLatin1String("/component/")))
        return handleComponent(message, connection);
    if (member == QLatin1String("allMainComponents")) {
        QMap<QString, QStringList> components;
        for (const auto &id : m_actionIds)
            components.insert(id.at(0), {id.at(0), id.at(2), QString(), QString()});
        sendReply({QVariant::fromValue(components.values())});
        return true;
    }
    if (member == QLatin1String("allComponents")) {
        QList<QDBusObjectPath> paths;
        QSet<QString> seen;
        for (const auto &id : m_actionIds) {
            const QString path = componentPath(id.at(0));
            if (!seen.contains(path)) {
                seen.insert(path);
                if (!m_componentPaths.contains(path) && m_bus.registerVirtualObject(path, this))
                    m_componentPaths.insert(path);
                paths.append(QDBusObjectPath(path));
            }
        }
        sendReply({QVariant::fromValue(paths)});
        return true;
    }
    if (member == QLatin1String("allActionsForComponent") && args.size() == 1) {
        const QStringList requested = args.first().toStringList();
        QList<QStringList> rows;
        for (const auto &id : m_actionIds)
            if (!requested.isEmpty() && requested.first() == id.at(0))
                rows.append(id);
        sendReply({QVariant::fromValue(rows)});
        return true;
    }
    if (member == QLatin1String("globalShortcutsByKey") && args.size() == 2) {
        const QKeySequence sequence = sequenceValue(args.at(0));
        QList<ShortcutInfoWire> rows;
        for (const auto &id : m_actionIds) {
            const auto binding = m_registry->binding(actionKey(id));
            if (!binding || !binding->shortcuts.contains(sequence))
                continue;
            rows.append({id.at(1), id.at(3), id.at(0), id.at(2), QStringLiteral("default"),
                         QStringLiteral("Default Context"), firstKeys(binding->shortcuts),
                         firstKeys(binding->defaultShortcuts)});
        }
        sendReply({QVariant::fromValue(rows)});
        return true;
    }
    if (member == QLatin1String("doRegister") && args.size() == 1) {
        const QStringList id = args.first().toStringList();
        if (ensureBinding(id, message.service())) {
            sendReply();
        } else {
            failUnsupported();
        }
        return true;
    }
    if (member == QLatin1String("setShortcutKeys") && args.size() == 3) {
        const QStringList id = args.at(0).toStringList();
        if (!ensureBinding(id, message.service())) {
            failUnsupported();
            return true;
        }
        const QString key = actionKey(id);
        const auto sequences = sequenceList(args.at(1));
        const uint flags = args.at(2).toUInt();
        if (flags & 8)
            m_registry->setDefaultShortcuts(key, sequences);
        else if ((flags & 4) || !m_store || !m_store->hasSavedKeys(key))
            m_registry->setShortcuts(key, sequences);
        const auto binding = m_registry->binding(key);
        if (!(flags & 8) && m_store) m_store->keysAssigned(key);
        sendReply({encodedSequences((flags & 8) ? binding->defaultShortcuts : binding->shortcuts)});
        return true;
    }
    if ((member == QLatin1String("shortcutKeys") || member == QLatin1String("defaultShortcutKeys"))
        && args.size() == 1) {
        const auto binding = m_registry->binding(actionKey(args.first().toStringList()));
        sendReply({encodedSequences(binding ? (member == QLatin1String("shortcutKeys")
            ? binding->shortcuts : binding->defaultShortcuts) : QList<QKeySequence>{})});
        return true;
    }
    if ((member == QLatin1String("setForeignShortcutKeys") || member == QLatin1String("setForeignShortcut"))
        && args.size() == 2) {
        const QStringList id = args.at(0).toStringList();
        QList<QKeySequence> desired;
        if (member == QLatin1String("setForeignShortcutKeys")) {
            desired = sequenceList(args.at(1));
        } else {
            QList<int> keys;
            args.at(1).value<QDBusArgument>() >> keys;
            for (int key : keys)
                desired.append(QKeySequence(key));
        }
        if (ensureBinding(id)) {
            const QString key = actionKey(id);
            const auto firstAttempt = m_registry->setShortcuts(key, desired);
            if (firstAttempt.status == RegisterStatus::Conflict) {
                for (const auto &binding : m_registry->bindings()) {
                    if (binding.id() == key)
                        continue;
                    const bool overlaps = std::any_of(desired.cbegin(), desired.cend(), [&binding](const QKeySequence &seq) {
                        return binding.shortcuts.contains(seq);
                    });
                    if (overlaps && m_registry->setShortcuts(binding.id(), {}).status == RegisterStatus::Registered) {
                        const auto previousOwner = m_actionIds.value(binding.id());
                        if (!previousOwner.isEmpty()) {
                            QDBusMessage displaced = QDBusMessage::createSignal(
                                QStringLiteral("/kglobalaccel"), RootInterface, QStringLiteral("yourShortcutsChanged"));
                            displaced.setArguments({previousOwner, encodedSequences({})});
                            connection.send(displaced);
                        }
                    }
                }
                m_registry->setShortcuts(key, desired);
            }
            if (m_store) m_store->keysAssigned(key);
            QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/kglobalaccel"), RootInterface,
                                                              QStringLiteral("yourShortcutsChanged"));
            signal.setArguments({id, encodedSequences(desired)});
            connection.send(signal);
        }
        sendReply();
        return true;
    }
    if (member == QLatin1String("setInactive") && args.size() == 1) {
        m_registry->setActive(actionKey(args.first().toStringList()), false);
        sendReply();
        return true;
    }
    if (member == QLatin1String("getComponent") && args.size() == 1) {
        const QString path = componentPath(args.first().toString());
        if (!m_componentPaths.contains(path)) {
            if (!m_bus.registerVirtualObject(path, this)) {
                failUnsupported();
                return true;
            }
            m_componentPaths.insert(path);
        }
        sendReply({QVariant::fromValue(QDBusObjectPath(path))});
        return true;
    }
    if (member == QLatin1String("globalShortcutAvailable") && args.size() == 2) {
        const auto sequence = sequenceValue(args.at(0));
        const QString component = args.at(1).toString();
        const auto bindings = m_registry->bindings();
        const bool available = std::none_of(bindings.cbegin(), bindings.cend(), [&sequence, &component](const Binding &b) {
            return b.component != component && b.shortcuts.contains(sequence);
        });
        sendReply({available});
        return true;
    }
    if (member == QLatin1String("unregister") && args.size() == 2) {
        const QString key = args.at(0).toString() + QChar(0x1f) + args.at(1).toString();
        const bool removed = m_registry->unregisterBinding(key);
        m_actionIds.remove(key);
        m_owners.remove(key);
        sendReply({removed});
        return true;
    }
    if (member == QLatin1String("blockGlobalShortcuts") && args.size() == 1) {
        if (args.first().toBool()) {
            m_blockers.insert(message.service()); m_watch->addWatchedService(message.service());
        } else m_blockers.remove(message.service());
        m_blocked = !m_blockers.isEmpty();
        sendReply();
        return true;
    }
    if (member == QLatin1String("activateGlobalShortcutContext") && args.size() == 2) {
        if (args.at(1).toString() != QLatin1String("default")) { failUnsupported(); return true; }
        sendReply();
        return true;
    }
    if (member == QLatin1String("actionList") && args.size() == 1) {
        const QKeySequence sequence = sequenceValue(args.first());
        QStringList ids;
        for (const auto &id : m_actionIds) {
            const auto binding = m_registry->binding(actionKey(id));
            if (binding && std::any_of(binding->shortcuts.cbegin(), binding->shortcuts.cend(),
                                       [&sequence](const QKeySequence &candidate) {
                                           return candidate[0] == sequence[0];
                                       }))
                ids.append(id);
        }
        sendReply({ids});
        return true;
    }
    if (member == QLatin1String("action") && args.size() == 1) {
        const int key = args.first().toInt();
        QStringList ids;
        for (const auto &id : m_actionIds) {
            const auto binding = m_registry->binding(actionKey(id));
            if (binding && std::any_of(binding->shortcuts.cbegin(), binding->shortcuts.cend(),
                                       [key](const QKeySequence &candidate) { return candidate[0].toCombined() == key; }))
                ids.append(id);
        }
        sendReply({ids});
        return true;
    }
    if (member == QLatin1String("shortcut") || member == QLatin1String("defaultShortcut")) {
        const auto binding = args.size() == 1 ? m_registry->binding(actionKey(args.first().toStringList())) : std::nullopt;
        const auto values = binding ? (member == QLatin1String("shortcut") ? binding->shortcuts : binding->defaultShortcuts)
                                    : QList<QKeySequence>{};
        sendReply({QVariant::fromValue(firstKeys(values))});
        return true;
    }
    if (member == QLatin1String("setShortcut") && args.size() == 3) {
        const QStringList id = args.at(0).toStringList();
        QList<int> keys;
        args.at(1).value<QDBusArgument>() >> keys;
        QList<QKeySequence> sequences;
        for (int key : keys)
            sequences.append(QKeySequence(key));
        if (!ensureBinding(id, message.service())) { sendReply({QVariant::fromValue(QList<int>{})}); return true; }
        const uint flags = args.at(2).toUInt();
        const QString key = actionKey(id);
        if (flags & 8) m_registry->setDefaultShortcuts(key, sequences);
        else if ((flags & 4) || !m_store || !m_store->hasSavedKeys(key)) m_registry->setShortcuts(key, sequences);
        const auto binding = m_registry->binding(key);
        if (!(flags & 8) && m_store) m_store->keysAssigned(key);
        sendReply({QVariant::fromValue(firstKeys(binding ? (flags & 8 ? binding->defaultShortcuts : binding->shortcuts)
                                                        : QList<QKeySequence>{}))});
        return true;
    }
    if (member == QLatin1String("unRegister") && args.size() == 1) {
        const QString key = actionKey(args.first().toStringList());
        m_registry->setActive(key, false);
        sendReply();
        return true;
    }
    if (member == QLatin1String("isGlobalShortcutAvailable") && args.size() == 2) {
        const int key = args.at(0).toInt();
        const QString component = args.at(1).toString();
        const auto bindings = m_registry->bindings();
        const bool available = std::none_of(bindings.cbegin(), bindings.cend(),
            [key, &component](const Binding &binding) {
                return binding.component != component && std::any_of(binding.shortcuts.cbegin(), binding.shortcuts.cend(),
                    [key](const QKeySequence &sequence) { return sequence[0].toCombined() == key; });
            });
        sendReply({available});
        return true;
    }
    if (member == QLatin1String("getGlobalShortcutsByKey") && args.size() == 1) {
        const int key = args.first().toInt();
        QList<ShortcutInfoWire> rows;
        for (const auto &id : m_actionIds) {
            const auto binding = m_registry->binding(actionKey(id));
            if (binding && std::any_of(binding->shortcuts.cbegin(), binding->shortcuts.cend(),
                                        [key](const QKeySequence &sequence) { return sequence[0].toCombined() == key; }))
                rows.append({id.at(1), id.at(3), id.at(0), id.at(2), QStringLiteral("default"),
                             QStringLiteral("Default Context"), firstKeys(binding->shortcuts), firstKeys(binding->defaultShortcuts)});
        }
        sendReply({QVariant::fromValue(rows)});
        return true;
    }
    failUnsupported();
    return true;
}
} // namespace KWin::Shortcuts
