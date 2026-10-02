/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once
#include "shortcuts/shortcut_registry.h"
namespace KWin::Shortcuts
{
// Static desktop-file actions retain activation after the Settings client exits.
// Uses the existing KService/KIO launcher contract; never interprets Exec itself.
bool desktopActionAvailable(const Binding &binding);
bool launchDesktopAction(const Binding &binding);
}
