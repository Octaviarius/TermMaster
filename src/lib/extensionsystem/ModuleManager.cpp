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

/**************************************************************************/

ModuleInfo::ModuleInfo() : isValid(false), isStatic(false), isLoaded(false), isRequired(false), isStarted(false)
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
        _info.isValid = false;
    }
    else
    {
        _info.isValid   = true;
        _info.isLoaded  = true;
        _info.file      = _loader->fileName();
        _info.buildTime = _module->buildTime();

        _loadMetadata(_loader->metaData().value("MetaData").toObject());
    }
}

ModuleContainer::ModuleContainer(const QStaticPlugin& plugin) : _loader(nullptr)
{
    _module = qobject_cast<Module*>(plugin.instance());

    if (_module)
    {
        _info.isValid = false;
    }
    else
    {
        _info.isValid   = true;
        _info.isLoaded  = true;
        _info.isStatic  = true;
        _info.file      = "built-in";
        _info.buildTime = _module->buildTime();

        _loadMetadata(plugin.metaData().value("MetaData").toObject());
    }
}
ModuleContainer::~ModuleContainer()
{
    if (_loader)
    {
        delete _loader;
    }
}

void ModuleContainer::_loadMetadata(QJsonObject data)
{
    auto name        = data.value("name");
    auto version     = data.value("version");
    auto required    = data.value("required");
    auto vendor      = data.value("vendor");
    auto license     = data.value("license");
    auto category    = data.value("category");
    auto description = data.value("description");
    auto url         = data.value("url");
    auto depends     = data.value("depends");

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
        for (const auto& it : depends.toArray())
        {
            _info.dependencies += it.toString();
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
    else if (_info.isStarted)
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

ModuleManager::ModuleManager()
{
}

bool ModuleManager::_registerModule(ModuleContainer* module)
{
    bool result = true;

    auto modInfo = module->info();

    if (!modInfo.isValid)
    {
        result = false;
        delete module;
    }
    else
    {
        if (_modules.contains(modInfo.name))
        {
            delete module;
            result = false;
        }
        else
        {
            _modules[modInfo.name] = module;

            for (const auto& it : modInfo.dependencies)
            {
                _dependants[it] += modInfo.name;
            }
        }
    }

    return result;
}

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
            auto modInfo = module->info();

            if (!modInfo.isStarted)
            {
                for (const auto& dep : modInfo.dependencies)
                {
                    result &= startup(dep);
                }

                if (!result)
                {
                    qCritical() << QString("Module '%1' can't be started, because the depends unsatisfied")
                                       .arg(moduleName);
                }
                else
                {
                    result = module->startup();

                    if (result)
                    {
                        emit moduleStarted(moduleName);
                    }
                }
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

void ModuleManager::startupRequired()
{
    for (const auto& it : _modules.asKeyValueRange())
    {
        if (it.second->info().isRequired)
        {
            startup(it.first);
        }
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
            auto modInfo = module->info();

            if (modInfo.isStarted)
            {
                for (const auto& dep : _dependants[modInfo.name])
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
                    emit moduleStopped(moduleName);
                }
            }
        }
    }

    return result;
}

void ModuleManager::scanForModules(Path path, bool recursively)
{
    QDirIterator it(path,
                    QDir::Filter::Files,
                    recursively ? QDirIterator::IteratorFlag::Subdirectories :
                                  QDirIterator::IteratorFlag::NoIteratorFlags);

    while (it.hasNext())
    {
        auto absPath = it.next();

        if (QLibrary::isLibrary(absPath))
        {
            auto loader    = new QPluginLoader(absPath);
            auto container = new ModuleContainer(loader);
            _registerModule(container);
        }
    }
}

void ModuleManager::scanForBuiltinModules()
{
    for (const auto& it : QPluginLoader::staticPlugins())
    {
        auto container = new ModuleContainer(it);
        _registerModule(container);
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
