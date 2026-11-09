#include "MainWindow.h"

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
    setWindowTitle(QCoreApplication::applicationName());
    resize(1000, 650);
    _workspacePage = new WorkspacePage(this);
    _terminalTabs  = new QTabWidget(this);
    _terminalTabs->setObjectName("terminalTabs");
    _terminalTabs->setTabsClosable(true);
    _terminalTabs->setMovable(true);
    _terminalTabs->setDocumentMode(true);
    _central = new QStackedWidget(this);
    _central->addWidget(_workspacePage);
    _central->addWidget(_terminalTabs);
    setCentralWidget(_central);
    connect(_workspacePage, &WorkspacePage::openRequested, this, &MainWindow::openWorkspaceRequested);
    connect(_workspacePage, &WorkspacePage::createRequested, this, &MainWindow::createWorkspaceRequested);
    connect(_workspacePage, &WorkspacePage::openSessionRequested, this, &MainWindow::openSessionRequested);
    connect(_workspacePage, &WorkspacePage::createSessionRequested, this, &MainWindow::createSessionRequested);
    _createMenus();
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

void MainWindow::setWorkspace(const QString& name, const QString& file)
{
    setWindowTitle(name + " — " + QCoreApplication::applicationName());
    _workspacePage->setWorkspace(name, file);
    showTerminals(_terminalTabs->count() > 0);
}

void MainWindow::showWorkspaceChoice(const QString& error)
{
    setWindowTitle(QCoreApplication::applicationName());
    _workspacePage->showChoice(error);
    showTerminals(false);
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
    menu("view", tr("&View"), {"panels", "window"});
    menu("session", tr("&Session"), {"session", "recent", "settings"});
    auto recentSessions = std::make_unique<MenuActionContainer>("recentSessions", tr("Recent &sessions"));
    recentSessions->addGroup("entries");
    _mainMenu.findMenu("/session")->addMenu(recentSessions.get(), "recent");
    recentSessions.release();
    menu("terminal", tr("&Terminal"), {"create", "connection", "management", "output", "settings"});
    menu("tools", tr("&Tools"), {"management"});
    menu("help", tr("&Help"), {"about"});
    for (const auto& group : QStringList {"terminalCreation", "terminal"})
    {
        _toolbar.addGroup(group);
    }
}

void MainWindow::setSession(const QString& name, const QString& file, int objects, int links)
{
    _workspacePage->setSession(name, file, objects, links);
}

QTabWidget& MainWindow::terminalTabs()
{
    return *_terminalTabs;
}
void MainWindow::showTerminals(bool visible)
{
    _central->setCurrentWidget(visible && _terminalTabs->count() ? static_cast<QWidget*>(_terminalTabs) :
                                                                   _workspacePage);
}
