#include "MainWindow.h"

#include "../workspace/WorkspacePage.h"

#include <QCoreApplication>
#include <QStatusBar>
#include <QToolBar>
#include <Termaster/Core/gui/action/MenuActionContainer.h>
#include <memory>

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent), _mainMenu(menuBar()), _toolbar(new QToolBar(tr("Main"), this))
{
    setObjectName("mainWindow");
    setWindowTitle(QCoreApplication::applicationName());
    resize(1000, 650);
    _workspacePage = new WorkspacePage(this);
    setCentralWidget(_workspacePage);
    connect(_workspacePage, &WorkspacePage::openRequested, this, &MainWindow::openWorkspaceRequested);
    connect(_workspacePage, &WorkspacePage::createRequested, this, &MainWindow::createWorkspaceRequested);
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
}

void MainWindow::showWorkspaceChoice(const QString& error)
{
    setWindowTitle(QCoreApplication::applicationName());
    _workspacePage->showChoice(error);
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
    menu("file", tr("&File"), {"workspace", "recent", "application"});
    auto* file = _mainMenu.findMenu("/file");
    for (const auto& entry : {
             std::pair {"recentWorkspaces", QT_TR_NOOP("Recent &workspaces")},
             std::pair {  "recentSessions",   QT_TR_NOOP("Recent &sessions")}
    })
    {
        auto item = std::make_unique<MenuActionContainer>(entry.first, tr(entry.second));
        item->addGroup("entries");
        file->addMenu(item.get(), "recent");
        item.release();
    }
    menu("edit", tr("&Edit"), {"history", "clipboard", "selection"});
    menu("view", tr("&View"), {"panels", "window"});
    menu("tools", tr("&Tools"), {"management"});
    menu("help", tr("&Help"), {"about"});
    for (const auto& group : QStringList {"application", "workspace", "session", "terminal"})
    {
        _toolbar.addGroup(group);
    }
}
