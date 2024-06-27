#include "WindowManager.h"

#include "CommandManager.h"

WindowManager::WindowManager()
{

    auto cont = new CommandContainer(this);

    cont->newCommand("File.NewSerial", "New Serial");
    cont->newCommand("File.NewSocket", "New Socket");
    cont->newCommand("File.NewStdio", "New Stdio");
    cont->newCommand("File.ExitApplication", "Exit application");
    cont->newCommand("File.CloseWindow", "Close window");

    cont->newCommand("Settings.General", "General");
    cont->newCommand("Settings.Terminal", "Terminal");
    cont->newCommand("Settings.Plugins", "Plugins");

    cont->newCommand("Control.Term.Connect", "Connect");
    cont->newCommand("Control.Term.Disconnect", "Disconnect");
    cont->newCommand("Control.Term.Close", "Close");
    cont->newCommand("Control.Term.ClearBuffer", "Clear buffer");
    cont->newCommand("Control.Term.ResetConsole", "Reset console");
    cont->newCommand("Control.Term.FullReset", "Full reset");

    cont->newCommand("Info.HomePage", "Home page");
    cont->newCommand("Info.Sources", "Srouces");
    cont->newCommand("Info.HelpIndex", "Help index");
    cont->newCommand("Info.AboutApplication", "About");

    auto& cm = CommandManager::instance();
    cm.registerCommandContainer(cont);
}

MainWindow* WindowManager::currentWindow()
{
    return _currentWindow;
}

QList<MainWindow*> WindowManager::windows()
{
    return _windows;
}

MainWindow* WindowManager::newWindow(PrettyId id, bool show)
{
    auto window = new MainWindow();
    _windows    += window;
    if (show)
    {
        window->show();
    }
    return window;
}
