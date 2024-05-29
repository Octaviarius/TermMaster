#include "core/ModuleManager.h"
#include "core/Registree.h"
#include "core/SettingsManager.h"
#include "modules/Base/BaseModule.h"
#include "modules/Base/BaseModuleApi.h"
#include "modules/Terminal/TerminalModule.h"

#include <QApplication>
#include <QFile>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QVariant>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("TermMaster");

    auto p1 = SettingsManager::application();

    p1.setValue("root/node1", 12345);
    p1.sync();

    ModuleManager::addModuleToPool(new BaseModule());
    ModuleManager::addModuleToPool(new TerminalModule());
    ModuleManager::startupAll();

    auto baseApi = ModuleManager::moduleApi<BaseModuleApi>("Base");

    if (baseApi)
    {
        baseApi->showMainWindow();
    }

    return app.exec();
}
