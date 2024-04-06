#ifndef CONFIGBASEWIDGET_H
#define CONFIGBASEWIDGET_H

#include "modules/core/SettingsManager.h"

#include <QWidget>

class ConfigBaseWidget;

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

class ConfigBaseWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ConfigBaseWidget(SettingsProxy config, QWidget* parent = nullptr);

    template <typename T>
    static ConfigBaseWidget* constructWidget(SettingsProxy config, QWidget* parent = nullptr)
    {
        return new T(config, parent);
    }

    const SettingsProxy& config();
    virtual void         commit() = 0;

protected:
    SettingsProxy _config;
signals:
};

#endif // CONFIGBASEWIDGET_H
