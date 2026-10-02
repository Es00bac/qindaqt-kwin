/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once
#include "shortcuts/shortcut_registry.h"
#include <KService>
#include <QHash>
namespace KWin::Shortcuts
{
// Static actions retain activation after Settings exits. Desktop lookup belongs
// to control-plane registration, never synchronous input dispatch. Uses existing
// KService/KIO parsing, with no hand interpretation of Exec.
class DesktopActions final
{
public:
    bool registerAction(const Binding &);
    bool available(const Binding &) const;
    bool launch(const Binding &) const;
private:
    QHash<QString, KService::Ptr> m_services;
};
}
