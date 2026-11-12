#include "DetachedTerminalWindow.h"

#include "../Main/EditActions.h"
#include "TerminalTabGroup.h"
#include "TerminalWorkspace.h"

#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <Termaster/Core/ActionContainer.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <Termaster/Core/Gui/Icons.h>
#include <memory>

DetachedTerminalWindow::DetachedTerminalWindow(CommandRegistry& commands, QWidget* parent) :
    QMainWindow(parent, Qt::Window), _mainMenu(menuBar())
{
    setObjectName("detachedTerminalWindow");
    _refreshTitle();
    resize(1000, 650);

    _terminalWorkspace = new TerminalWorkspace(this);
    setCentralWidget(_terminalWorkspace);
    _createMenus();

    auto* toolbar = new QToolBar(tr("Window"), this);
    toolbar->setObjectName("detachedWindowToolbar");
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setMovable(false);
    addToolBar(toolbar);

    const auto windowAction = [this, &commands, toolbar](const QString& id,
                                                         const QString& text,
                                                         const QString& icon,
                                                         const QString& tooltip) {
        auto* command = commands.find(id);
        if (!command)
        {
            command = commands.create(id, text, Icons::icon(icon));
        }
        auto* action = command->newAction(this);
        action->setObjectName(id);
        action->setToolTip(tooltip);
        action->setShortcutContext(Qt::WindowShortcut);
        addAction(action);
        _windowMenu->addAction(action, "navigation");
        toolbar->addAction(action);
        return action;
    };
    auto* returnAction = windowAction("Window.ReturnAllToMain",
                                      tr("Return all to main"),
                                      "panel-top",
                                      tr("Return every terminal in this window to the main window"));
    auto* mainAction   = windowAction("Window.GoToMain",
                                      tr("Go to main window"),
                                      "app-window",
                                      tr("Activate the main Termaster window"));
    connect(returnAction, &QAction::triggered, this, &DetachedTerminalWindow::returnRequested);
    connect(mainAction, &QAction::triggered, this, &DetachedTerminalWindow::mainWindowRequested);

    _createViewActions(commands);
    connect(_terminalWorkspace, &TerminalWorkspace::groupAdded, this, &DetachedTerminalWindow::_observeGroup);
    connect(_terminalWorkspace, &TerminalWorkspace::activated, this, &DetachedTerminalWindow::_setActiveGroup);
    for (auto* group : _terminalWorkspace->groups())
    {
        _observeGroup(group);
    }
    _setActiveGroup(_terminalWorkspace->initialGroup());
    new EditActions(*this, commands, *_editMenu, this);
}

DetachedTerminalWindow::~DetachedTerminalWindow()
{
    // QWidget tears down children after this class's members have been destroyed.
    // Stop child visibility and destruction notifications before that boundary.
    for (auto* object : findChildren<QObject*>())
    {
        object->removeEventFilter(this);
        disconnect(object, nullptr, this, nullptr);
    }
}

TerminalWorkspace& DetachedTerminalWindow::terminalWorkspace()
{
    return *_terminalWorkspace;
}

ActionContainer& DetachedTerminalWindow::terminalMenu()
{
    return *_terminalMenu;
}

ActionContainer& DetachedTerminalWindow::editMenu()
{
    return *_editMenu;
}

ActionContainer& DetachedTerminalWindow::viewMenu()
{
    return *_viewMenu;
}

void DetachedTerminalWindow::setContextNames(const QString& workspace, const QString& session)
{
    _workspaceName = workspace;
    _sessionName   = session;
    _refreshTitle();
}

int DetachedTerminalWindow::number() const
{
    return _number;
}

void DetachedTerminalWindow::setNumber(int number)
{
    if (number < 2)
    {
        return;
    }

    _number = number;
    _refreshTitle();
}

void DetachedTerminalWindow::allowClose()
{
    _allowClose = true;
}

void DetachedTerminalWindow::closeEvent(QCloseEvent* event)
{
    if (_allowClose)
    {
        QMainWindow::closeEvent(event);
        return;
    }
    event->ignore();
    emit returnRequested();
}

void DetachedTerminalWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && _fullScreenAction)
    {
        const QSignalBlocker blocker(_fullScreenAction);
        _fullScreenAction->setChecked(isFullScreen());
    }
}

bool DetachedTerminalWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (_activeGroup && watched == &_activeGroup->statusBar()
        && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
    {
        QTimer::singleShot(0, this, [this] { _refreshPanelActions(); });
    }
    return QMainWindow::eventFilter(watched, event);
}

void DetachedTerminalWindow::_refreshTitle()
{
    QStringList parts;
    for (const auto& name : {_workspaceName, _sessionName})
    {
        if (!name.isEmpty())
        {
            parts.append(name);
        }
    }
    if (parts.isEmpty())
    {
        parts.append(QCoreApplication::applicationName());
    }
    parts.append(QString::number(_number));
    setWindowTitle(parts.join(" / "));
}

void DetachedTerminalWindow::_createMenus()
{
    _mainMenu.addGroup("main");
    const auto menu = [this](const QString& id, const QString& title, const QStringList& groups) {
        auto item = std::make_unique<MenuActionContainer>(id, title);
        for (const auto& group : groups)
        {
            item->addGroup(group);
        }
        auto* result = item.get();
        _mainMenu.addMenu(item.get(), "main");
        item.release();
        return result;
    };
    _terminalMenu = menu("terminal",
                         tr("&Terminal"),
                         {"allConnections", "connection", "management", "output", "settings", "window"});
    _editMenu     = menu("edit", tr("&Edit"), {"history", "clipboard", "selection"});
    _viewMenu     = menu("view", tr("&View"), {"terminal", "panels", "window"});
    _windowMenu   = menu("window", tr("&Window"), {"navigation"});
}

void DetachedTerminalWindow::_createViewActions(CommandRegistry& commands)
{
    const auto action = [this, &commands](const QString&      id,
                                          const QString&      text,
                                          const QIcon&        icon,
                                          const QKeySequence& shortcut,
                                          const QString&      group) {
        auto* command = commands.find(id);
        if (!command)
        {
            command = commands.create(id, text, icon, shortcut);
        }
        auto* presentation = command->newAction(this);
        presentation->setShortcutContext(Qt::WindowShortcut);
        presentation->setCheckable(true);
        addAction(presentation);
        _viewMenu->addAction(presentation, group);
        return presentation;
    };
    _toolbarAction    = action("View.Toolbar", tr("&Toolbar"), Icons::icon("panel-top"), {}, "panels");
    _statusAction     = action("View.StatusBar", tr("&Status bar"), Icons::icon("panel-bottom"), {}, "panels");
    _fullScreenAction = action("View.FullScreen", tr("&Full screen"), {}, QKeySequence(Qt::Key_F11), "window");
    connect(_toolbarAction, &QAction::toggled, this, [this](bool visible) {
        if (_activeGroup)
        {
            _activeGroup->toolbar().setVisible(visible);
        }
    });
    connect(_statusAction, &QAction::toggled, this, [this](bool visible) {
        if (_activeGroup)
        {
            _activeGroup->statusBar().setVisible(visible);
        }
    });
    connect(_fullScreenAction, &QAction::toggled, this, [this](bool full) {
        if (full)
        {
            showFullScreen();
        }
        else
        {
            showNormal();
        }
    });
}

void DetachedTerminalWindow::_observeGroup(TerminalTabGroup* group)
{
    if (!_activeGroup)
    {
        _setActiveGroup(group);
    }
    group->statusBar().installEventFilter(this);
    connect(
        &group->toolbar(),
        &QToolBar::visibilityChanged,
        this,
        [this] { _refreshPanelActions(); },
        Qt::QueuedConnection);
    connect(group, &QObject::destroyed, this, [this] { _refreshPanelActions(); }, Qt::QueuedConnection);
}

void DetachedTerminalWindow::_setActiveGroup(TerminalTabGroup* group)
{
    _activeGroup = group;
    _refreshPanelActions();
}

void DetachedTerminalWindow::_refreshPanelActions()
{
    if (!_toolbarAction || !_statusAction)
    {
        return;
    }

    _toolbarAction->setEnabled(_activeGroup);
    _statusAction->setEnabled(_activeGroup);
    const QSignalBlocker toolbarBlocker(_toolbarAction);
    const QSignalBlocker statusBlocker(_statusAction);
    _toolbarAction->setChecked(_activeGroup && !_activeGroup->toolbar().isHidden());
    _statusAction->setChecked(_activeGroup && !_activeGroup->statusBar().isHidden());
}
