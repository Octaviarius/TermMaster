#include "DetachedTerminalWindow.h"

#include "../Main/EditActions.h"

#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <Termaster/Core/ActionContainer.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <Termaster/Core/Gui/Icons.h>
#include <memory>

DetachedTerminalWindow::DetachedTerminalWindow(CommandRegistry& commands, QWidget* parent) :
    QMainWindow(parent, Qt::Window), _mainMenu(menuBar()), _toolbar(new QToolBar(tr("Terminal"), this)),
    _statusBar(statusBar())
{
    setObjectName("detachedTerminalWindow");
    setWindowTitle(QCoreApplication::applicationName());
    resize(1000, 650);
    _createMenus();
    _toolbar->setObjectName("detachedTerminalToolbar");
    _toolbar->setIconSize(QSize(24, 24));
    _toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(_toolbar);
    _statusBar->showMessage(tr("Ready"));
    _statusBar->installEventFilter(this);
    _createViewActions(commands);
    new EditActions(*this, commands, *_editMenu, this);
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

QToolBar& DetachedTerminalWindow::terminalToolbar()
{
    return *_toolbar;
}

void DetachedTerminalWindow::addStatusAction(QAction* action, const QString& objectName)
{
    auto* button = new QToolButton(this);
    button->setObjectName(objectName);
    button->setAutoRaise(true);
    button->setIconSize(QSize(16, 16));
    button->setDefaultAction(action);
    _statusBar->addPermanentWidget(button);
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
    if (watched == _statusBar.data() && _statusAction
        && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
    {
        const QSignalBlocker blocker(_statusAction);
        _statusAction->setChecked(!_statusBar->isHidden());
    }
    return QMainWindow::eventFilter(watched, event);
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
    _terminalMenu = menu("terminal", tr("&Terminal"), {"connection", "management", "output", "settings", "window"});
    _editMenu     = menu("edit", tr("&Edit"), {"history", "clipboard", "selection"});
    _viewMenu     = menu("view", tr("&View"), {"terminal", "panels", "window"});
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
    _toolbarAction->setChecked(true);
    _statusAction->setChecked(true);
    connect(_toolbarAction, &QAction::toggled, _toolbar, &QWidget::setVisible);
    connect(_toolbar, &QToolBar::visibilityChanged, this, [this] {
        if (_toolbarAction)
        {
            const QSignalBlocker blocker(_toolbarAction);
            _toolbarAction->setChecked(!_toolbar->isHidden());
        }
    });
    connect(_statusAction, &QAction::toggled, _statusBar.data(), &QWidget::setVisible);
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
