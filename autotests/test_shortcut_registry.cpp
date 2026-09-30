/*
    SPDX-FileCopyrightText: 2026 QindaQt contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "../src/shortcuts/shortcut_registry.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace KWin::Shortcuts;

namespace
{
Binding binding(QString component, QString action, QKeyCombination key, QString owner = {})
{
    Binding value;
    value.component = std::move(component);
    value.action = std::move(action);
    value.description = QStringLiteral("Test action");
    value.owners = owner.isEmpty() ? QStringList{} : QStringList{owner};
    value.shortcuts = {QKeySequence(key)};
    value.defaultShortcuts = value.shortcuts;
    return value;
}
}

class ShortcutRegistryTests final : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void registrationDispatchAndConflictAreAtomic();
    void componentOwnerLifecycleReleasesBindings();
    void storePersistsAndImportsLegacyOnlyOnce();
    void malformedStoreDoesNotReplaceCurrentState();
    void invalidLegacyBatchLeavesStoreAndMarkerUntouched();
};

void ShortcutRegistryTests::registrationDispatchAndConflictAreAtomic()
{
    Registry registry;
    const Binding first = binding(QStringLiteral("kwin"), QStringLiteral("overview"), Qt::CTRL | Qt::Key_Space,
                                  QStringLiteral("Window manager"));
    QCOMPARE(registry.registerBinding(first).status, RegisterStatus::Registered);
    const Binding competing = binding(QStringLiteral("shell"), QStringLiteral("launcher"), Qt::CTRL | Qt::Key_Space,
                                      QStringLiteral("Window manager: Overview"));
    const RegisterResult conflict = registry.registerBinding(competing);
    QCOMPARE(conflict.status, RegisterStatus::Conflict);
    QCOMPARE(conflict.conflictingOwner, QStringLiteral("Window manager"));
    QCOMPARE(registry.bindings().size(), 1);

    QVERIFY(registry.dispatch(first.shortcuts.first()));
    QVERIFY(!registry.dispatch(QKeySequence(Qt::CTRL | Qt::Key_Space), false, true));
    QVERIFY(!registry.dispatch(QKeySequence(Qt::CTRL | Qt::Key_Space), false, false, true));
    QVERIFY(!registry.dispatch(QKeySequence(Qt::CTRL | Qt::Key_A)));
    QCOMPARE(registry.setShortcuts(first.id(), {QKeySequence(Qt::CTRL | Qt::Key_X)}).status, RegisterStatus::Registered);
    QVERIFY(!registry.dispatch(first.shortcuts.first()));
    QCOMPARE(registry.dispatch(QKeySequence(Qt::CTRL | Qt::Key_X))->id(), first.id());
    QCOMPARE(registry.setShortcuts(first.id(), {QKeySequence(Qt::CTRL | Qt::Key_X), QKeySequence(Qt::CTRL | Qt::Key_Y)}).status,
             RegisterStatus::Registered);
    QCOMPARE(registry.dispatch(QKeySequence(Qt::CTRL | Qt::Key_X))->id(), first.id());
    QCOMPARE(registry.dispatch(QKeySequence(Qt::CTRL | Qt::Key_Y))->id(), first.id());
    const Binding occupied = binding(QStringLiteral("shell"), QStringLiteral("launcher"), Qt::CTRL | Qt::Key_Z,
                                     QStringLiteral("Desktop shell"));
    QCOMPARE(registry.registerBinding(occupied).status, RegisterStatus::Registered);
    const RegisterResult reassignment = registry.setShortcuts(first.id(), {QKeySequence(Qt::CTRL | Qt::Key_W), QKeySequence(Qt::CTRL | Qt::Key_Z)});
    QCOMPARE(reassignment.status, RegisterStatus::Conflict);
    QCOMPARE(reassignment.conflictingOwner, QStringLiteral("Desktop shell"));
    QCOMPARE(registry.binding(first.id())->shortcuts, QList<QKeySequence>({QKeySequence(Qt::CTRL | Qt::Key_X), QKeySequence(Qt::CTRL | Qt::Key_Y)}));

    Binding sequence = binding(QStringLiteral("kwin"), QStringLiteral("sequence"), Qt::CTRL | Qt::Key_K);
    sequence.shortcuts = {QKeySequence(QStringLiteral("Ctrl+K, Ctrl+C"), QKeySequence::PortableText)};
    sequence.defaultShortcuts = sequence.shortcuts;
    QCOMPARE(registry.registerBinding(sequence).status, RegisterStatus::Registered);
    QCOMPARE(registry.dispatch(sequence.shortcuts.first())->id(), sequence.id());
}

void ShortcutRegistryTests::componentOwnerLifecycleReleasesBindings()
{
    Registry registry;
    Binding first = binding(QStringLiteral("desktop-controls"), QStringLiteral("volume-up"),
                            Qt::CTRL | Qt::Key_V, QStringLiteral("Desktop controls"));
    first.repeat = true;
    QCOMPARE(registry.registerBinding(first).status, RegisterStatus::Registered);
    QVERIFY(registry.dispatch(first.shortcuts.first(), true));
    QVERIFY(!registry.dispatch(first.shortcuts.first(), true, true));
    registry.unregisterComponent(QStringLiteral("desktop-controls"));
    QVERIFY(registry.bindings().isEmpty());
    QVERIFY(!registry.dispatch(first.shortcuts.first()));
    QCOMPARE(registry.registerBinding(first).status, RegisterStatus::Registered);
}

void ShortcutRegistryTests::storePersistsAndImportsLegacyOnlyOnce()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("shortcuts.json"));
    Store store;
    Binding native = binding(QStringLiteral("kwin"), QStringLiteral("overview"), Qt::CTRL | Qt::Key_O);
    QCOMPARE(store.importLegacy({native}), true);
    QVERIFY(store.legacyImported());
    QVERIFY(store.setShortcuts(native.id(), {QKeySequence(Qt::CTRL | Qt::Key_R), QKeySequence(Qt::CTRL | Qt::Key_T)}));
    QString error;
    QVERIFY2(store.save(path, &error), qPrintable(error));

    Store loaded;
    QVERIFY2(loaded.load(path, &error), qPrintable(error));
    QVERIFY(loaded.legacyImported());
    QCOMPARE(loaded.binding(native.id())->shortcuts, QList<QKeySequence>({QKeySequence(Qt::CTRL | Qt::Key_R), QKeySequence(Qt::CTRL | Qt::Key_T)}));

    Binding replacement = binding(QStringLiteral("kwin"), QStringLiteral("overview"), Qt::CTRL | Qt::Key_P);
    QVERIFY(!loaded.importLegacy({replacement}));
    QCOMPARE(loaded.binding(native.id())->shortcuts, QList<QKeySequence>({QKeySequence(Qt::CTRL | Qt::Key_R), QKeySequence(Qt::CTRL | Qt::Key_T)}));

    const Binding userValue = binding(QStringLiteral("shell"), QStringLiteral("launcher"), Qt::CTRL | Qt::Key_L);
    const QString nativePath = directory.filePath(QStringLiteral("native-before-import.json"));
    QFile nativeFile(nativePath);
    QVERIFY(nativeFile.open(QIODevice::WriteOnly));
    nativeFile.write(R"({"version":1,"legacyImported":false,"bindings":[{"component":"kwin","action":"overview","description":"native choice","owners":[],"shortcuts":["Ctrl+U"],"defaultShortcuts":["Ctrl+O"],"repeat":false}]})");
    nativeFile.close();
    Store withNativeOverride;
    QVERIFY(withNativeOverride.load(nativePath, &error));
    QVERIFY(withNativeOverride.importLegacy({replacement, userValue}));
    QCOMPARE(withNativeOverride.binding(native.id())->shortcuts,
             QList<QKeySequence>({QKeySequence(QStringLiteral("Ctrl+U"))}));
    QVERIFY(withNativeOverride.binding(userValue.id()));
    QVERIFY(withNativeOverride.legacyImported());
    QVERIFY(!withNativeOverride.importLegacy({replacement}));
}

void ShortcutRegistryTests::malformedStoreDoesNotReplaceCurrentState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("broken.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{broken");
    file.close();

    Store store;
    const Binding current = binding(QStringLiteral("kwin"), QStringLiteral("overview"), Qt::CTRL | Qt::Key_M);
    QVERIFY(store.importLegacy({current}));
    QString error;
    QVERIFY(!store.load(path, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.binding(current.id())->shortcuts, current.shortcuts);
}

QTEST_GUILESS_MAIN(ShortcutRegistryTests)
#include "test_shortcut_registry.moc"

void ShortcutRegistryTests::invalidLegacyBatchLeavesStoreAndMarkerUntouched()
{
    Store store;
    Binding valid = binding(QStringLiteral("kwin"), QStringLiteral("overview"), Qt::CTRL | Qt::Key_O);
    Binding invalid;
    invalid.component = QStringLiteral("kwin");
    invalid.action.clear();
    QVERIFY(!store.importLegacy({valid, invalid}));
    QVERIFY(!store.legacyImported());
    QVERIFY(!store.binding(valid.id()));
}
