#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <Termaster/Core/CommandRegistry.h>
#include <stdexcept>

class CommandTests final : public QObject
{
    Q_OBJECT
private:
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
