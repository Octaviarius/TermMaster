#include "core/CommandManager.h"
#include "core/RandomNames.h"
#include "core/SettingsManager.h"
#include "core/WindowManager.h"

#include <QApplication>
#include <QMap>
#include <string>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("TermMaster");

    SettingsManager::instance();
    RandomNames::instance();
    CommandManager::instance();
    WindowManager::instance();

    WindowManager::instance().newWindow();

    return app.exec();
}
