#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/CommandShortcuts.h>
#include <stdexcept>

class CommandTests final : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void persistentAssignmentsAndRegistration()
    {
        auto*            registry = new CommandRegistry;
        CommandShortcuts shortcuts(registry);
        QSignalSpy       added(registry, &CommandRegistry::commandAdded);
        QSignalSpy       removed(registry, &CommandRegistry::commandRemoved);
        QVariantMap      values {
            {   "edit.copy", "Ctrl+Shift+C"},
            {"plugin.later",             ""}
        };
        QVERIFY(shortcuts.setAssignments(values));
        auto* command = registry->create("edit.copy", {}, {}, QKeySequence("Ctrl+C"));
        auto* action  = command->newAction();
        QCOMPARE(action->shortcut(), QKeySequence("Ctrl+Shift+C"));
        QCOMPARE(command->defaultShortcut(), QKeySequence("Ctrl+C"));
        auto* late = registry->create("plugin.later", {}, {}, QKeySequence("Ctrl+L"));
        QVERIFY(late->shortcut().isEmpty());
        delete late;
        QCOMPARE(removed.size(), 1);
        QVERIFY(registry->create("plugin.later", {}, {}, QKeySequence("Ctrl+L"))->shortcut().isEmpty());
        QVERIFY(registry->remove("edit.copy"));
        command = registry->create("edit.copy", {}, {}, QKeySequence("Ctrl+C"));
        QCOMPARE(command->shortcut(), QKeySequence("Ctrl+Shift+C"));
        QCOMPARE(shortcuts.assignments(), values);
        QVERIFY(shortcuts.setAssignments({}));
        QCOMPARE(command->shortcut(), QKeySequence("Ctrl+C"));
        QCOMPARE(added.size(), 4);
        delete registry;
        QVERIFY(shortcuts.setAssignments(values));
    }

    Q_SLOT void rejectsShortcutConflictsAtomically()
    {
        CommandRegistry registry;
        auto*           first = registry.create("first", {}, {}, QKeySequence("Ctrl+A"));
        registry.create("second", {}, {}, QKeySequence("Ctrl+B"));
        CommandShortcuts shortcuts(&registry);
        QString          error;
        QVERIFY(!shortcuts.setAssignments(
            {
                {"first", "Ctrl+B"}
        },
            &error));
        QVERIFY(error.contains("first") && error.contains("second"));
        QCOMPARE(first->shortcut(), QKeySequence("Ctrl+A"));
        QVERIFY(shortcuts.assignments().isEmpty());
        QVERIFY(!shortcuts.setAssignments(
            {
                { "first",         "Ctrl+K"},
                {"second", "Ctrl+K, Ctrl+C"}
        },
            &error));
        QVERIFY(!shortcuts.setAssignments(
            {
                {"first", 1}
        },
            &error));
        QVERIFY(!shortcuts.setAssignments(
            {
                {"first", "not a key"}
        },
            &error));
        QVERIFY(shortcuts.setAssignments(
            {
                { "first",       ""},
                {"second", "Ctrl+A"}
        },
            &error));
        QVERIFY(error.isEmpty());
        QVERIFY(first->shortcut().isEmpty());
    }

    Q_SLOT void shortcutDispatchUsesUpdatedActions()
    {
        CommandRegistry  registry;
        auto*            command = registry.create("dispatch", {}, {}, QKeySequence("Ctrl+J"));
        CommandShortcuts shortcuts(&registry);
        QWidget          window;
        auto*            action = command->newAction(&window);
        window.addAction(action);
        QSignalSpy triggered(action, &QAction::triggered);
        window.show();
        window.activateWindow();
        window.setFocus();
        QTRY_VERIFY(window.isActiveWindow());
        QTest::keyClick(&window, Qt::Key_J, Qt::ControlModifier);
        QCOMPARE(triggered.size(), 1);
        QVERIFY(shortcuts.setAssignments({
            {"dispatch", "Ctrl+K"}
        }));
        QTest::keyClick(&window, Qt::Key_J, Qt::ControlModifier);
        QCOMPARE(triggered.size(), 1);
        QTest::keyClick(&window, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(triggered.size(), 2);
        QVERIFY(shortcuts.setAssignments({
            {"dispatch", ""}
        }));
        QTest::keyClick(&window, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(triggered.size(), 2);
    }

    Q_SLOT void lateConflictsAreSuppressedAndRestored()
    {
        CommandRegistry  registry;
        CommandShortcuts shortcuts(&registry);
        QSignalSpy       diagnostics(&shortcuts, &CommandShortcuts::diagnostic);
        QVERIFY(shortcuts.setAssignments({
            {"late.override", "Ctrl+K, Ctrl+C"}
        }));
        registry.create("active", {}, {}, QKeySequence("Ctrl+K"));
        auto* defaultConflict  = registry.create("late.default", {}, {}, QKeySequence("Ctrl+K"));
        auto* overrideConflict = registry.create("late.override", {}, {}, QKeySequence("Ctrl+O"));
        QVERIFY(defaultConflict->newAction()->shortcut().isEmpty());
        QVERIFY(overrideConflict->newAction()->shortcut().isEmpty());
        QCOMPARE(defaultConflict->defaultShortcut(), QKeySequence("Ctrl+K"));
        QCOMPARE(shortcuts.assignments().value("late.override").toString(), QString("Ctrl+K, Ctrl+C"));
        QCOMPARE(diagnostics.size(), 2);
        QVERIFY(registry.remove("active"));
        QCOMPARE(defaultConflict->shortcut(), QKeySequence("Ctrl+K"));
        QVERIFY(overrideConflict->shortcut().isEmpty());
        QVERIFY(registry.remove("late.default"));
        QCOMPARE(overrideConflict->shortcut(), QKeySequence("Ctrl+K, Ctrl+C"));
        QCOMPARE(diagnostics.size(), 2);
    }

    Q_SLOT void assignmentSwapsDoNotSuppressCommands()
    {
        CommandRegistry  registry;
        CommandShortcuts shortcuts(&registry);
        auto*            first  = registry.create("first", {}, {}, QKeySequence("Ctrl+A"));
        auto*            second = registry.create("second", {}, {}, QKeySequence("Ctrl+B"));
        QSignalSpy       diagnostics(&shortcuts, &CommandShortcuts::diagnostic);
        QVERIFY(shortcuts.setAssignments({
            { "first", "Ctrl+B"},
            {"second", "Ctrl+A"}
        }));
        QCOMPARE(first->shortcut(), QKeySequence("Ctrl+B"));
        QCOMPARE(second->shortcut(), QKeySequence("Ctrl+A"));
        QVERIFY(diagnostics.isEmpty());
    }

    Q_SLOT void unicodeAndShortcuts()
    {
        Command command("edit.copy", {}, {}, QKeySequence("Ctrl+C"));
        auto*   first = command.newAction();
        QCOMPARE(first->shortcut(), QKeySequence("Ctrl+C"));
        const auto text    = QString::fromUtf8("Копировать 日本語 😀");
        const auto tooltip = QString::fromUtf8("Подсказка 中文");
        command.setText(text).setTooltip(tooltip);
        auto* second = command.newAction();
        for (auto* action : command.actions())
        {
            QCOMPARE(action->text(), text);
            QCOMPARE(action->toolTip(), tooltip);
        }
        QSignalSpy changes(&command, &Command::shortcutChanged);
        command.setShortcut({});
        QVERIFY(first->shortcut().isEmpty());
        QVERIFY(second->shortcut().isEmpty());
        command.setDefaultShortcut(QKeySequence("Ctrl+Insert"));
        QVERIFY(command.shortcut().isEmpty());
        command.resetShortcut();
        QCOMPARE(second->shortcut(), QKeySequence("Ctrl+Insert"));
        QCOMPARE(changes.size(), 2);
    }

    Q_SLOT void actionLifetimes()
    {
        auto*             command = new Command("test");
        auto*             first   = command->newAction();
        QPointer<QAction> second  = command->newAction();
        connect(first, &QObject::destroyed, [second] { delete second.data(); });
        delete command;
        QVERIFY(second.isNull());

        Command remaining("remaining");
        auto*   owner = new QObject;
        remaining.newAction(owner);
        delete owner;
        QVERIFY(remaining.actions().isEmpty());
        auto*             surviving = remaining.newAction();
        QPointer<QAction> removed   = remaining.newAction();
        connect(surviving, &QAction::changed, [removed] { delete removed.data(); });
        remaining.setText("updated");
        QCOMPARE(remaining.actions().size(), 1);
        QVERIFY(removed.isNull());
    }

    Q_SLOT void rejectsCreationDuringDestruction()
    {
        auto* command  = new Command("closing");
        auto* action   = command->newAction();
        bool  rejected = false;
        connect(action, &QObject::destroyed, [command, &rejected]() { rejected = command->newAction() == nullptr; });
        delete command;
        QVERIFY(rejected);
    }

    Q_SLOT void sharedActions()
    {
        Command command("shared");
        auto*   owner  = new QObject;
        auto*   action = command.newAction(owner);
        command.setSingleInstance(true);
        QCOMPARE(action->parent(), &command);
        QCOMPARE(command.newAction(owner), action);
        delete owner;
        QCOMPARE(command.actions().size(), 1);
        command.setSingleInstance(false);
        QVERIFY(command.newAction() != action);
        command.setSingleInstance(false);
        QCOMPARE(command.actions().size(), 2);
    }

    Q_SLOT void registryValidationAndRemoval()
    {
        CommandRegistry registry;
        QVERIFY_THROWS_EXCEPTION(std::exception, registry.create("bad id"));
        QVERIFY_THROWS_EXCEPTION(std::exception, registry.create(QString(129, 'a')));
        QVERIFY_THROWS_EXCEPTION(std::exception, registry.create("a\n"));
        auto* original = registry.create("edit.copy", "Original");
        QVERIFY_THROWS_EXCEPTION(std::exception, registry.create("edit.copy", "Replaced"));
        QCOMPARE(registry.find("edit.copy"), original);
        QCOMPARE(original->text(), QString("Original"));
        delete original;
        QVERIFY(!registry.find("edit.copy"));
        QVERIFY(registry.commands().isEmpty());
        QPointer<Command> recreated = registry.create("edit.copy");
        QVERIFY(registry.remove("edit.copy"));
        QVERIFY(recreated.isNull());
        QVERIFY(!registry.remove("edit.copy"));
    }

    Q_SLOT void registryReentrantDestruction()
    {
        auto*             registry         = new CommandRegistry;
        auto*             first            = registry->create("a");
        QPointer<Command> second           = registry->create("b");
        bool              rejectedCreation = false;
        connect(first, &QObject::destroyed, [registry, second, &rejectedCreation] {
            delete second.data();
            try
            {
                registry->create("replacement");
            }
            catch (const std::logic_error&)
            {
                rejectedCreation = true;
            }
        });
        delete registry;
        QVERIFY(second.isNull());
        QVERIFY(rejectedCreation);
    }
};

QTEST_MAIN(CommandTests)
#include "CommandTests.moc"
