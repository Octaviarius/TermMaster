#include <QApplication>
#include <QMap>
#include <core/RandomNames.h>
#include <logic/GeneralLogic.h>
#include <managers/CommandManager.h>
#include <managers/SettingsManager.h>
#include <managers/TerminalManager.h>
#include <managers/WindowManager.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Terminal Master");

    SettingsManager::initialize();
    RandomNames::initialize();
    CommandManager::initialize();
    WindowManager::initialize();
    TerminalManager::initialize();

    GeneralLogic::initialize();

    auto recentSessions = WindowManager::instance().recentSessions();

    return app.exec();
}
