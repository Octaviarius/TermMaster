#include <QApplication>
#include <QMenu>
#include <QSerialPort>
#include <extensionsystem/ModuleManager.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("TermMaster");

    ModuleManager::scanForModules(Path::cwd(), false);

    return app.exec();
}
