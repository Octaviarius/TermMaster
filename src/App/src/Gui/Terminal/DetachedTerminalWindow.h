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
    Q_SIGNAL void    moveUpdated(const QPoint& globalPosition);
    Q_SIGNAL void    moveFinished(const QPoint& globalPosition, bool cancelled);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

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
#ifdef Q_OS_WIN
    QRect   _nativeMoveFrame;
    bool    _nativeMoveActive     = false;
    bool    _nativeMoveChanged    = false;
    bool    _nativeMoveCancelled  = false;
    quint64 _nativeMoveGeneration = 0;
#endif

    void _createMenus();
    void _createViewActions(CommandRegistry& commands);
};
