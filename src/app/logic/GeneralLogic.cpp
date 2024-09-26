#include "GeneralLogic.h"

#include "core/MenuActionContainer.h"
#include "managers/CommandManager.h"
#include "managers/SettingsManager.h"
#include "managers/WindowManager.h"
#include "widgets/AboutDialog.h"
#include "widgets/SettingsDialog.h"
#include "widgets/config/ConfigSerialTerminalWidget.h"

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

    //-------- create commands
    auto& cm = CommandManager::instance();

    cm.createCommand("File.NewWindow", "New window")
        ->setSingleInstance(true)
        .setDefaultShortcut(QKeySequence("Ctrl+Alt+W"));

    cm.createCommand("File.NewSerial", "New Serial")
        ->setSingleInstance(true)
        .setDefaultShortcut(QKeySequence("Alt+N,Alt+S"));

    cm.createCommand("File.NewSocket", "New Socket")
        ->setSingleInstance(true)
        .setDefaultShortcut(QKeySequence("Alt+N,Alt+K"));

    cm.createCommand("File.NewStdio", "New Stdio")
        ->setSingleInstance(true)
        .setDefaultShortcut(QKeySequence("Alt+N,Alt+I"));

    cm.createCommand("File.ExitApplication", "Exit application")
        ->setSingleInstance(true)
        .setDefaultShortcut(QKeySequence("Ctrl+Alt+Q"));

    cm.createCommand("File.CloseWindow", "Close window")->setDefaultShortcut(QKeySequence("Ctrl+Alt+X"));

    cm.createCommand("Settings.General", "General settings")->setSingleInstance(true);
    cm.createCommand("Settings.Terminal", "Terminal settings")->setSingleInstance(true);
    cm.createCommand("Settings.Plugins", "Plugins")->setSingleInstance(true);

    cm.createCommand("Control.Term.Connect", "Connect");
    cm.createCommand("Control.Term.Disconnect", "Disconnect");
    cm.createCommand("Control.Term.Close", "Close");
    cm.createCommand("Control.Term.ClearBuffer", "Clear buffer");
    cm.createCommand("Control.Term.ResetConsole", "Reset console");
    cm.createCommand("Control.Term.FullReset", "Full reset");

    cm.createCommand("Info.HomePage", "Home page")->setSingleInstance(true);
    cm.createCommand("Info.Sources", "Sources")->setSingleInstance(true);
    cm.createCommand("Info.HelpIndex", "Help index")->setSingleInstance(true);
    cm.createCommand("Info.AboutApplication", "About")->setSingleInstance(true);

    cm.resetShortcuts();

    //-------- init single instance actions

    // File
    QAction* action;

    action = cm.newAction("File.NewWindow");
    QObject::connect(action, &QAction::triggered, this, [this]() { WindowManager::instance().newWindow(); });

    action = cm.newAction("File.ExitApplication");
    QObject::connect(action, &QAction::triggered, [this]() { QApplication::exit(); });

    // Settings
    action = cm.newAction("Settings.General");
    QObject::connect(action, &QAction::triggered, [this]() {
        SettingsDialog dialog(SettingsManager::instance().configContainer("General"));
        dialog.exec();
    });

    // Info
    action = cm.newAction("Info.HomePage");
    QObject::connect(action, &QAction::triggered, [this]() { QDesktopServices::openUrl(QUrl(_homepageUrl)); });

    // Info
    action = cm.newAction("Info.HelpIndex");
    QObject::connect(action, &QAction::triggered, [this]() { QDesktopServices::openUrl(QUrl(_helpUrl)); });

    action = cm.newAction("Info.AboutApplication");
    QObject::connect(action, &QAction::triggered, this, [this]() {
        AboutDialog dialog;
        dialog.exec();
    });

    //-------- Config containers
    auto& cfgMan = SettingsManager::instance();
    {
        auto cfgRoot = new ConfigContainer("General");
        cfgMan.addConfigContainer(cfgRoot);

        auto cfgConnections = new ConfigContainer("Connections", "", "", QIcon(":/icons/cat-connections.svg"));
        {
            cfgRoot->addContainer(cfgConnections);

            auto cfgTermSerial = new ConfigContainer("Serial", "", "");
            cfgTermSerial->addWidgetConstructor(IConfigWidget::getWidgetConstructor<ConfigSerialTerminalWidget>());
            cfgConnections->addContainer(cfgTermSerial);

            auto cfgTermSock = new ConfigContainer("Socket", "", "");
            cfgConnections->addContainer(cfgTermSock);

            auto cfgTermStdio = new ConfigContainer("Stdio", "", "");
            cfgConnections->addContainer(cfgTermStdio);
        }

        auto cfgTerminal = new ConfigContainer("Terminal", "", "", QIcon(":/icons/cat-terminal.svg"));
        cfgRoot->addContainer(cfgTerminal);

        auto cfgKeyboard = new ConfigContainer("Keyboard", "", "", QIcon(":/icons/cat-keyboard.svg"));
        cfgRoot->addContainer(cfgKeyboard);

        auto cfgMouse = new ConfigContainer("Mouse", "", "", QIcon(":/icons/cat-mouse.svg"));
        cfgRoot->addContainer(cfgMouse);

        auto cfgShortcuts = new ConfigContainer("Shortcuts", "", "", QIcon(":/icons/cat-shortcuts.svg"));
        cfgRoot->addContainer(cfgShortcuts);

        auto cfgDialogs = new ConfigContainer("Dialogs", "", "", QIcon(":/icons/cat-dialogs.svg"));
        cfgRoot->addContainer(cfgDialogs);

        auto cfgAppearance = new ConfigContainer("Appearance", "", "", QIcon(":/icons/cat-appearance.svg"));
        cfgRoot->addContainer(cfgAppearance);
    }
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
    {
        settingsMenu->addGroup("General");
        settingsMenu->addGroup("Terminal");
        settingsMenu->addGroup("Other");

        settingsMenu->addAction(action = cm.newAction("Settings.General"), "General");

        settingsMenu->addAction(action = cm.newAction("Settings.Terminal"), "Terminal");
        QObject::connect(action, &QAction::triggered, [this]() {
            SettingsDialog dialog(SettingsManager::instance().configContainer("Terminal"));
            dialog.exec();
        });

        window->mainMenu()->addMenu(settingsMenu, "");
    }

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

void openGeneralSettingsDialog()
{
}
void openTerminalSettingsDialog()
{
}
