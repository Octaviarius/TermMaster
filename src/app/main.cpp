#include <QApplication>
#include <QMenu>
#include <QSerialPort>
#include <extensionsystem/ModuleManager.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("TermMaster");

    ModuleManager::instance().scanForBuiltinModules();
    ModuleManager::instance().scanForModules(Path::cwd());
    ModuleManager::instance().startupRequired();

    return app.exec();
}
