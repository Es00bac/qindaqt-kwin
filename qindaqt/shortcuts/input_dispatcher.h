/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once
#include "compatibility_endpoint.h"
#include <functional>
namespace KWin::Shortcuts
{
// Adapted KGlobalAccelD modifier-only and rolling sequence logic. Same-thread,
// borrowed registry plus callback; layout normalization remains the current
// compositor's modifiersRelevantForGlobalShortcuts and Qt event key boundary.
class InputDispatcher final
{
public:
    using Notify = std::function<void(const Binding &, ShortcutEvent)>;
    InputDispatcher(Registry &registry, Notify notify);
    bool key(int combinedKey, ShortcutEvent event, bool locked, bool inhibited);
    void cancelModifierOnly();
private:
    bool dispatch(int combinedKey, ShortcutEvent event);
    enum State { Normal, PressingModifierOnly, ReleasingModifierOnly };
    Registry &m_registry;
    Notify m_notify;
    State m_state = Normal;
    Qt::KeyboardModifiers m_modifiers;
    QKeySequence m_sequence;
    std::optional<Binding> m_last;
};
}
