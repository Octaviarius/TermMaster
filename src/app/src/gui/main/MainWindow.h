#pragma once

#include <QMainWindow>
#include <Termaster/Core/gui/action/MenubarActionContainer.h>
#include <Termaster/Core/gui/action/ToolbarActionContainer.h>

/** Application shell. Commands and domain operations are wired by App. */
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    MenubarActionContainer& mainMenu();
    ToolbarActionContainer& toolbar();

private:
    MenubarActionContainer _mainMenu;
    ToolbarActionContainer _toolbar;
};
