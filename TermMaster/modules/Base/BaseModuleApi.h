#ifndef BASEMODULEAPI_H
#define BASEMODULEAPI_H

#include "core/Module.h"
#include "widgets/ConfigBaseWidget.h"

#include <QList>
#include <QMap>
#include <QMenuBar>
#include <QSettings>
#include <QToolBar>

class SettingsEntry
{
public:
    using List = QList<SettingsEntry>;
    using Map  = QMap<QString, SettingsEntry>;
    typedef ConfigBaseWidget* (*WidgetConstructor)(SettingsProxy config, QWidget* parent);

    SettingsEntry(QString guiPath, QString configPath, WidgetConstructor constructWidget) :
        guiPath(guiPath), configPath(configPath), constructWidget(constructWidget)
    {
    }

    SettingsEntry()
    {
    }

    QString           guiPath;
    QString           configPath;
    WidgetConstructor constructWidget;
};

class SettingsCategory
{
public:
    using Map  = QMap<QString, SettingsCategory>;
    using List = QList<SettingsCategory>;

    SettingsCategory()
    {
    }

    SettingsCategory(QString name, QString iconPath = "") : name(name), iconPath(iconPath)
    {
    }

    QString name;
    QString iconPath;
};

class BaseModuleApi : public ModuleApi
{
public:
    BaseModuleApi(Module* module);
    virtual ~BaseModuleApi();

    void showMainWindow();
    void hideMainWindow();

    bool                         registerSettingsCategory(const SettingsCategory& category);
    bool                         deregisterSettingsCategory(const SettingsCategory& category);
    int                          settingsCategoryOwnersCount(QString name);
    const SettingsCategory::Map& settingsCategories();

    void                      registerSettingEntries(const SettingsEntry::List& list);
    void                      deregisterSettingEntries(const SettingsEntry::List& list);
    const SettingsEntry::Map& settingEntries();

    // GUI related API
    QMenuBar& menuBar();
    QToolBar& toolBar();

    QStringList menuSockets();
    QStringList toolSockets();

    void addMenu(QString path);
    void addMenuActions(QString path, QActionGroup* actions);
    void addToolbarActions(QString toolbarName, QList<QAction*> actions);

private:
    class BaseModuleApiPrivate;
    BaseModuleApiPrivate* d;
};

#endif // BASEMODULEAPI_H
