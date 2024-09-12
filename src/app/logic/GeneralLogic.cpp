#include "GeneralLogic.h"

#include "core/MenuActionContainer.h"
#include "managers/CommandManager.h"
#include "managers/WindowManager.h"
#include "widgets/AboutDialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QUrl>

static const QString _homepageUrl = "https://github.com/Octaviarius/TermMaster";
static const QString _helpUrl     = "https://github.com/Octaviarius/TermMaster/wiki";

GeneralLogic::GeneralLogic()
{
    // connect with window manager
    auto& wm = WindowManager::instance();

    connect(&wm, &WindowManager::windowCreated, this, &GeneralLogic::onWindowCreated);
    connect(&wm, &WindowManager::windowTerminated, this, &GeneralLogic::onWindowTerminated);

    // create commands
    auto& cm = CommandManager::instance();

    cm.createCommand("File.NewWindow", "New window")
        ->setSingleInstance(true)
        .setDefaultShortcut(QKeySequence("Alt+N,Alt+W"))
        .resetShortcut();

    cm.createCommand("File.NewSerial", "New Serial")->setSingleInstance(true);
    cm.createCommand("File.NewSocket", "New Socket")->setSingleInstance(true);
    cm.createCommand("File.NewStdio", "New Stdio")->setSingleInstance(true);
    cm.createCommand("File.ExitApplication", "Exit application")->setSingleInstance(true);
    cm.createCommand("File.CloseWindow", "Close window");

    cm.createCommand("Settings.General", "General")->setSingleInstance(true);
    cm.createCommand("Settings.Terminal", "Terminal")->setSingleInstance(true);
    cm.createCommand("Settings.Plugins", "Plugins")->setSingleInstance(true);

    cm.createCommand("Control.Term.Connect", "Connect");
    cm.createCommand("Control.Term.Disconnect", "Disconnect");
    cm.createCommand("Control.Term.Close", "Close");
    cm.createCommand("Control.Term.ClearBuffer", "Clear buffer");
    cm.createCommand("Control.Term.ResetConsole", "Reset console");
    cm.createCommand("Control.Term.FullReset", "Full reset");

    cm.createCommand("Info.HomePage", "Home page")->setSingleInstance(true);
    cm.createCommand("Info.Sources", "Srouces")->setSingleInstance(true);
    cm.createCommand("Info.HelpIndex", "Help index")->setSingleInstance(true);
    cm.createCommand("Info.AboutApplication", "About")->setSingleInstance(true);

    // init single instance actions

    // File
    QAction* action;

    action = cm.newAction("File.NewWindow");
    QObject::connect(action, &QAction::triggered, [this, &wm]() { wm.newWindow(); });

    action = cm.newAction("File.ExitApplication");
    QObject::connect(action, &QAction::triggered, [this]() { QApplication::exit(); });

    // Settings
    action = cm.newAction("Settings.General");
    QObject::connect(action, &QAction::triggered, [this]() { QApplication::exit(); });

    // Info
    action = cm.newAction("Info.HomePage");
    QObject::connect(action, &QAction::triggered, [this]() { QDesktopServices::openUrl(QUrl(_homepageUrl)); });

    // Info
    action = cm.newAction("Info.HelpIndex");
    QObject::connect(action, &QAction::triggered, [this]() { QDesktopServices::openUrl(QUrl(_helpUrl)); });

    action = cm.newAction("Info.AboutApplication");
    QObject::connect(action, &QAction::triggered, [this]() {
        AboutDialog dialog;
        dialog.exec();
    });
}

void GeneralLogic::onWindowCreated(MainWindow* window)
{
    QAction* action;

    auto& cm = CommandManager::instance();

    window->mainMenu()->addGroup("");

    auto fileMenu = new MenuActionContainer("File");
    window->mainMenu()->addMenu(fileMenu, "");
    {
        fileMenu->addGroup("New");
        fileMenu->addGroup("Open");
        fileMenu->addGroup("Save");
        fileMenu->addGroup("Exit");

        auto recentSessionMenu = new MenuActionContainer("RecentSession", "Recent sessions");
        fileMenu->addMenu(recentSessionMenu, "Open");

        auto recentTerminalMenu = new MenuActionContainer("RecentTerminal", "Recent terminals");
        fileMenu->addMenu(recentTerminalMenu, "Open");

        fileMenu->addAction(action = cm.newAction("File.NewWindow", window), "New");

        fileMenu->addAction(action = cm.newAction("File.CloseWindow", window), "Exit");
        QObject::connect(action, &QAction::triggered, window, &MainWindow::close);

        fileMenu->addAction(action = cm.newAction("File.ExitApplication", window), "Exit");
    }

    auto settingsMenu = new MenuActionContainer("Settings");
    window->mainMenu()->addMenu(settingsMenu, "");

    auto controlMenu = new MenuActionContainer("Control");
    window->mainMenu()->addMenu(controlMenu, "");

    auto infoMenu = new MenuActionContainer("Info");
    window->mainMenu()->addMenu(infoMenu, "");
    {
        infoMenu->addGroup("General");
        infoMenu->addGroup("About");
        infoMenu->addAction(cm.newAction("Info.HomePage", window), "General");
        infoMenu->addAction(cm.newAction("Info.Source", window), "General");
        infoMenu->addAction(cm.newAction("Info.HelpIndex", window), "General");
        infoMenu->addAction(cm.newAction("Info.AboutApplication", window), "About");
    }
}

void GeneralLogic::onWindowTerminated(MainWindow* window)
{
}
