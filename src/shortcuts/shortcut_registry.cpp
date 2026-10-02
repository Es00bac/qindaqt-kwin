/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "shortcut_registry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace KWin::Shortcuts
{
namespace
{
constexpr int StoreVersion = 1;

bool validSequenceList(const QList<QKeySequence> &sequences)
{
    if (sequences.size() > 4) return false;
    for (qsizetype i = 0; i < sequences.size(); ++i) {
        if (sequences.at(i).isEmpty() || sequences.at(i).count() > 4)
            return false;
        for (qsizetype j = 0; j < i; ++j) {
            if (sequences.at(i) == sequences.at(j)) return false;
        }
    }
    return true;
}

bool validBinding(const Binding &binding)
{
    return !binding.component.trimmed().isEmpty()
        && !binding.action.trimmed().isEmpty()
        && !binding.component.contains(QChar(0x1f))
        && !binding.action.contains(QChar(0x1f))
        && validSequenceList(binding.shortcuts)
        && validSequenceList(binding.defaultShortcuts);
}

QJsonArray sequences(const QList<QKeySequence> &values)
{
    QJsonArray array;
    for (const QKeySequence &value : values)
        array.append(value.toString(QKeySequence::PortableText));
    return array;
}

bool readSequences(const QJsonValue &value, QList<QKeySequence> *result)
{
    if (!value.isArray() || value.toArray().size() > 4) return false;
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isString()) return false;
        const QString text = entry.toString();
        const QKeySequence sequence(text, QKeySequence::PortableText);
        if (sequence.isEmpty() || sequence.count() > 4
            || sequence.toString(QKeySequence::PortableText) != text)
            return false;
        result->append(sequence);
    }
    return true;
}

QJsonObject encode(const Binding &binding)
{
    return {
        {QStringLiteral("component"), binding.component},
        {QStringLiteral("action"), binding.action},
        {QStringLiteral("description"), binding.description},
        {QStringLiteral("owners"), QJsonArray::fromStringList(binding.owners)},
        {QStringLiteral("shortcuts"), sequences(binding.shortcuts)},
        {QStringLiteral("defaultShortcuts"), sequences(binding.defaultShortcuts)},
        {QStringLiteral("repeat"), binding.repeat},
    };
}

std::optional<Binding> decode(const QJsonValue &value)
{
    if (!value.isObject()) return std::nullopt;
    const QJsonObject object = value.toObject();
    Binding binding;
    if (!object.value(QStringLiteral("component")).isString()
        || !object.value(QStringLiteral("action")).isString()
        || !object.value(QStringLiteral("description")).isString()
        || !object.value(QStringLiteral("owners")).isArray()
        || !object.value(QStringLiteral("repeat")).isBool())
        return std::nullopt;
    binding.component = object.value(QStringLiteral("component")).toString();
    binding.action = object.value(QStringLiteral("action")).toString();
    binding.description = object.value(QStringLiteral("description")).toString();
    binding.repeat = object.value(QStringLiteral("repeat")).toBool();
    for (const QJsonValue &owner : object.value(QStringLiteral("owners")).toArray()) {
        if (!owner.isString()) return std::nullopt;
        binding.owners.append(owner.toString());
    }
    if (!readSequences(object.value(QStringLiteral("shortcuts")), &binding.shortcuts)
        || !readSequences(object.value(QStringLiteral("defaultShortcuts")), &binding.defaultShortcuts)
        || !validBinding(binding))
        return std::nullopt;
    return binding;
}
}

QString Binding::id() const
{
    return component + QChar(0x1f) + action;
}

QString Registry::conflictOwner(const QKeySequence &shortcut, const QString &exceptId) const
{
    for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it) {
        if (it.key() != exceptId && it->shortcuts.contains(shortcut))
            return it->owners.isEmpty() ? it->component : it->owners.join(QStringLiteral(", "));
    }
    return {};
}

RegisterResult Registry::registerBinding(Binding binding)
{
    if (!validBinding(binding)) return {RegisterStatus::Invalid, {}};
    const QString id = binding.id();
    if (m_bindings.contains(id)) return {RegisterStatus::Duplicate, {}};
    for (const QKeySequence &shortcut : binding.shortcuts) {
        const QString owner = conflictOwner(shortcut);
        if (!owner.isEmpty()) return {RegisterStatus::Conflict, owner};
    }
    m_bindings.insert(id, std::move(binding));
    return {RegisterStatus::Registered, {}};
}

RegisterResult Registry::setShortcuts(const QString &id, QList<QKeySequence> shortcuts)
{
    auto it = m_bindings.find(id);
    if (it == m_bindings.end() || !validSequenceList(shortcuts))
        return {RegisterStatus::Invalid, {}};
    for (const QKeySequence &shortcut : shortcuts) {
        const QString owner = conflictOwner(shortcut, id);
        if (!owner.isEmpty()) return {RegisterStatus::Conflict, owner};
    }
    it->shortcuts = std::move(shortcuts);
    return {RegisterStatus::Registered, {}};
}

bool Registry::setActive(const QString &id, bool active)
{
    auto it = m_bindings.find(id);
    if (it == m_bindings.end()) return false;
    it->active = active;
    return true;
}

bool Registry::setRepeat(const QString &id, bool repeat)
{
    auto it = m_bindings.find(id); if (it == m_bindings.end()) return false;
    it->repeat = repeat; return true;
}

bool Registry::setDefaultShortcuts(const QString &id, QList<QKeySequence> shortcuts)
{
    auto it = m_bindings.find(id);
    if (it == m_bindings.end() || !validSequenceList(shortcuts)) return false;
    it->defaultShortcuts = std::move(shortcuts);
    return true;
}

bool Registry::unregisterBinding(const QString &id)
{
    return m_bindings.remove(id) != 0;
}

void Registry::unregisterComponent(const QString &component)
{
    for (auto it = m_bindings.begin(); it != m_bindings.end();) {
        if (it->component == component) it = m_bindings.erase(it);
        else ++it;
    }
}

std::optional<Binding> Registry::binding(const QString &id) const
{
    const auto it = m_bindings.constFind(id);
    return it == m_bindings.cend() ? std::nullopt : std::optional<Binding>(*it);
}

std::optional<Binding> Registry::dispatch(const QKeySequence &shortcut, bool repeat,
                                          bool locked, bool inhibited) const
{
    if (shortcut.isEmpty() || locked || inhibited) return std::nullopt;
    for (const Binding &binding : m_bindings) {
        if (binding.active && binding.shortcuts.contains(shortcut) && (!repeat || binding.repeat))
            return binding;
    }
    return std::nullopt;
}

QList<Binding> Registry::bindings() const
{
    return m_bindings.values();
}

bool Store::load(const QString &path, QString *error)
{
    QFile file(path);
    if (file.size() > 4 * 1024 * 1024) { if (error) *error = QStringLiteral("Shortcut store exceeds its bound"); return false; }
    if (!file.exists()) {
        m_bindings.clear();
        m_legacyImported = false;
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Shortcut store is malformed");
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != StoreVersion
        || !root.value(QStringLiteral("bindings")).isArray()) {
        if (error) *error = QStringLiteral("Shortcut store version is unsupported");
        return false;
    }
    QHash<QString, Binding> loaded;
    for (const QJsonValue &entry : root.value(QStringLiteral("bindings")).toArray()) {
        auto binding = decode(entry);
        if (!binding || loaded.contains(binding->id())) {
            if (error) *error = QStringLiteral("Shortcut store contains an invalid binding");
            return false;
        }
        loaded.insert(binding->id(), *binding);
    }
    m_bindings = std::move(loaded);
    m_legacyImported = root.value(QStringLiteral("legacyImported")).toBool();
    return true;
}

bool Store::save(const QString &path, QString *error) const
{
    QJsonArray array;
    for (const Binding &binding : m_bindings) array.append(encode(binding));
    const QJsonObject root{
        {QStringLiteral("version"), StoreVersion},
        {QStringLiteral("legacyImported"), m_legacyImported},
        {QStringLiteral("bindings"), array},
    };
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool Store::upsert(const Binding &binding)
{
    if (!validBinding(binding)) return false;
    m_bindings.insert(binding.id(), binding);
    return true;
}

bool Store::remove(const QString &id) { return m_bindings.remove(id) != 0; }

bool Store::importLegacy(const QList<Binding> &legacyBindings)
{
    if (m_legacyImported) return false;
    for (const Binding &binding : legacyBindings) {
        if (!validBinding(binding)) return false;
    }
    for (Binding binding : legacyBindings) {
        if (!m_bindings.contains(binding.id()))
            m_bindings.insert(binding.id(), std::move(binding));
    }
    m_legacyImported = true;
    return true;
}

bool Store::setShortcuts(const QString &id, QList<QKeySequence> shortcuts)
{
    if (!validSequenceList(shortcuts)) return false;
    auto it = m_bindings.find(id);
    if (it == m_bindings.end()) return false;
    it->shortcuts = std::move(shortcuts);
    return true;
}

std::optional<Binding> Store::binding(const QString &id) const
{
    const auto it = m_bindings.constFind(id);
    return it == m_bindings.cend() ? std::nullopt : std::optional<Binding>(*it);
}

QList<Binding> Store::bindings() const
{
    return m_bindings.values();
}

bool Store::legacyImported() const noexcept
{
    return m_legacyImported;
}
} // namespace KWin::Shortcuts
