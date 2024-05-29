#include "BaseModuleApi.h"

#include "windows/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDesktopServices>
#include <QList>

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
        mainWindow  = new MainWindow(SettingsManager::application());
        helpActions = new QActionGroup(nullptr);
    }

    ~BaseModuleApiPrivate()
    {
        mainWindow->deleteLater();
        helpActions->deleteLater();
    }

    SettingsCategory::Map settingsCategories;
    QMap<QString, int>    settingsCategoriesOwners;

    SettingsEntry::Map settingEntries;

    QActionGroup* helpActions;
};

BaseModuleApi::BaseModuleApi(Module* module) : ModuleApi(module)
{
    d = new BaseModuleApiPrivate();

    auto file    = menuBar().addMenu("File");
    auto edit    = menuBar().addMenu("Edit");
    auto options = menuBar().addMenu("Options");
    auto help    = menuBar().addMenu("Help");

    d->mainWindow->addMenuSocket("FileNew", file, new QAction());
    d->mainWindow->addMenuSocket("FileSaves", file, new QAction());
    d->mainWindow->addMenuSocket("FileMiddle", file, new QAction());
    d->mainWindow->addMenuSocket("FileExits", file, new QAction());

    d->mainWindow->addMenuSocket("Edit", edit, new QAction());
    d->mainWindow->addMenuSocket("Options", options, new QAction());

    d->mainWindow->addMenuSocket("Help", help, new QAction());

    QActionGroup* group;
    group = new QActionGroup(nullptr);
    group->addAction("Home page");
    group->addAction("Source page");
    group->addAction("")->setSeparator(true);
    group->addAction("About");
    d->mainWindow->addMenuActions("Help", group);
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
    for (auto& it : list)
    {
        d->settingEntries.remove(it.guiPath);
    }
}

const SettingsEntry::Map& BaseModuleApi::settingEntries()
{
    return d->settingEntries;
}

QMenuBar& BaseModuleApi::menuBar()
{
    return d->mainWindow->menuBar();
}

QToolBar& BaseModuleApi::toolBar()
{
    return d->mainWindow->toolBar();
}

void BaseModuleApi::addMenu(QString path)
{
    // d->mainWindow->addMenu(path);
}

void BaseModuleApi::addMenuActions(QString path, QActionGroup* actions)
{
    // d->mainWindow->addMenuAction(path, actions);
}

void BaseModuleApi::addToolbarActions(QString toolbarName, QList<QAction*> actions)
{
}
