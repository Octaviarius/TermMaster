#pragma once

#include <QMainWindow>
#include <Termaster/Core/Gui/Action/MenubarActionContainer.h>
#include <Termaster/Core/Gui/Action/ToolbarActionContainer.h>

class WorkspacePage;
class QIcon;
class QTabWidget;
class QStackedWidget;

/** Application shell. Commands and domain operations are wired by App. */
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    MenubarActionContainer& mainMenu();
    ToolbarActionContainer& toolbar();
    QTabWidget&             terminalTabs();
    void                    showTerminals(bool visible);
    void                    setTerminalStatusIcon(int index, const QIcon& icon);
    void                    setWorkspace(const QString& name, const QString& file);
    void                    showWorkspaceChoice(const QString& error = {});
    void                    setSession(const QString& name, const QString& file, int objects, int links);
    Q_SIGNAL void           openSessionRequested();
    Q_SIGNAL void           createSessionRequested();
    Q_SIGNAL void           openWorkspaceRequested();
    Q_SIGNAL void           createWorkspaceRequested();

private:
    MenubarActionContainer _mainMenu;
    ToolbarActionContainer _toolbar;
    WorkspacePage*         _workspacePage;
    QTabWidget*            _terminalTabs;
    QStackedWidget*        _central;

    void _createMenus();
};
