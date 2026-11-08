#include "gui/main/MainWindow.h"

#include <QDialog>
#include <QPointer>
#include <QStatusBar>
#include <Termaster/App/App.h>
#include <Termaster/Core/ApplicationPaths.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/Version.h>
#include <Termaster/Core/gui/action/MenuActionContainer.h>
#include <Termaster/Core/gui/plugin/PluginManagerGui.h>
#include <Termaster/Core/plugin/PluginManager.h>
#include <stdexcept>

struct App::Data
{
    ApplicationPaths            paths;
    Settings                    settings;
    Settings                    pluginSettings;
    CommandRegistry             commands;
    std::unique_ptr<MainWindow> window;
    QPointer<QDialog>           pluginWindow;
    // Destroy the manager before views/commands even if shutdown must be retried.
    PluginManager plugins;

    explicit Data(const Options& options) :
        paths(_paths(options)), settings(paths.configFile("application")), pluginSettings(paths.configFile("plugins")),
        window(std::make_unique<MainWindow>()), plugins(pluginSettings, paths.pluginsDirectory().toString())
    {
    }

private:
    static ApplicationPaths _paths(const Options& options)
    {
        ApplicationPaths paths;
        if (!options.dataDirectory.isEmpty())
        {
            paths.setDataDirectory(Path(options.dataDirectory));
        }
        if (!options.pluginsDirectory.isEmpty())
        {
            paths.setPluginsDirectory(Path(options.pluginsDirectory));
        }
        return paths;
    }
};

App::App(int& argc, char** argv) : App(argc, argv, Options {})
{
}

App::App(int& argc, char** argv, const Options& options) : QApplication(argc, argv)
{
    setApplicationName("Termaster");
    setApplicationVersion(Version::shortVersion);

    _data = std::make_unique<Data>(options);
    _setupActions();
    connect(&_data->plugins,
            &PluginManager::errorOccurred,
            _data->window.get(),
            [window = _data->window.get()](const QString& error) { window->statusBar()->showMessage(error); });
}

App::~App() = default;

int App::run()
{
    _data->window->show();
    QString error;
    if (!_data->settings.load(&error))
    {
        _data->window->statusBar()->showMessage(error);
    }
    if (!_data->plugins.start(&error))
    {
        _data->window->statusBar()->showMessage(error);
    }
    return exec();
}

const ApplicationPaths& App::paths() const
{
    return _data->paths;
}

Settings& App::settings()
{
    return _data->settings;
}

CommandRegistry& App::commands()
{
    return _data->commands;
}

QMainWindow& App::mainWindow()
{
    return *_data->window;
}

MenubarActionContainer& App::mainMenu()
{
    return _data->window->mainMenu();
}

ToolbarActionContainer& App::toolbar()
{
    return _data->window->toolbar();
}

void App::_setupActions()
{
    auto* plugins = commands().create("Application.Plugins", tr("&Plugins…"));
    auto* quit    = commands().create("Application.Exit", tr("E&xit"), {}, QKeySequence::Quit);
    if (!plugins || !quit)
    {
        throw std::logic_error("Cannot register built-in commands");
    }
    auto* pluginAction = plugins->newAction(_data->window.get());
    auto* quitAction   = quit->newAction(_data->window.get());
    quitAction->setMenuRole(QAction::QuitRole);
    connect(pluginAction, &QAction::triggered, this, &App::_showPluginManager);
    connect(quitAction, &QAction::triggered, this, []() { QCoreApplication::quit(); });

    auto file  = std::make_unique<MenuActionContainer>("file", tr("&File"));
    auto tools = std::make_unique<MenuActionContainer>("tools", tr("&Tools"));
    if (!mainMenu().addGroup("application") || !toolbar().addGroup("application") || !file->addGroup("application")
        || !tools->addGroup("management") || !file->addAction(quitAction, "application")
        || !tools->addAction(pluginAction, "management") || !toolbar().addAction(pluginAction, "application"))
    {
        throw std::logic_error("Cannot assemble application actions");
    }
    if (!mainMenu().addMenu(file.get(), "application"))
    {
        throw std::logic_error("Cannot attach File menu");
    }
    file.release();
    if (!mainMenu().addMenu(tools.get(), "application"))
    {
        throw std::logic_error("Cannot attach Tools menu");
    }
    tools.release();
}

void App::_showPluginManager()
{
    if (!_data->pluginWindow)
    {
        _data->pluginWindow = createPluginManagerWindow(_data->plugins, _data->window.get());
        _data->pluginWindow->setAttribute(Qt::WA_DeleteOnClose);
    }
    _data->pluginWindow->show();
    _data->pluginWindow->raise();
    _data->pluginWindow->activateWindow();
}
