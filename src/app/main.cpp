#include <QApplication>
#include <QMap>
#include <core/RandomNames.h>
#include <logic/GeneralLogic.h>
#include <managers/CommandManager.h>
#include <managers/ConfigManager.h>
#include <managers/WindowManager.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Terminal Master");

    ConfigManager::initialize();
    RandomNames::initialize();
    CommandManager::initialize();
    WindowManager::initialize();

    GeneralLogic::initialize();

    WindowManager::instance().newWindow();

    return app.exec();
}
