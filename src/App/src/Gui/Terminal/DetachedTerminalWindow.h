#pragma once

#include <QMainWindow>
#include <QPointer>
#include <Termaster/Core/Gui/Action/MenubarActionContainer.h>

class QAction;
class QCloseEvent;
class QToolBar;
class QStatusBar;
class ActionContainer;
class CommandRegistry;

/** Native detached presentation shell. The GUI-thread owner supplies the terminal
 * panel, domain actions and persistence. Commands outlive this window; each action
 * presentation and editing controller belongs to this window.
 */
class DetachedTerminalWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit DetachedTerminalWindow(CommandRegistry& commands, QWidget* parent = nullptr);

    ActionContainer& terminalMenu();
    ActionContainer& editMenu();
    ActionContainer& viewMenu();
    QToolBar&        terminalToolbar();
    void             addStatusAction(QAction* action, const QString& objectName);
    void             allowClose();
    Q_SIGNAL void    returnRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    MenubarActionContainer _mainMenu;
    ActionContainer*       _terminalMenu = nullptr;
    ActionContainer*       _editMenu     = nullptr;
    ActionContainer*       _viewMenu     = nullptr;
    QToolBar*              _toolbar      = nullptr;
    QPointer<QStatusBar>   _statusBar;
    QPointer<QAction>      _toolbarAction;
    QPointer<QAction>      _statusAction;
    QPointer<QAction>      _fullScreenAction;
    bool                   _allowClose = false;

    void _createMenus();
    void _createViewActions(CommandRegistry& commands);
};
