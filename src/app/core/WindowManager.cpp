#include "WindowManager.h"

#include "CommandManager.h"
#include "widgets/AboutDialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QUrl>

static QString _homepageUrl = "https://github.com/Octaviarius/TermMaster";

static void _initNewWindow(MainWindow* window);

WindowManager::WindowManager() : _currentId(0)
{
    auto     cont = new CommandContainer(this);
    QAction* action;

    cont->newCommand("File.NewWindow", "New window")->setSingleInstance(true);

    cont->newCommand("File.NewSerial", "New Serial")->setSingleInstance(true);
    cont->newCommand("File.NewSocket", "New Socket")->setSingleInstance(true);
    cont->newCommand("File.NewStdio", "New Stdio")->setSingleInstance(true);
    cont->newCommand("File.ExitApplication", "Exit application")->setSingleInstance(true);
    cont->newCommand("File.CloseWindow", "Close window");

    cont->newCommand("Settings.General", "General")->setSingleInstance(true);
    cont->newCommand("Settings.Terminal", "Terminal")->setSingleInstance(true);
    cont->newCommand("Settings.Plugins", "Plugins")->setSingleInstance(true);

    cont->newCommand("Control.Term.Connect", "Connect");
    cont->newCommand("Control.Term.Disconnect", "Disconnect");
    cont->newCommand("Control.Term.Close", "Close");
    cont->newCommand("Control.Term.ClearBuffer", "Clear buffer");
    cont->newCommand("Control.Term.ResetConsole", "Reset console");
    cont->newCommand("Control.Term.FullReset", "Full reset");

    cont->newCommand("Info.HomePage", "Home page")->setSingleInstance(true);
    cont->newCommand("Info.Sources", "Srouces")->setSingleInstance(true);
    cont->newCommand("Info.HelpIndex", "Help index")->setSingleInstance(true);
    cont->newCommand("Info.AboutApplication", "About")->setSingleInstance(true);

    auto& cm = CommandManager::instance();
    cm.registerCommandContainer(cont);

    action = cm.newAction("File.NewWindow");
    QObject::connect(action, &QAction::triggered, [this]() { newWindow(); });

    action = cm.newAction("File.ExitApplication");
    QObject::connect(action, &QAction::triggered, [this]() { QApplication::exit(); });

    action = cm.newAction("Info.HomePage");
    QObject::connect(action, &QAction::triggered, [this]() { QDesktopServices::openUrl(QUrl(_homepageUrl)); });

    action = cm.newAction("Info.AboutApplication");
    QObject::connect(action, &QAction::triggered, [this]() {
        AboutDialog dialog;
        dialog.exec();
    });
}

MainWindow* WindowManager::currentWindow()
{
    return MainWindow::currentWindow();
}

QList<MainWindow*> WindowManager::windows()
{
    return _windows;
}

MainWindow* WindowManager::newWindow(bool show)
{
    auto onWindowClosed = [this](QObject* obj) {
        auto window = dynamic_cast<MainWindow*>(obj);
        _windows.removeOne(window);
        emit windowTerminated(window);
    };

    auto window = new MainWindow(RandomNames::instance().getId(_currentId++));
    _windows    += window;

    connect(window, &QMainWindow::destroyed, this, onWindowClosed);

    _initNewWindow(window);
    emit windowCreated(window);

    if (show)
    {
        window->show();
    }
    return window;
}

static void _initNewWindow(MainWindow* window)
{
    QAction* action;

    auto& cm = CommandManager::instance();

    window->mainMenu()->addGroup("");

    auto fileMenu = new MenuActionContainer("File");
    window->mainMenu()->addContainer(fileMenu, "");
    {
        fileMenu->addGroup("New");
        fileMenu->addGroup("Open");
        fileMenu->addGroup("Save");
        fileMenu->addGroup("Exit");

        auto recentSessionMenu = new MenuActionContainer("RecentSession", "Recent sessions");
        fileMenu->addContainer(recentSessionMenu, "Open");

        auto recentTerminalMenu = new MenuActionContainer("RecentTerminal", "Recent terminals");
        fileMenu->addContainer(recentTerminalMenu, "Open");

        fileMenu->addAction(action = cm.newAction("File.NewWindow", window), "New");

        fileMenu->addAction(action = cm.newAction("File.CloseWindow", window), "Exit");
        QObject::connect(action, &QAction::triggered, window, &MainWindow::close);

        fileMenu->addAction(action = cm.newAction("File.ExitApplication", window), "Exit");
    }

    auto settingsMenu = new MenuActionContainer("Settings");
    window->mainMenu()->addContainer(settingsMenu, "");

    auto controlMenu = new MenuActionContainer("Control");
    window->mainMenu()->addContainer(controlMenu, "");

    auto infoMenu = new MenuActionContainer("Info");
    window->mainMenu()->addContainer(infoMenu, "");
    {
        infoMenu->addGroup("General");
        infoMenu->addGroup("About");
        infoMenu->addAction(cm.newAction("Info.HomePage", window), "General");
        infoMenu->addAction(cm.newAction("Info.Source", window), "General");
        infoMenu->addAction(cm.newAction("Info.HelpIndex", window), "General");
        infoMenu->addAction(cm.newAction("Info.AboutApplication", window), "About");
    }
}
