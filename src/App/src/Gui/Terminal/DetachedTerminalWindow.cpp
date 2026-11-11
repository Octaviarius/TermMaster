#include "DetachedTerminalWindow.h"

#include "../Main/EditActions.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <Termaster/Core/ActionContainer.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <Termaster/Core/Gui/Icons.h>
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

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

bool DetachedTerminalWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" && message)
    {
        const auto* native = static_cast<const MSG*>(message);
        switch (native->message)
        {
            case WM_ENTERSIZEMOVE:
                ++_nativeMoveGeneration;
                _nativeMoveCancelled = false;
                _nativeMoveFrame     = frameGeometry();
                _nativeMoveActive    = true;
                _nativeMoveChanged   = false;
                break;
            case WM_MOVING:
                if (_nativeMoveActive)
                {
                    _nativeMoveChanged = true;
                    const QPointer<DetachedTerminalWindow> self(this);
                    emit                                   moveUpdated(QCursor::pos());
                    if (!self)
                    {
                        if (result)
                        {
                            *result = 0;
                        }
                        return true;
                    }
                }
                break;
            case WM_EXITSIZEMOVE:
                if (_nativeMoveActive)
                {
                    _nativeMoveActive = false;
                    if (_nativeMoveChanged)
                    {
                        _nativeMoveChanged                                    = false;
                        const QRect                            startFrame     = _nativeMoveFrame;
                        const QPoint                           globalPosition = QCursor::pos();
                        const QPointer<DetachedTerminalWindow> self(this);
                        const quint64                          generation = _nativeMoveGeneration;
                        QTimer::singleShot(0, this, [self, startFrame, globalPosition, generation] {
                            if (!self || self->_nativeMoveGeneration != generation)
                            {
                                return;
                            }
                            const QRect finalFrame = self->frameGeometry();
                            const bool  cancelled  = self->_nativeMoveCancelled || finalFrame == startFrame
                                                  || (finalFrame.topLeft() - startFrame.topLeft()).manhattanLength()
                                                         < QApplication::startDragDistance();
                            emit        self->moveFinished(globalPosition, cancelled);
                        });
                    }
                }
                break;
            case WM_CANCELMODE:
                _nativeMoveCancelled = true;
                if (_nativeMoveActive)
                {
                    _nativeMoveActive = false;
                    if (_nativeMoveChanged)
                    {
                        _nativeMoveChanged                                    = false;
                        const QPoint                           globalPosition = QCursor::pos();
                        const QPointer<DetachedTerminalWindow> self(this);
                        const quint64                          generation = _nativeMoveGeneration;
                        QTimer::singleShot(0, this, [self, globalPosition, generation] {
                            if (self && self->_nativeMoveGeneration == generation)
                            {
                                emit self->moveFinished(globalPosition, true);
                            }
                        });
                    }
                }
                break;
            default:
                break;
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
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
