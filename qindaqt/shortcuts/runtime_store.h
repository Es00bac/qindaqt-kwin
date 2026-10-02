/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once
#include "shortcuts/shortcut_registry.h"
#include <QSet>

namespace KWin::Shortcuts
{
// Synchronous compositor-thread persistence adapter. Registry is borrowed.
// Native values win during the single KConfig legacy import; a malformed
// store/import or failed atomic write fails closed without replacing the file.
class RuntimeStore final
{
public:
    explicit RuntimeStore(Registry &registry);
    bool load(const QString &nativePath, const QString &legacyPath, QString *error);
    bool save(QString *error);
    void setTransient(const QString &id, bool transient);
    void keysAssigned(const QString &id);
    bool hasSavedKeys(const QString &id) const;
private:
    Registry &m_registry;
    Store m_store;
    QString m_path;
    QSet<QString> m_transient;
    QSet<QString> m_assigned;
};
}
