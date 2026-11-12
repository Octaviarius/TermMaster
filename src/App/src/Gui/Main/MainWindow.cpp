#include "MainWindow.h"

#include "../Terminal/TerminalTabGroup.h"
#include "../Terminal/TerminalWorkspace.h"
#include "../Workspace/WorkspacePage.h"

#include <QCoreApplication>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <memory>

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent), _mainMenu(menuBar()), _toolbar(new QToolBar(tr("Main"), this))
{
    setObjectName("mainWindow");
    _refreshTitle();
    resize(1000, 650);

    _workspacePage     = new WorkspacePage(this);
    _terminalWorkspace = new TerminalWorkspace(this);
    _central           = new QStackedWidget(this);
    _central->addWidget(_workspacePage);
    _central->addWidget(_terminalWorkspace);
    setCentralWidget(_central);

    connect(_workspacePage, &WorkspacePage::openRequested, this, &MainWindow::openWorkspaceRequested);
    connect(_workspacePage, &WorkspacePage::createRequested, this, &MainWindow::createWorkspaceRequested);
    connect(_workspacePage, &WorkspacePage::openSessionRequested, this, &MainWindow::openSessionRequested);
    connect(_workspacePage, &WorkspacePage::createSessionRequested, this, &MainWindow::createSessionRequested);

    _createMenus();
    _toolbar.toolbar()->setIconSize(QSize(24, 24));
    _toolbar.toolbar()->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(_toolbar.toolbar());
    statusBar()->showMessage(tr("Ready"));
}

MenubarActionContainer& MainWindow::mainMenu()
{
    return _mainMenu;
}

ToolbarActionContainer& MainWindow::toolbar()
{
    return _toolbar;
}

TerminalWorkspace& MainWindow::terminalWorkspace()
{
    return *_terminalWorkspace;
}

QTabWidget& MainWindow::terminalTabs()
{
    return _terminalWorkspace->initialGroup()->tabs();
}

void MainWindow::setWorkspace(const QString& name, const QString& file)
{
    _workspaceName = name;
    _refreshTitle();
    _workspacePage->setWorkspace(name, file);

    bool hasTerminals = false;
    for (auto* group : _terminalWorkspace->groups())
    {
        if (group->tabs().count() > 0)
        {
            hasTerminals = true;
            break;
        }
    }
    showTerminals(hasTerminals);
}

void MainWindow::showWorkspaceChoice(const QString& error)
{
    _workspaceName.clear();
    _sessionName.clear();
    _refreshTitle();
    _workspacePage->showChoice(error);
    showTerminals(false);
}

void MainWindow::setSession(const QString& name, const QString& file, int objects, int links)
{
    _sessionName = name;
    _refreshTitle();
    _workspacePage->setSession(name, file, objects, links);
}

void MainWindow::setTerminalTab(int            index,
                                const QString& title,
                                const QString& detail,
                                const QIcon&   typeIcon,
                                const QIcon&   statusIcon)
{
    _terminalWorkspace->initialGroup()->setTabPresentation(index, title, detail, typeIcon, statusIcon);
}

void MainWindow::showTerminals(bool visible)
{
    _central->setCurrentWidget(visible ? static_cast<QWidget*>(_terminalWorkspace) : _workspacePage);
}

void MainWindow::_createMenus()
{
    _mainMenu.addGroup("main");
    const auto menu = [this](const QString& id, const QString& title, const QStringList& groups) {
        auto item = std::make_unique<MenuActionContainer>(id, title);
        for (const auto& group : groups)
        {
            item->addGroup(group);
        }
        _mainMenu.addMenu(item.get(), "main");
        item.release();
    };
    menu("file", tr("&File"), {"workspace", "settings", "recent", "application"});
    auto* file   = _mainMenu.findMenu("/file");
    auto  recent = std::make_unique<MenuActionContainer>("recentWorkspaces", tr("Recent &workspaces"));
    recent->addGroup("entries");
    file->addMenu(recent.get(), "recent");
    recent.release();
    menu("edit", tr("&Edit"), {"history", "clipboard", "selection"});
    menu("view", tr("&View"), {"panels", "terminal", "window"});
    menu("session", tr("&Session"), {"session", "recent", "settings"});
    auto recentSessions = std::make_unique<MenuActionContainer>("recentSessions", tr("Recent &sessions"));
    recentSessions->addGroup("entries");
    _mainMenu.findMenu("/session")->addMenu(recentSessions.get(), "recent");
    recentSessions.release();
    menu("terminal", tr("&Terminal"), {"create", "allConnections", "connection", "management", "output", "settings"});
    menu("tools", tr("&Tools"), {"management"});
    menu("help", tr("&Help"), {"about"});
    _toolbar.addGroup("terminalCreation");
}

void MainWindow::_refreshTitle()
{
    QStringList parts;
    for (const auto& name : {_workspaceName, _sessionName, QCoreApplication::applicationName()})
    {
        if (!name.isEmpty())
        {
            parts.append(name);
        }
    }
    setWindowTitle(parts.join(QString::fromUtf8(" — ")));
}
