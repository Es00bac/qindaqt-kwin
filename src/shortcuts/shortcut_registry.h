/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace KWin::Shortcuts
{
// Pure, thread-independent value conversion. Accept canonical PortableText,
// including modifier-only chords; invalid or noncanonical input has no value.
std::optional<QKeySequence> portableSequence(const QString &text);

// AGENT-CONTRACT: PF22 persists stable component/action IDs and complete Qt
// sequences; PF23's input adapter must normalize layout-independent event keys
// to the same portable sequence representation.
struct Binding
{
    QString component;
    QString action;
    QString description;
    QStringList owners;
    QList<QKeySequence> shortcuts;
    QList<QKeySequence> defaultShortcuts;
    bool repeat = false;
    bool active = true; // Runtime owner lifecycle state; never persisted.
    QString id() const;
};
enum class RegisterStatus { Registered, Invalid, Duplicate, Conflict };
struct RegisterResult
{
    RegisterStatus status = RegisterStatus::Invalid;
    QString conflictingOwner;
};
// This policy is owned by the compositor input thread; callers serialize
// registration, lifecycle cleanup, reassignment, and dispatch on that thread.
class Registry final
{
public:
    RegisterResult registerBinding(Binding binding);
    RegisterResult setShortcuts(const QString &id, QList<QKeySequence> shortcuts);
    bool setActive(const QString &id, bool active);
    bool setRepeat(const QString &id, bool repeat);
    bool setDefaultShortcuts(const QString &id, QList<QKeySequence> shortcuts);
    bool unregisterBinding(const QString &id);
    void unregisterComponent(const QString &component);
    std::optional<Binding> binding(const QString &id) const;
    std::optional<Binding> dispatch(const QKeySequence &shortcut, bool repeat = false,
                                    bool locked = false, bool inhibited = false) const;
    QList<Binding> bindings() const;
private:
    QString conflictOwner(const QKeySequence &shortcut, const QString &exceptId = {}) const;
    QHash<QString, Binding> m_bindings;
};
// Store operations are synchronous and caller-path-scoped. A failed load leaves
// the previous in-memory value intact; save uses atomic replacement.
class Store final
{
public:
    bool load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const;
    bool upsert(const Binding &binding);
    bool remove(const QString &id);
    bool importLegacy(const QList<Binding> &legacyBindings);
    bool setShortcuts(const QString &id, QList<QKeySequence> shortcuts);
    std::optional<Binding> binding(const QString &id) const;
    QList<Binding> bindings() const;
    bool legacyImported() const noexcept;
private:
    QHash<QString, Binding> m_bindings;
    bool m_legacyImported = false;
};
} // namespace KWin::Shortcuts
