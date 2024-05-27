#include "ModuleManager.h"

#include <QDebug>
#include <QMap>
#include <QSet>
#include <QtGlobal>

static QMap<QString, Module*>    _modules;
static QMap<QString, QStringSet> _dependants;

bool ModuleManager::startupModule(QString moduleName)
{
    bool result = true;

    if (!_modules.contains(moduleName))
    {
        qCritical() << QString("Module '%1' is not found").arg(moduleName);
        result = false;
    }
    else
    {
        auto module = _modules[moduleName];

        if (!module->isStartedUp())
        {
            auto unresolved = unresolvedDependencies(moduleName);

            if (unresolved.count() > 0)
            {
                qCritical() << QString("Module '%1' has unresolved dependencies: %2")
                                   .arg(moduleName)
                                   .arg(unresolved.join(", "));
                result = false;
            }
            else
            {
                for (const auto& depName : module->dependecies())
                {
                    if (!startupModule(depName))
                    {
                        qCritical() << QString("Module '%1' is not found").arg(moduleName);
                        result = false;
                    }
                }
            }

            if (result)
            {
                result = module->startup();

                if (result)
                {
                    qInfo() << QString("Module '%1' is started").arg(moduleName);
                }
            }
        }
    }

    return result;
}

bool ModuleManager::startupModules(QStringList moduleNames)
{
    bool result = true;

    for (const auto& modName : moduleNames)
    {
        result = startupModule(modName);
        if (!result)
        {
            break;
        }
    }

    return result;
}

bool ModuleManager::startupAll()
{
    return startupModules(_modules.keys());
}

bool ModuleManager::shutdownModule(QString moduleName)
{
    bool result = true;

    auto module = _modules.value(moduleName);

    if (!module)
    {
        result = false;
    }
    else if (module->isStartedUp())
    {
        // get dependants and shutdown them by chain
        for (const auto& depName : dependantsList(moduleName))
        {
            result = shutdownModule(depName);

            if (!result)
            {
                qCritical() << QString("Failed to shutdown dependant module '%1'").arg(depName);
                break;
            }
        }

        if (result)
        {
            result = module->shutdown();
        }
    }

    return result;
}

bool ModuleManager::shutdownModules(QStringList moduleNames)
{
    bool result = true;

    for (const auto& modName : moduleNames)
    {
        result = startupModule(modName);
        if (!result)
        {
            break;
        }
    }

    return result;
}

bool ModuleManager::isModuledStarted(QString moduleName)
{
    auto module = _modules.value(moduleName);
    if (module)
    {
        return module->isStartedUp();
    }
    else
    {
        return false;
    }
}

QStringList ModuleManager::startupModulesList()
{
    QStringList result;

    for (auto module : _modules.values())
    {
        if (module->isStartedUp())
        {
            result.append(module->name());
        }
    }

    return result;
}

QStringList ModuleManager::shutdownModulesList()
{
    QStringList result;

    for (auto module : _modules.values())
    {
        if (!module->isStartedUp())
        {
            result.append(module->name());
        }
    }

    return result;
}

QStringList ModuleManager::modulesPool()
{
    return _modules.keys();
}

void ModuleManager::addModuleToPool(Module* module)
{
    if (_modules.contains(module->name()))
    {
        delete module;
    }
    else
    {
        _modules[module->name()] = module;

        for (const auto& depName : module->dependecies())
        {
            if (!_dependants.contains(depName))
            {
                _dependants[depName] = QStringSet({module->name()});
            }
            else
            {
                _dependants[depName].insert(module->name());
            }
        }
    }
}

void ModuleManager::addModuleToPool(QString path)
{
}

void ModuleManager::addModulesToPool(QString dirPath, bool recursively)
{
}

QStringList ModuleManager::unresolvedDependencies(QString moduleName)
{
    QStringList result;

    if (!_modules.contains(moduleName))
    {
        qCritical() << QString("Module '%1' is not found").arg(moduleName);
    }
    else
    {
        auto module = _modules[moduleName];
        for (const auto& depName : module->dependecies())
        {
            if (!_modules.contains(depName))
            {
                result.append(depName);
            }
        }
    }

    return result;
}

QStringList ModuleManager::dependantsList(QString moduleName)
{
    return _dependants.value(moduleName).values();
}
