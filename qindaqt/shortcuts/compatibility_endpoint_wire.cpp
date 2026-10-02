/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compatibility_endpoint_wire.h"

#include <QDBusMetaType>

namespace KWin::Shortcuts
{
QDBusArgument &operator<<(QDBusArgument &arg, const SequenceWire &wire)
{
    arg.beginStructure();
    arg.beginArray(qMetaTypeId<int>());
    for (int i = 0; i < 4; ++i)
        arg << (i < wire.value.count() ? wire.value[i].toCombined() : 0);
    arg.endArray();
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, SequenceWire &wire)
{
    int keys[4]{};
    arg.beginStructure();
    arg.beginArray();
    for (int &key : keys)
        arg >> key;
    arg.endArray();
    arg.endStructure();
    wire.value = QKeySequence(keys[0], keys[1], keys[2], keys[3]);
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const ShortcutInfoWire &wire)
{
    arg.beginStructure();
    arg << wire.action << wire.actionLabel << wire.component << wire.componentLabel
        << wire.context << wire.contextLabel << wire.keys << wire.defaults;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, ShortcutInfoWire &wire)
{
    arg.beginStructure();
    arg >> wire.action >> wire.actionLabel >> wire.component >> wire.componentLabel
        >> wire.context >> wire.contextLabel >> wire.keys >> wire.defaults;
    arg.endStructure();
    return arg;
}

QList<QKeySequence> sequenceList(const QVariant &value)
{
    QList<SequenceWire> wire;
    value.value<QDBusArgument>() >> wire;
    QList<QKeySequence> sequences;
    for (const auto &entry : wire)
        sequences.append(entry.value);
    return sequences;
}

QKeySequence sequenceValue(const QVariant &value)
{
    SequenceWire wire;
    value.value<QDBusArgument>() >> wire;
    return wire.value;
}

QVariant encodedSequences(const QList<QKeySequence> &sequences)
{
    QList<SequenceWire> wire;
    for (const auto &sequence : sequences)
        wire.append({sequence});
    return QVariant::fromValue(wire);
}

QList<int> firstKeys(const QList<QKeySequence> &sequences)
{
    QList<int> out;
    for (const auto &sequence : sequences)
        if (!sequence.isEmpty())
            out.append(sequence[0].toCombined());
    return out;
}
}
