#ifndef MODULEMANAGER_H
#define MODULEMANAGER_H

#include "Module.h"

#include <QPluginLoader>
#include <QString>
#include <QStringList>
#include <QVersionNumber>
#include <utils/Path.h>

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

    bool isLoaded   : 1;
    bool isRequired : 1;
    bool isStarted  : 1;
    bool isStatic   : 1;
};

class ModuleContainer : QObject
{
    Q_OBJECT
public:
    ModuleContainer(QPluginLoader* loader);

    const ModuleInfo& info() const;
    bool              isStarted() const;

    friend ModuleManager;

signals:
    void startedUp(QString name);
    void shutdowned(QString name);

private:
    QPluginLoader* _loader;
    Module*        _module;
    ModuleInfo     _info;

    bool startup();
    bool shutdown();
};

class ModuleManager
{
public:
    ModuleManager()                          = delete;
    ModuleManager(const ModuleManager& copy) = delete;

    static QList<ModuleContainer*> modules();
    static QList<ModuleContainer*> startedModules();
    static QList<ModuleContainer*> staticModules();

    static bool startup(QString moduleName);
    static void startupAll();
    static bool shutdown(QString moduleName);

    static void scanForModules(Path path, bool recursively = true);

    static ModuleInfo moduleInfo(QString moduleName);
};

#endif // MODULEMANAGER_H
