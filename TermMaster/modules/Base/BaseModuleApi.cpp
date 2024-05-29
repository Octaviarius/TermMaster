#include "BaseModuleApi.h"

#include "windows/MainWindow.h"

#include <QApplication>
#include <QDesktopServices>

static const QString _sourcesLink  = "https://github.com/Octaviarius/TermMaster";
static const QString _homepageLink = "https://github.com/Octaviarius/TermMaster";

static const SettingsCategory::List _settingCategories = {
    SettingsCategory("Input", ":/res/icon/cat-input"),
};

static const SettingsEntry::List _settingEntries = {
    // SettingsEntry("Input/Shortcuts", "main/input/shortcuts", ConfigBaseWidget::constructWidget<>),
};

class BaseModuleApi::BaseModuleApiPrivate
{
public:
    MainWindow* mainWindow;

    BaseModuleApiPrivate()
    {
        mainWindow = new MainWindow(SettingsManager::application());
    }

    ~BaseModuleApiPrivate()
    {
        mainWindow->deleteLater();
    }

    SettingsCategory::Map settingsCategories;
    QMap<QString, int>    settingsCategoriesOwners;

    SettingsEntry::Map settingEntries;
};

BaseModuleApi::BaseModuleApi(Module* module) : ModuleApi(module)
{
    d = new BaseModuleApiPrivate();
}

BaseModuleApi::~BaseModuleApi()
{
    delete d;
}

void BaseModuleApi::showMainWindow()
{
    d->mainWindow->show();
}

void BaseModuleApi::hideMainWindow()
{
    d->mainWindow->hide();
}

bool BaseModuleApi::registerSettingsCategory(const SettingsCategory& category)
{
    if (settingsCategoryOwnersCount(category.name) == 0)
    {
        d->settingsCategories[category.name] = category;
    }

    d->settingsCategoriesOwners[category.name]++;
}

bool BaseModuleApi::deregisterSettingsCategory(const SettingsCategory& category)
{
    switch (settingsCategoryOwnersCount(category.name))
    {
        case 0:
            break;

        case 1:
            d->settingsCategories.remove(category.name);
            d->settingsCategoriesOwners[category.name]--;

        default:
            d->settingsCategoriesOwners[category.name]--;
    }
}

int BaseModuleApi::settingsCategoryOwnersCount(QString name)
{
    return d->settingsCategoriesOwners.value(name, 0);
}

const SettingsCategory::Map& BaseModuleApi::settingsCategories()
{
    return d->settingsCategories;
}

void BaseModuleApi::registerSettingEntries(const SettingsEntry::List& list)
{
    for (auto& it : list)
    {
        d->settingEntries[it.guiPath] = it;
    }
}

void BaseModuleApi::deregisterSettingEntries(const SettingsEntry::List& list)
{
    for (auto it : list)
    {
        d->settingEntries.remove(it.guiPath);
    }
}

const SettingsEntry::Map& BaseModuleApi::settingEntries()
{
    return d->settingEntries;
}
