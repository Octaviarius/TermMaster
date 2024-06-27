#include "MainWindow.h"

#include "core/CommandManager.h"
#include "core/SettingsManager.h"
#include "ui_MainWindow.h"

#include <QKeySequenceEdit>
#include <QLabel>
#include <QShortcut>

//===================================================================

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // config statusbar
    auto lblVersion = new QLabel();
    lblVersion->setFrameStyle(QFrame::Sunken);
    lblVersion->setText(SettingsManager::shortVersion);
    ui->statusbar->addWidget(lblVersion);

    auto& cm = CommandManager::instance();

    // add to menu
    _mainMenuContainer = new MenubarActionContainer(ui->menubar);

    _mainMenuContainer->addGroup("");

    auto fileMenu = new MenuActionContainer("File");
    _mainMenuContainer->addContainer(fileMenu, "");
    {
        fileMenu->addGroup("Open");
        fileMenu->addGroup("Save");
        fileMenu->addGroup("Exit");

        auto recentSessionMenu = new MenuActionContainer("RecentSession", "Recent sessions");
        fileMenu->addContainer(recentSessionMenu, "Open");

        auto recentTerminalMenu = new MenuActionContainer("RecentTerminal", "Recent terminals");
        fileMenu->addContainer(recentTerminalMenu, "Open");

        fileMenu->addAction(cm.newAction("File.CloseWindow", this), "Exit");
        fileMenu->addAction(cm.newAction("File.ExitApplication", this), "Exit");
    }

    auto settingsMenu = new MenuActionContainer("Settings");
    _mainMenuContainer->addContainer(settingsMenu, "");

    auto controlMenu = new MenuActionContainer("Control");
    _mainMenuContainer->addContainer(controlMenu, "");

    auto infoMenu = new MenuActionContainer("Info");
    _mainMenuContainer->addContainer(infoMenu, "");
    {
        infoMenu->addGroup("General");
        infoMenu->addGroup("About");
        infoMenu->addAction(cm.newAction("Info.HomePage", this), "General");
        infoMenu->addAction(cm.newAction("Info.Source", this), "General");
        infoMenu->addAction(cm.newAction("Info.HelpIndex", this), "General");
        infoMenu->addAction(cm.newAction("Info.AboutApplication", this), "About");
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

MenubarActionContainer* MainWindow::mainMenu()
{
    return _mainMenuContainer;
}
