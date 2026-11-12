#pragma once

#include <QMainWindow>
#include <QPointer>
#include <Termaster/Core/Gui/Action/MenubarActionContainer.h>

class QAction;
class QCloseEvent;
class ActionContainer;
class CommandRegistry;
class TerminalWorkspace;
class TerminalTabGroup;

/** Native presentation shell. Terminal pages and their controls belong to groups;
 * the GUI-thread controller owns domain actions and transfer operations.
 */
class DetachedTerminalWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit DetachedTerminalWindow(CommandRegistry& commands, QWidget* parent = nullptr);
    ~DetachedTerminalWindow() override;

    TerminalWorkspace& terminalWorkspace();
    ActionContainer&   terminalMenu();
    ActionContainer&   editMenu();
    ActionContainer&   viewMenu();
    void               setContextNames(const QString& workspace, const QString& session);
    int                number() const;
    void               setNumber(int number);
    void               allowClose();
    Q_SIGNAL void      returnRequested();
    Q_SIGNAL void      mainWindowRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    MenubarActionContainer     _mainMenu;
    ActionContainer*           _terminalMenu      = nullptr;
    ActionContainer*           _editMenu          = nullptr;
    ActionContainer*           _viewMenu          = nullptr;
    ActionContainer*           _windowMenu        = nullptr;
    TerminalWorkspace*         _terminalWorkspace = nullptr;
    QPointer<TerminalTabGroup> _activeGroup;
    QPointer<QAction>          _toolbarAction;
    QPointer<QAction>          _statusAction;
    QPointer<QAction>          _fullScreenAction;
    QString                    _workspaceName;
    QString                    _sessionName;
    int                        _number     = 2;
    bool                       _allowClose = false;

    void _createMenus();
    void _refreshTitle();
    void _createViewActions(CommandRegistry& commands);
    void _observeGroup(TerminalTabGroup* group);
    void _setActiveGroup(TerminalTabGroup* group);
    void _refreshPanelActions();
};
