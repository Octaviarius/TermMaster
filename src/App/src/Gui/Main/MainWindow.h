#pragma once

#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <Termaster/Core/Gui/Action/MenubarActionContainer.h>
#include <Termaster/Core/Gui/Action/ToolbarActionContainer.h>

class WorkspacePage;
class TerminalWorkspace;
class QIcon;
class QTabWidget;
class QStackedWidget;
class TerminalTabGroup;

/** Application shell. Commands and domain operations are wired by App. */
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    static constexpr bool MainToolbarEnabled = false;

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    MenubarActionContainer& mainMenu();
    ToolbarActionContainer& toolbar();
    TerminalWorkspace&      terminalWorkspace();
    /** Primary group convenience for creating new terminal tabs. */
    QTabWidget&   terminalTabs();
    void          showTerminals(bool visible);
    void          setTerminalTab(int            index,
                                 const QString& title,
                                 const QString& detail,
                                 const QIcon&   typeIcon,
                                 const QIcon&   statusIcon);
    void          setWorkspace(const QString& name, const QString& file);
    void          showWorkspaceChoice(const QString& error = {});
    void          setSession(const QString& name, const QString& file, int objects, int links);
    void          showMessage(const QString& message);
    QString       message() const;
    Q_SIGNAL void openSessionRequested();
    Q_SIGNAL void createSessionRequested();
    Q_SIGNAL void openWorkspaceRequested();
    Q_SIGNAL void createWorkspaceRequested();

protected:
    bool event(QEvent* event) override;

private:
    MenubarActionContainer     _mainMenu;
    ToolbarActionContainer     _toolbar;
    WorkspacePage*             _workspacePage     = nullptr;
    TerminalWorkspace*         _terminalWorkspace = nullptr;
    QStackedWidget*            _central           = nullptr;
    QString                    _workspaceName;
    QString                    _sessionName;
    QPointer<TerminalTabGroup> _activeGroup;

    TerminalTabGroup* _messageGroup() const;
    void              _createMenus();
    void              _refreshTitle();
};
