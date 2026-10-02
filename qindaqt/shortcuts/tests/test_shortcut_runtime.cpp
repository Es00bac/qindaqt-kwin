/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "qindaqt/shortcuts/input_dispatcher.h"
#include "qindaqt/shortcuts/runtime_store.h"
#include <KConfig>
#include <KConfigGroup>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>
using namespace KWin::Shortcuts;
class RuntimeTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void legacyImportNativePrecedence() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto legacy = dir.filePath("legacyrc");
        KConfig config(legacy, KConfig::SimpleConfig); KConfigGroup group(&config, "shell");
        group.writeEntry("launcher", QStringList{"Meta+Space", "Alt+F1", "Show launcher"}); config.sync();
        Registry registry; RuntimeStore runtime(registry); QString error;
        QVERIFY2(runtime.load(dir.filePath("native.json"), legacy, &error), qPrintable(error));
        const QString id = QStringLiteral("shell") + QChar(0x1f) + QStringLiteral("launcher");
        QVERIFY(registry.binding(id)); QVERIFY(!registry.binding(id)->active);
        QCOMPARE(registry.binding(id)->shortcuts, QList<QKeySequence>{QKeySequence("Meta+Space")});
        QCOMPARE(registry.setShortcuts(id, {QKeySequence("Ctrl+F12")}).status, RegisterStatus::Registered);
        QVERIFY(runtime.save(&error));
        group.writeEntry("launcher", QStringList{"Alt+F9", "Alt+F1", "Changed legacy"}); config.sync();
        Registry reloaded; RuntimeStore second(reloaded);
        QVERIFY(second.load(dir.filePath("native.json"), legacy, &error));
        QCOMPARE(reloaded.binding(id)->shortcuts, QList<QKeySequence>{QKeySequence("Ctrl+F12")});
        QVERIFY(!reloaded.binding(id)->active);
    }
    void modifierRepeatReleaseAndAdmission() {
        Registry registry; Binding meta{"shell", "launcher", "Launcher", {"Shell"}, {QKeySequence(int(Qt::MetaModifier))}, {}, true};
        QCOMPARE(registry.registerBinding(meta).status, RegisterStatus::Registered);
        Binding key{"shell", "action", "Action", {"Shell"}, {QKeySequence("Ctrl+F12")}, {}, true};
        QCOMPARE(registry.registerBinding(key).status, RegisterStatus::Registered);
        QList<ShortcutEvent> events; InputDispatcher input(registry, [&](const Binding &, ShortcutEvent e) { events.append(e); });
        QVERIFY(!input.key(int(Qt::MetaModifier) | Qt::Key_Meta, ShortcutEvent::Pressed, false, false));
        QVERIFY(!input.key(Qt::Key_Meta, ShortcutEvent::Released, false, false));
        QCOMPARE(events, (QList<ShortcutEvent>{ShortcutEvent::Pressed, ShortcutEvent::Released})); events.clear();
        input.key(int(Qt::MetaModifier) | Qt::Key_Meta, ShortcutEvent::Pressed, false, false); input.cancelModifierOnly();
        input.key(Qt::Key_Meta, ShortcutEvent::Released, false, false); QVERIFY(events.isEmpty());
        QVERIFY(input.key(int(Qt::ControlModifier) | Qt::Key_F12, ShortcutEvent::Pressed, false, false));
        QVERIFY(input.key(int(Qt::ControlModifier) | Qt::Key_F12, ShortcutEvent::Repeated, false, false));
        input.key(Qt::Key_F12, ShortcutEvent::Released, false, false);
        QCOMPARE(events, (QList<ShortcutEvent>{ShortcutEvent::Pressed, ShortcutEvent::Repeated, ShortcutEvent::Released})); events.clear();
        QVERIFY(!input.key(int(Qt::ControlModifier) | Qt::Key_F12, ShortcutEvent::Pressed, true, false));
        QVERIFY(!input.key(int(Qt::ControlModifier) | Qt::Key_F12, ShortcutEvent::Pressed, false, true)); QVERIFY(events.isEmpty());
    }
    void rollingSequence() {
        Registry registry; Binding binding{"editor", "comment", "Comment", {"Editor"}, {QKeySequence("Ctrl+K, Ctrl+C")}, {}, false};
        QCOMPARE(registry.registerBinding(binding).status, RegisterStatus::Registered);
        int presses = 0; InputDispatcher input(registry, [&](const Binding &, ShortcutEvent e) { if(e == ShortcutEvent::Pressed) ++presses; });
        QVERIFY(!input.key(int(Qt::ControlModifier) | Qt::Key_K, ShortcutEvent::Pressed, false, false));
        input.key(Qt::Key_K, ShortcutEvent::Released, false, false);
        QVERIFY(input.key(int(Qt::ControlModifier) | Qt::Key_C, ShortcutEvent::Pressed, false, false)); QCOMPARE(presses, 1);
    }
};
QTEST_GUILESS_MAIN(RuntimeTest)
#include "test_shortcut_runtime.moc"
