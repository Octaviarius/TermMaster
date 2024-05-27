#pragma once

#include "Module.h"

class ModuleManager
{
public:
    ModuleManager()                     = delete;
    ModuleManager(const ModuleManager&) = delete;

    static bool startupModule(QString moduleName);
    static bool startupModules(QStringList moduleNames);
    static bool startupAll();
    static bool shutdownModule(QString moduleName);
    static bool shutdownModules(QStringList moduleNames);

    static bool isModuledStarted(QString moduleName);

    static const ModuleInfo moduleInfo(QString moduleName);
    static ModuleApi*       moduleApi(QString moduleName);

    static QStringList startupModulesList();
    static QStringList shutdownModulesList();

    static QStringList modulesPool();
    static void        addModuleToPool(Module* module);
    static void        addModuleToPool(QString path);
    static void        addModulesToPool(QString dirPath, bool recursively = true);

    static QStringList unresolvedDependencies(QString moduleName);
    static QStringList dependantsList(QString moduleName);
};
