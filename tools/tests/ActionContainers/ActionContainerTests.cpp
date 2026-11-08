#include <QDir>
#include <QMainWindow>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <Termaster/Core/ActionContainer.h>
#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <Termaster/Core/Gui/Action/MenubarActionContainer.h>
#include <Termaster/Core/Gui/Action/ToolbarActionContainer.h>
#include <stdexcept>

/** Data-only backend: no widgets or rendering hooks are needed by the model. */
class FakeContainer final : public ActionContainer
{
public:
    explicit FakeContainer(QString name, QObject* owner = nullptr) : ActionContainer(name, {}, owner)
    {
    }
    void setAvailable(bool value)
    {
        _available = value;
    }
    void setAcceptActions(bool value)
    {
        _acceptActions = value;
    }
    void setAcceptMenus(bool value)
    {
        _acceptMenus = value;
    }

protected:
    bool _isAvailable() const override
    {
        return _available;
    }
    bool _acceptsAction(const QAction*) const override
    {
        return _acceptActions;
    }
    bool _acceptsMenu(const ActionContainer*) const override
    {
        return _acceptMenus;
    }

private:
    bool _available     = true;
    bool _acceptActions = true;
    bool _acceptMenus   = true;
};

class ActionContainerTests final : public QObject
{
    Q_OBJECT

private:
    static QList<QAction*> visibleItems(const QList<QAction*>& items)
    {
        QList<QAction*> result;
        for (QAction* action : items)
        {
            if (!action->isSeparator())
            {
                result.append(action);
            }
        }
        return result;
    }

    Q_SLOT void orderedGroups()
    {
        FakeContainer root("root");
        root.addGroup("");
        root.addGroup("last");
        root.insertGroupBefore("first", "");
        root.insertGroupAfter("middle", "");
        QCOMPARE(root.groups(), QStringList({"first", "", "middle", "last"}));
        QVERIFY(root.moveGroup("last", 0));
        QVERIFY(root.moveGroupAfter("first", "middle"));
        QVERIFY(root.moveGroupBefore("middle", ""));
        QCOMPARE(root.groups(), QStringList({"last", "middle", "", "first"}));
        QVERIFY(root.renameGroup("middle", "renamed"));
        QCOMPARE(root.groupIndex("renamed"), qsizetype(1));
        const QStringList before = root.groups();
        QVERIFY_THROWS_EXCEPTION(std::exception, root.addGroup("first"));
        QVERIFY_THROWS_EXCEPTION(std::exception, root.insertGroup("bad", -1));
        QVERIFY_THROWS_EXCEPTION(std::exception, root.insertGroup("bad", 99));
        QVERIFY(!root.moveGroup("first", 4));
        QVERIFY(!root.renameGroup("renamed", "first"));
        QVERIFY_THROWS_EXCEPTION(std::exception, root.insertGroupAfter("bad", "missing"));
        QVERIFY_THROWS_EXCEPTION(std::exception, root.insertGroupBefore("bad", "missing"));
        QCOMPARE(root.groups(), before);
        QVERIFY(root.removeGroup("renamed"));
        QCOMPARE(root.groupIndex("renamed"), qsizetype(-1));
        QVERIFY(!root.removeGroup("missing"));
    }

    Q_SLOT void mixedItemsAndValidation()
    {
        FakeContainer root("root");
        QAction       first("first");
        QAction       last("last");
        root.addGroup("a");
        root.addGroup("b");
        root.addAction(&first, "a");
        root.addAction(&last, "a");
        auto* menu = new FakeContainer("menu");
        root.insertMenu(menu, "a", 1);
        QCOMPARE(root.items("a").at(0).action.data(), &first);
        QCOMPARE(root.items("a").at(1).kind, ActionContainer::Kind::Menu);
        QCOMPARE(root.items("a").at(1).menu.data(), menu);
        QCOMPARE(root.items("a").at(2).action.data(), &last);
        QVERIFY(root.moveAction(&first, "a", 2));
        QCOMPARE(root.items("a").last().action.data(), &first);
        QVERIFY(root.moveMenu(menu, "b", 0));
        QCOMPARE(root.actions("a"), QList<QAction*>({&last, &first}));
        QVERIFY_THROWS_EXCEPTION(std::exception, root.addAction(&first, "b"));
        QVERIFY_THROWS_EXCEPTION(std::exception, root.insertAction(&last, "a", -1));
        QVERIFY(!root.moveAction(&first, "a", 2));
        QVERIFY(!root.moveMenu(menu, "missing", 0));
        QVERIFY_THROWS_EXCEPTION(std::exception, root.addAction(nullptr, "a"));
        QVERIFY_THROWS_EXCEPTION(std::exception, root.addAction(&last, "missing"));
        QVERIFY_THROWS_EXCEPTION(std::exception, root.insertAction(&last, "b", 1));
        QVERIFY_THROWS_EXCEPTION(std::exception, root.addMenu(nullptr, "a"));
        QVERIFY_THROWS_EXCEPTION(std::exception, root.addMenu(menu, "a"));
        auto* duplicate = new FakeContainer("menu");
        QVERIFY_THROWS_EXCEPTION(std::exception, root.addMenu(duplicate, "a"));
        QCOMPARE(duplicate->parent(), nullptr);
        delete duplicate;
        QCOMPARE(root.itemCount("a"), qsizetype(2));
        QCOMPARE(root.menus("b"), ActionContainer::List({menu}));

        root.setAcceptActions(false);
        QAction rejected("rejected");
        QVERIFY_THROWS_EXCEPTION(std::exception, root.addAction(&rejected, "b"));
        root.setAvailable(false);
        QVERIFY_THROWS_EXCEPTION(std::exception, root.addGroup("unavailable"));
        QCOMPARE(root.itemCount("a"), qsizetype(2));
        // Cleanup remains possible after the backend has disappeared.
        QVERIFY(root.clear());
        QVERIFY(root.groups().isEmpty());
    }

    Q_SLOT void subtreeOwnershipAndPaths()
    {
        QObject       originalOwner;
        FakeContainer root("root");
        FakeContainer target("target");
        root.addGroup("");
        target.addGroup("");
        auto* branch = new FakeContainer("branch", &originalOwner);
        auto* leaf   = new FakeContainer("leaf");
        branch->addGroup("nested");
        leaf->addGroup("");
        branch->addMenu(leaf, "nested");
        root.addMenu(branch, "");
        QCOMPARE(branch->parent(), &root);
        QCOMPARE(branch->parentContainer(), &root);
        QCOMPARE(leaf->root(), &root);
        QCOMPARE(root.root(), &root);
        QCOMPARE(root.path(), QString("/"));
        QCOMPARE(leaf->path(), QString("/branch/leaf"));
        QCOMPARE(root.findMenu("branch/leaf"), leaf);
        QCOMPARE(leaf->findMenu("../."), branch);
        QCOMPARE(leaf->findMenu("/branch/leaf"), leaf);
        QCOMPARE(root.findMenu("."), &root);
        QCOMPARE(root.findMenu("missing/leaf"), nullptr);
        QVERIFY_THROWS_EXCEPTION(std::exception, leaf->addMenu(&root, ""));
        QVERIFY(!root.transferMenu(branch, *leaf, "", 0));
        target.setAcceptMenus(false);
        QVERIFY(!root.transferMenu(branch, target, "", 0));
        QCOMPARE(branch->parentContainer(), &root);
        target.setAcceptMenus(true);
        QVERIFY(!root.transferMenu(branch, target, "", 1));
        QVERIFY(root.transferMenu(branch, target, "", 0));
        QCOMPARE(root.itemCount(""), qsizetype(0));
        QCOMPARE(branch->parent(), &target);
        QCOMPARE(leaf->root(), &target);
        QCOMPARE(target.takeMenu(branch), branch);
        QCOMPARE(branch->parent(), nullptr);
        QCOMPARE(branch->parentContainer(), nullptr);
        QCOMPARE(leaf->root(), branch);
        QCOMPARE(leaf->path(), QString("/leaf"));
        QPointer<ActionContainer> leafGuard(leaf);
        delete branch;
        QVERIFY(leafGuard.isNull());
        for (const QString& name : QStringList({"", ".", "..", "a/b"}))
        {
            auto* invalid = new FakeContainer(name);
            QVERIFY_THROWS_EXCEPTION(std::exception, root.addMenu(invalid, ""));
            delete invalid;
        }
    }

    Q_SLOT void qobjectOwnershipCycles()
    {
        FakeContainer ancestor("ancestor");
        FakeContainer destination("destination", &ancestor);
        destination.addGroup("menus");
        QVERIFY_THROWS_EXCEPTION(std::exception, destination.addMenu(&ancestor, "menus"));
        QCOMPARE(destination.parent(), &ancestor);
        QCOMPARE(ancestor.parent(), nullptr);
        QVERIFY(destination.menus("menus").isEmpty());

        FakeContainer source("source");
        source.addGroup("menus");
        auto* branch = new FakeContainer("branch");
        source.addMenu(branch, "menus");
        FakeContainer nestedOwner("nestedOwner", branch);
        nestedOwner.addGroup("menus");
        QVERIFY(!source.transferMenu(branch, nestedOwner, "menus", 0));
        QCOMPARE(branch->parentContainer(), &source);
        QCOMPARE(branch->parent(), &source);
        QCOMPARE(nestedOwner.parent(), branch);
        QCOMPARE(source.menus("menus"), ActionContainer::List({branch}));
    }

    Q_SLOT void borrowedActionsAndExternalDestruction()
    {
        FakeContainer first("first");
        FakeContainer second("second");
        first.addGroup("");
        second.addGroup("");
        auto*             action = new QAction("shared");
        QPointer<QAction> guard(action);
        first.addAction(action, "");
        second.addAction(action, "");
        QVERIFY(first.clearGroup(""));
        QVERIFY(guard);
        QCOMPARE(second.actions(""), QList<QAction*>({action}));
        QSignalSpy changed(&second, &ActionContainer::changed);
        delete action;
        QVERIFY(guard.isNull());
        QCOMPARE(second.itemCount(""), qsizetype(0));
        QVERIFY(!changed.isEmpty());
        auto* menu = new FakeContainer("child");
        first.addMenu(menu, "");
        delete menu;
        QCOMPARE(first.itemCount(""), qsizetype(0));
    }

    Q_SLOT void destructiveRemovalPreservesBorrowedActions()
    {
        for (int operation = 0; operation < 4; ++operation)
        {
            FakeContainer     root("root");
            QAction           action("borrowed");
            QPointer<QAction> actionGuard(&action);
            root.addGroup("group");
            auto* child = new FakeContainer("child");
            child->addGroup("");
            child->addAction(&action, "");
            QPointer<ActionContainer> guard(child);
            root.addMenu(child, "group");
            switch (operation)
            {
                case 0:
                    QVERIFY(root.removeMenu(child));
                    break;
                case 1:
                    QVERIFY(root.clearGroup("group"));
                    break;
                case 2:
                    QVERIFY(root.removeGroup("group"));
                    break;
                case 3:
                    QVERIFY(root.clear());
                    break;
            }
            QVERIFY(guard.isNull());
            QVERIFY(actionGuard);
        }
    }

    Q_SLOT void qtProjectionAndUnicode()
    {
        QMenuBar               bar;
        QToolBar               toolbar;
        MenubarActionContainer menubar(&bar);
        ToolbarActionContainer tools(&toolbar);
        MenuActionContainer    menu("menu", QString::fromUtf8("Терминал 日本語"));
        QAction                first("first");
        QAction                last("last");
        QAction                outside("outside");
        bar.addAction(&outside);
        toolbar.addAction(&outside);
        menu.menu()->addAction(&outside);
        for (ActionContainer* container : QList<ActionContainer*>({&menubar, &tools, &menu}))
        {
            container->addGroup("one");
            container->addGroup("empty");
            container->addGroup("two");
            container->addGroup("trailingEmpty");
            container->addAction(&first, "one");
            auto* submenu = new MenuActionContainer("child", "Child");
            container->addMenu(submenu, "one");
            container->addAction(&last, "two");
            QWidget* widget = container == &menubar ? static_cast<QWidget*>(&bar) :
                              container == &tools   ? static_cast<QWidget*>(&toolbar) :
                                                      menu.menu();
            QCOMPARE(visibleItems(widget->actions()),
                     QList<QAction*>({&outside, &first, submenu->menu()->menuAction(), &last}));
            QCOMPARE(widget->actions().size(), qsizetype(5));
            QVERIFY(widget->actions().at(3)->isSeparator());
            if (container == &tools)
            {
                auto* button = qobject_cast<QToolButton*>(toolbar.widgetForAction(submenu->menu()->menuAction()));
                QVERIFY(button);
                QCOMPARE(button->popupMode(), QToolButton::InstantPopup);
            }
            QVERIFY(container->moveAction(&last, "one", 0));
            QCOMPARE(visibleItems(widget->actions()),
                     QList<QAction*>({&outside, &last, &first, submenu->menu()->menuAction()}));
            QVERIFY(container->clear());
            QCOMPARE(widget->actions(), QList<QAction*>({&outside}));
        }
        QCOMPARE(menu.menu()->title(), QString::fromUtf8("Терминал 日本語"));
        menu.setTitle(QString::fromUtf8("接続 — Соединение"));
        QCOMPARE(menu.menu()->title(), menu.title());
    }

    Q_SLOT void widgetAndWrapperLifetimes()
    {
        QObject                owner;
        auto*                  bar     = new QMenuBar;
        auto*                  toolbar = new QToolBar;
        MenubarActionContainer menubar(bar, &owner);
        ToolbarActionContainer tools(toolbar, &owner);
        menubar.addGroup("");
        tools.addGroup("");
        delete bar;
        delete toolbar;
        QVERIFY(!menubar.isAvailable());
        QVERIFY(!tools.isAvailable());
        QVERIFY_THROWS_EXCEPTION(std::exception, menubar.addGroup("dead"));
        QVERIFY_THROWS_EXCEPTION(std::exception, tools.addGroup("dead"));
        QMenuBar survivingBar;
        QToolBar survivingToolbar;
        QAction  borrowed("borrowed");
        {
            MenubarActionContainer temporaryBar(&survivingBar);
            ToolbarActionContainer temporaryToolbar(&survivingToolbar);
            temporaryBar.addGroup("");
            temporaryToolbar.addGroup("");
            temporaryBar.addAction(&borrowed, "");
            temporaryToolbar.addAction(&borrowed, "");
        }
        QVERIFY(survivingBar.actions().isEmpty());
        QVERIFY(survivingToolbar.actions().isEmpty());
        survivingBar.addAction(&borrowed);
        survivingToolbar.addAction(&borrowed);
    }

    Q_SLOT void nativeMenuRemovalAndTransfer()
    {
        QMenuBar               firstBar;
        QMenuBar               secondBar;
        MenubarActionContainer first(&firstBar);
        MenubarActionContainer second(&secondBar);
        first.addGroup("menus");
        second.addGroup("menus");
        auto*                     menu = new MenuActionContainer("child", "Child");
        QPointer<ActionContainer> guard(menu);
        QAction                   action("borrowed");
        menu->addGroup("items");
        menu->addAction(&action, "items");
        first.addMenu(menu, "menus");
        QVERIFY(first.transferMenu(menu, second, "menus", 0));
        QVERIFY(visibleItems(firstBar.actions()).isEmpty());
        QCOMPARE(visibleItems(secondBar.actions()), QList<QAction*>({menu->menu()->menuAction()}));
        delete menu->menu();
        QVERIFY(guard.isNull());
        QCOMPARE(second.itemCount("menus"), qsizetype(0));
        QVERIFY(visibleItems(secondBar.actions()).isEmpty());
        QCOMPARE(action.text(), QString("borrowed"));
    }

    Q_SLOT void visualFixture()
    {
        const QString destination = qEnvironmentVariable("TERMASTER_ACTION_GUI_CAPTURE");
        if (destination.isEmpty())
        {
            QSKIP("Set TERMASTER_ACTION_GUI_CAPTURE to capture the synthetic visual fixture.");
        }
        QVERIFY(QDir().mkpath(destination));
        QMainWindow window;
        window.setWindowTitle("Action container visual fixture");
        window.resize(800, 400);
        auto*                  toolbar = window.addToolBar("Commands");
        MenubarActionContainer menubar(window.menuBar());
        ToolbarActionContainer tools(toolbar);
        QAction                connect(QString::fromUtf8("Connect — Подключить"), &window);
        QAction                disconnect("Disconnect", &window);
        QAction                preferences("Preferences", &window);
        menubar.addGroup("menus");
        tools.addGroup("commands");
        auto* menu = new MenuActionContainer("connection", QString::fromUtf8("Connection — 接続"));
        menu->addGroup("connection");
        menu->addGroup("settings");
        menu->addAction(&connect, "connection");
        menu->addAction(&disconnect, "connection");
        auto* submenu = new MenuActionContainer("options", "Options");
        submenu->addGroup("preferences");
        submenu->addAction(&preferences, "preferences");
        menu->addMenu(submenu, "settings");
        menubar.addMenu(menu, "menus");
        tools.addAction(&connect, "commands");
        tools.addAction(&disconnect, "commands");
        tools.addGroup("menus");
        auto* toolbarMenu = new MenuActionContainer("options", "Options");
        toolbarMenu->addGroup("preferences");
        toolbarMenu->addAction(&preferences, "preferences");
        tools.addMenu(toolbarMenu, "menus");
        window.show();
        QTest::qWait(150);
        QVERIFY(window.grab().save(QDir(destination).filePath("ActionContainersWindow.png")));
        menu->menu()->popup(window.mapToGlobal(QPoint(20, 60)));
        QTest::qWait(150);
        QVERIFY(menu->menu()->grab().save(QDir(destination).filePath("ActionContainersMenu.png")));
        menu->menu()->close();
    }
};

QTEST_MAIN(ActionContainerTests)
#include "ActionContainerTests.moc"
