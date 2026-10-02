/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QDBusArgument>
#include <QKeySequence>
#include <QList>
#include <QVariant>

namespace KWin::Shortcuts
{
struct SequenceWire
{
    QKeySequence value;
};
QDBusArgument &operator<<(QDBusArgument &arg, const SequenceWire &wire);
const QDBusArgument &operator>>(const QDBusArgument &arg, SequenceWire &wire);

struct ShortcutInfoWire
{
    QString action;
    QString actionLabel;
    QString component;
    QString componentLabel;
    QString context;
    QString contextLabel;
    QList<int> keys;
    QList<int> defaults;
};
QDBusArgument &operator<<(QDBusArgument &arg, const ShortcutInfoWire &wire);
const QDBusArgument &operator>>(const QDBusArgument &arg, ShortcutInfoWire &wire);

QList<QKeySequence> sequenceList(const QVariant &value);
QKeySequence sequenceValue(const QVariant &value);
QVariant encodedSequences(const QList<QKeySequence> &sequences);
QList<int> firstKeys(const QList<QKeySequence> &sequences);
}
Q_DECLARE_METATYPE(KWin::Shortcuts::SequenceWire)
Q_DECLARE_METATYPE(KWin::Shortcuts::ShortcutInfoWire)
