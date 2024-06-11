#ifndef MODULEMANAGER_H
#define MODULEMANAGER_H

#include "Module.h"

#include <QPluginLoader>
#include <QString>
#include <QStringList>
#include <QVersionNumber>
#include <utils/Path.h>
#include <utils/Singleton.h>

class ModuleManager;
class ModuleInfo;

class ModuleInfo
{
public:
    using List = QList<ModuleInfo>;

    ModuleInfo();

    Path file;

    QString        name;
    QVersionNumber version;
    QString        vendor;
    QString        license;
    QString        category;
    QString        url;
    QString        description;
    QStringList    dependencies;
    QDateTime      buildTime;

    bool isValid    : 1;
    bool isLoaded   : 1;
    bool isRequired : 1;
    bool isStarted  : 1;
    bool isStatic   : 1;
};

class ModuleContainer : QObject
{
    Q_OBJECT
public:
    friend class ModuleManager;

    ~ModuleContainer();

    const ModuleInfo& info() const;
    bool              isStarted() const;

    friend ModuleManager;

signals:
    void startedUp(QString name);
    void shutdowned(QString name);

private:
    ModuleContainer(QPluginLoader* loader);
    ModuleContainer(const QStaticPlugin& plugin);

    void _loadMetadata(QJsonObject data);

    QPluginLoader* _loader;
    Module*        _module;
    ModuleInfo     _info;

    bool startup();
    bool shutdown();
};

class ModuleManager : public QObject, public Singleton<ModuleManager>
{
    Q_OBJECT
public:
    friend class Singleton<ModuleManager>;

    QList<ModuleContainer*> modules();
    QList<ModuleContainer*> startedModules();
    QList<ModuleContainer*> staticModules();

    bool startup(QString moduleName);
    void startupAll();
    void startupRequired();
    bool shutdown(QString moduleName);

    void scanForModules(Path path, bool recursively = true);
    void scanForBuiltinModules();

    ModuleInfo moduleInfo(QString moduleName);

private:
    ModuleManager();
    bool _registerModule(ModuleContainer* module);

signals:
    void moduleStarted(QString name);
    void moduleFound(QString name);
    void moduleStopped(QString name);
};

#endif // MODULEMANAGER_H
