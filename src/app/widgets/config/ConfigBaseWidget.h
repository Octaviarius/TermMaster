#ifndef CONFIGBASEWIDGET_H
#define CONFIGBASEWIDGET_H

#include "managers/ConfigManager.h"

#include <QWidget>

class ConfigBaseWidget;

class SettingsEntry
{
public:
    using List = QList<SettingsEntry>;
    using Map  = QMap<QString, SettingsEntry>;
    typedef ConfigBaseWidget* (*WidgetConstructor)(Settings config, QWidget* parent);

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

class SettingContainer
{
public:
    SettingContainer(QString name, QIcon icon);

    using List = QList<SettingContainer>;
    using Map  = QMap<QString, SettingContainer>;

private:
    QString _name;
    QIcon   _icon;
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

class ConfigBaseWidget : public QWidget
{
    Q_OBJECT
public:
    ConfigBaseWidget(Settings config, QWidget* parent = nullptr);

    template <typename T>
    static ConfigBaseWidget* constructWidget(Settings config, QWidget* parent = nullptr)
    {
        return new T(config, parent);
    }

    const Settings& config();
    virtual void    commit() = 0;

protected:
    Settings _config;
signals:
};

#endif // CONFIGBASEWIDGET_H
