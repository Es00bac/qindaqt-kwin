/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "runtime_store.h"
#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QFileInfo>

namespace KWin::Shortcuts
{
namespace
{
QList<QKeySequence> legacyKeys(const QString &text)
{
    // Adapted from KGlobalAccelD v6.6.6 Component::loadSettings:
    // legacy triples contain tab-separated complete key sequences, not CSV.
    QList<QKeySequence> keys;
    for (const QString &part : text.split(QLatin1Char('\t'))) {
        if (part.isEmpty() || part == QLatin1String("none")) continue;
        keys.append(QKeySequence(part, QKeySequence::PortableText));
    }
    return keys;
}
}
RuntimeStore::RuntimeStore(Registry &registry) : m_registry(registry) {}
bool RuntimeStore::load(const QString &nativePath, const QString &legacyPath, QString *error)
{
    Store next;
    if (!next.load(nativePath, error)) return false;
    if (!next.legacyImported()) {
        KConfig legacy(legacyPath, KConfig::SimpleConfig);
        QList<Binding> imported;
        for (const auto &component : legacy.groupList()) {
            const KConfigGroup group(&legacy, component);
            for (const auto &action : group.keyList()) {
                if (action.startsWith(QLatin1String("_k_"))) continue;
                const QStringList triple = group.readEntry(action, QStringList());
                if (triple.size() != 3) continue;
                Binding binding;
                binding.component = component;
                binding.action = action;
                binding.description = triple.at(2);
                binding.owners = {group.readEntry("_k_friendly_name", component)};
                binding.shortcuts = legacyKeys(triple.at(0));
                binding.defaultShortcuts = legacyKeys(triple.at(1));
                binding.repeat = true;
                binding.active = false;
                imported.append(binding);
            }
        }
        if (!next.importLegacy(imported)) {
            if (error) *error = QStringLiteral("Legacy shortcut import contains invalid bindings");
            return false;
        }
    }
    Registry loaded;
    for (auto binding : next.bindings()) {
        binding.active = false;
        if (loaded.registerBinding(binding).status != RegisterStatus::Registered) {
            if (error) *error = QStringLiteral("Saved shortcut bindings conflict");
            return false;
        }
    }
    if (!QDir().mkpath(QFileInfo(nativePath).absolutePath()) || !next.save(nativePath, error)) return false;
    m_assigned.clear();
    for (const auto &binding : loaded.bindings()) m_assigned.insert(binding.id());
    m_registry = std::move(loaded);
    m_store = std::move(next);
    m_path = nativePath;
    return true;
}
bool RuntimeStore::save(QString *error)
{
    if (m_path.isEmpty()) return true; // Explicit injected test endpoint, no ambient file.
    Store next = m_store;
    for (const auto &saved : m_store.bindings())
        if (!m_registry.binding(saved.id())) next.remove(saved.id());
    for (const auto &binding : m_registry.bindings())
        if (m_assigned.contains(binding.id()) && !m_transient.contains(binding.id()) && !next.upsert(binding)) return false;
    if (!next.save(m_path, error)) return false;
    m_store = std::move(next);
    return true;
}
void RuntimeStore::setTransient(const QString &id, bool transient)
{
    if (transient) m_transient.insert(id); else m_transient.remove(id);
}
void RuntimeStore::keysAssigned(const QString &id) { m_assigned.insert(id); }
bool RuntimeStore::hasSavedKeys(const QString &id) const { return m_assigned.contains(id); }
}
