#include "ModuleManager.h"

#include "Module.h"

#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonValue>
#include <QLibrary>
#include <QPluginLoader>

static QHash<QString, ModuleContainer*> _modules;
static QHash<QString, ModuleContainer*> _startedModules;
static QHash<QString, QStringList>      _dependants;

ModuleInfo::ModuleInfo() : isStatic(false), isLoaded(false), isRequired(false), isStarted(false)
{
}

/**************************************************************************/

ModuleContainer::ModuleContainer(QPluginLoader* loader)
{
    loader->setParent(this);

    _loader = loader;
    _module = qobject_cast<Module*>(loader->instance());

    if (!_module)
    {
        qCritical() << QString("Module file '%1' is not a valid module").arg(_loader->fileName());
    }
    else
    {
        _info.isLoaded = true;
        _info.file = _loader->fileName();

        auto js = _loader->metaData().value("MetaData").toObject();

        auto name        = js.value("name");
        auto version     = js.value("version");
        auto required    = js.value("required");
        auto vendor      = js.value("vendor");
        auto license     = js.value("license");
        auto category    = js.value("category");
        auto description = js.value("description");
        auto url         = js.value("url");
        auto depends     = js.value("depends");

        _info.name        = name.toString();
        _info.version     = QVersionNumber::fromString(version.toString("0.0.0"));
        _info.isRequired  = required.toBool();
        _info.vendor      = vendor.toString();
        _info.license     = license.toString();
        _info.category    = category.toString("Global");
        _info.description = description.toString();
        _info.url         = url.toString();

        if (depends.isArray())
        {
            for (auto it : depends.toArray())
            {
                _info.dependencies += it.toString();
            }
        }
    }
}

const ModuleInfo& ModuleContainer::info() const
{
    return _info;
}

bool ModuleContainer::startup()
{
    bool result = true;

    if (!_module)
    {
        qCritical() << QString("Module '%1' can't be started up, because it null").arg(_info.name);
        result = false;
    }
    else if (_info.isStarted)
    {
        qWarning() << QString("Module '%1' is already started").arg(_info.name);
    }
    else
    {
        _info.isStarted = _module->startup();

        if (!_info.isStarted)
        {
            qCritical() << QString("Module '%1' is not started").arg(_info.name);
            result = false;
        }
        else
        {
            qInfo() << QString("Module '%1' is started").arg(_info.name);
            emit startedUp(_info.name);
        }
    }

    return result;
}

bool ModuleContainer::shutdown()
{
    bool result = true;

    if (!_module)
    {
        qCritical() << QString("Module '%1' can't be shutdown, because it null").arg(_info.name);
        result = false;
    }

    if (_info.isStarted)
    {
        _info.isStarted = !_module->shutdown();

        if (!_info.isStarted)
        {
            qInfo() << QString("Module '%1' is down").arg(_info.name);
            emit shutdowned(info().name);
        }
    }

    return result;
}

bool ModuleContainer::isStarted() const
{
    return _info.isStarted;
}

/**************************************************************************/

QList<ModuleContainer*> ModuleManager::modules()
{
    return _modules.values();
}

QList<ModuleContainer*> ModuleManager::startedModules()
{
    return _startedModules.values();
}

QList<ModuleContainer*> ModuleManager::staticModules()
{
    QList<ModuleContainer*> result;

    for (auto it : _startedModules.values())
    {
        if (it->info().isStatic)
        {
            result += it;
        }
    }

    return result;
}

bool ModuleManager::startup(QString moduleName)
{
    bool result = true;

    if (!_startedModules.contains(moduleName))
    {
        auto module = _modules.value(moduleName);

        if (!module)
        {
            qCritical() << QString("No modules with name '%1'").arg(moduleName);
            result = false;
        }
        else
        {
            for (const auto& dep : module->info().dependencies)
            {
                result = startup(dep);

                if (!result)
                {
                    break;
                }
            }

            if (result)
            {
                result = module->startup();
            }
        }
    }

    return result;
}

void ModuleManager::startupAll()
{
    for (const auto& it : _modules.keys())
    {
        startup(it);
    }
}

bool ModuleManager::shutdown(QString moduleName)
{
    bool result = true;

    if (_startedModules.contains(moduleName))
    {
        auto module = _modules.value(moduleName);

        if (!module)
        {
            qCritical() << QString("No modules with name '%1'").arg(moduleName);
            result = false;
        }
        else
        {
            for (const auto& dep : _dependants[module->info().name])
            {
                result = shutdown(dep);

                if (!result)
                {
                    break;
                }
            }

            if (result)
            {
                result = module->shutdown();
            }
        }
    }

    return result;
}

void ModuleManager::scanForModules(Path path, bool recursively)
{
    QDir dir(path);
    dir.setFilter(dir.filter() | QDir::Filter::NoDotAndDotDot | QDir::NoSymLinks);

    auto ll = dir.entryInfoList();

    for (const auto& entry : dir.entryInfoList())
    {
        auto absPath = entry.absoluteFilePath();

        if (entry.isDir())
        {
            if (recursively)
            {
                scanForModules(entry.absoluteFilePath(), recursively);
            }
        }
        else
        {
            qDebug() << absPath;

            if (QLibrary::isLibrary(absPath))
            {
                auto loader = new QPluginLoader(absPath);

                auto module = qobject_cast<Module*>(loader->instance());

                if (!module)
                {
                    delete loader;
                }
                else
                {
                    auto  container = new ModuleContainer(loader);
                    auto& modInfo   = container->info();

                    _modules[modInfo.name] = container;

                    for (const auto& it : modInfo.dependencies)
                    {
                        _dependants[it] += modInfo.name;
                    }
                }
            }
        }
    }
}

ModuleInfo ModuleManager::moduleInfo(QString moduleName)
{
    auto module = _modules[moduleName];
    if (module)
    {
        return module->info();
    }
    else
    {
        return ModuleInfo();
    }
}
