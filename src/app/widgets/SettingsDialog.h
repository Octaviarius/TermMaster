#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "managers/ConfigManager.h"
#include "widgets/config/ConfigBaseWidget.h"

#include <QDialog>
#include <QIcon>
#include <QTabWidget>
#include <QVector>

namespace Ui
{
class SettingsDialog;
} // namespace Ui

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(Settings config, QWidget* parent = nullptr);
    ~SettingsDialog();

    int  addEntries(QList<SettingsEntry> entries);
    bool addEntry(const SettingsEntry& entry);

    int  addCategories(QList<SettingsCategory> cats);
    bool addCategory(const SettingsCategory& cat);

private:
    Ui::SettingsDialog* ui;

    Settings                        _config;
    QMap<QString, QTabWidget*>      _tabWidgets;
    QMap<QString, SettingsCategory> _categories;

    void _updatePathLabel();
};

#endif // SETTINGSDIALOG_H
