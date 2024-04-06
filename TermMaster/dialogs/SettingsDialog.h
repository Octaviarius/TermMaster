#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "qtabwidget.h"
#include "widgets/ConfigBaseWidget.h"

#include <QDialog>
#include <QIcon>
#include <QVector>

namespace Ui
{
class SettingsDialog;
} // namespace Ui

class SettingsEntry
{
public:
    typedef ConfigBaseWidget* (*WidgetConstructor)(Registree config, QWidget* parent);

    SettingsEntry() {};

    SettingsEntry(QString guiPath, QString configPath, WidgetConstructor constructWidget) :
        guiPath(guiPath), configPath(configPath), constructWidget(constructWidget)
    {
    }

    QString           guiPath;
    QString           configPath;
    WidgetConstructor constructWidget;
};

class SettingsCategory
{
public:
    SettingsCategory()
    {
    }

    SettingsCategory(QString name, QString iconPath = "") : name(name), iconPath(iconPath)
    {
    }

    QString name;
    QString iconPath;
};

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(Registree config, QWidget* parent = nullptr);
    ~SettingsDialog();

    int  addEntries(QVector<SettingsEntry> entries);
    bool addEntry(const SettingsEntry& entry);

    int  addCategories(QVector<SettingsCategory> cats);
    bool addCategory(const SettingsCategory& cat);

private:
    Ui::SettingsDialog* ui;

    Registree                       _config;
    QMap<QString, QTabWidget*>      _tabWidgets;
    QMap<QString, SettingsCategory> _categories;

    void _updatePathLabel();
};

#endif // SETTINGSDIALOG_H
