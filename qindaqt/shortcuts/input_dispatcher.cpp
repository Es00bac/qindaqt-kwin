/*
    SPDX-FileCopyrightText: 2008 Michael Jansen <kde@michael-jansen.biz>
    SPDX-FileCopyrightText: 2022 Ahmad Samir <a.samirh78@gmail.com>
    SPDX-FileCopyrightText: 2026 Nicolas Fella <nicolas.fella@gmx.de>
    SPDX-FileCopyrightText: 2026 Kristen McWilliam <kristen@kde.org>
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#include "input_dispatcher.h"
#include <utility>
namespace KWin::Shortcuts
{
namespace
{
Qt::KeyboardModifier modifier(int key)
{
    switch (key) {
    case Qt::Key_Shift: return Qt::ShiftModifier;
    case Qt::Key_Control: return Qt::ControlModifier;
    case Qt::Key_Alt: return Qt::AltModifier;
    case Qt::Key_Meta: return Qt::MetaModifier;
    default: return Qt::NoModifier;
    }
}
}
InputDispatcher::InputDispatcher(Registry &registry, Notify notify)
    : m_registry(registry), m_notify(std::move(notify)) {}
bool InputDispatcher::key(int combined, ShortcutEvent event, bool locked, bool inhibited)
{
    // Source adaptation: KGlobalAccelD v6.6.6 GlobalShortcutsRegistry::keyEvent
    // and processKey (59fbb109bd449aca3167df65cf3526fe31a81bcb). No daemon state.
    int sym = combined & ~Qt::KeyboardModifierMask;
    auto mods = Qt::KeyboardModifiers(combined & Qt::KeyboardModifierMask);
    if (sym == Qt::Key_Super_L || sym == Qt::Key_Super_R) { sym = Qt::Key_Meta; mods |= Qt::MetaModifier; }
    if (sym == Qt::Key_SysReq) { sym = Qt::Key_Print; mods |= Qt::AltModifier; }
    if (locked || inhibited) {
        m_state = Normal; m_sequence = {}; m_modifiers = {};
        if (m_last) { m_notify(*m_last, ShortcutEvent::Released); m_last.reset(); }
        return false;
    }
    bool handled = false;
    if (sym == 0) {
        m_state = Normal;
        if (event != ShortcutEvent::Released) m_sequence = {};
    } else if (modifier(sym) != Qt::NoModifier) {
        if (event == ShortcutEvent::Released) {
            if (m_state == PressingModifierOnly) {
                m_state = ReleasingModifierOnly;
                dispatch(int(m_modifiers), ShortcutEvent::Pressed);
            }
            m_modifiers = mods & ~modifier(sym);
            if (m_state == ReleasingModifierOnly && !m_modifiers) m_state = Normal;
        } else {
            m_state = PressingModifierOnly; m_modifiers = modifier(sym) | mods;
            return false;
        }
    } else {
        m_state = Normal;
        if (event != ShortcutEvent::Released) {
            m_modifiers = mods; handled = dispatch(int(mods) | sym, event);
            if (!handled && sym == Qt::Key_Backtab) {
                handled = dispatch(int(mods | Qt::ShiftModifier) | Qt::Key_Tab, event);
            }
        }
    }
    if (event == ShortcutEvent::Released && m_last) {
        m_notify(*m_last, ShortcutEvent::Released); m_last.reset();
    }
    return handled;
}
bool InputDispatcher::dispatch(int combined, ShortcutEvent event)
{
    int keys[4] = {};
    int count = m_sequence.count();
    const int skip = count == 4 ? 1 : 0;
    for (int i = skip; i < count; ++i) keys[i - skip] = m_sequence[i].toCombined();
    keys[count - skip] = combined;
    m_sequence = QKeySequence(keys[0], keys[1], keys[2], keys[3]);
    std::optional<Binding> binding;
    for (int length = 1; length <= m_sequence.count(); ++length) {
        int suffix[4] = {};
        for (int i = 0; i < length; ++i) suffix[i] = m_sequence[m_sequence.count() - length + i].toCombined();
        binding = m_registry.dispatch(QKeySequence(suffix[0], suffix[1], suffix[2], suffix[3]), event == ShortcutEvent::Repeated);
        if (binding) break;
    }
    if (!binding) return false;
    m_sequence = {};
    if (m_last && m_last->id() != binding->id()) m_notify(*m_last, ShortcutEvent::Released);
    m_notify(*binding, event); m_last = binding;
    return true;
}
void InputDispatcher::cancelModifierOnly() { m_state = Normal; }
}
