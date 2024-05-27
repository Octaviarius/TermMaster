#include "MainWindow.h"
#include "core/ModuleManager.h"
#include "core/Registree.h"
#include "modules/Base/BaseModule.h"
#include "modules/Terminal/TerminalModule.h"

#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QSharedPointer>
#include <QVariant>
#include <yaml-cpp/yaml.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("TermMaster");

    {
        QSettings settings("settings.yaml", QSettings::Format::IniFormat);

        settings.beginWriteArray("root/node1");
        settings.setValue("3", 222);
        settings.setValue("7", 333);
        settings.setValue("10", "AnyValue");
        settings.endArray();


        settings.sync();

        settings.beginReadArray("root/node1");
        auto a = settings.value("0").toInt();
        auto b = settings.value("3").toInt();
        auto c = settings.value("7").toInt();
        settings.endArray();
    }

    ModuleManager::addModuleToPool(new TerminalModule());
    ModuleManager::addModuleToPool(new BaseModule());
    ModuleManager::startupAll();

    return app.exec();
}
