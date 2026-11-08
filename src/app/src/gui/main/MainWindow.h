#pragma once

#include <QMainWindow>
#include <Termaster/Core/gui/action/MenubarActionContainer.h>
#include <Termaster/Core/gui/action/ToolbarActionContainer.h>

class WorkspacePage;

/** Application shell. Commands and domain operations are wired by App. */
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    MenubarActionContainer& mainMenu();
    ToolbarActionContainer& toolbar();
    void                    setWorkspace(const QString& name, const QString& file);
    void                    showWorkspaceChoice(const QString& error = {});
    Q_SIGNAL void           openWorkspaceRequested();
    Q_SIGNAL void           createWorkspaceRequested();

private:
    MenubarActionContainer _mainMenu;
    ToolbarActionContainer _toolbar;
    WorkspacePage*         _workspacePage;

    void _createMenus();
};
