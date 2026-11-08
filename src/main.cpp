#include <QApplication>
#include <QDialog>
#include <Termaster/Core/ApplicationPaths.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/Version.h>
#include <Termaster/Core/gui/plugin/PluginManagerGui.h>
#include <Termaster/Core/plugin/PluginManager.h>
#include <memory>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName("Termaster");
    QCoreApplication::setApplicationVersion(Version::shortVersion);

    ApplicationPaths         paths;
    Settings                 settings(paths.configFile("plugins"));
    PluginManager            manager(settings, paths.pluginsDirectory().toString());
    std::unique_ptr<QDialog> window(createPluginManagerWindow(manager));
    window->show();

    // The controller starts and refreshes the manager on the GUI event loop.
    return application.exec();
}
