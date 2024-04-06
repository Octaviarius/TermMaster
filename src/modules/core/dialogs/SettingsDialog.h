#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

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
    explicit SettingsDialog(SettingsProxy config, QWidget* parent = nullptr);
    ~SettingsDialog();

    int  addEntries(QList<SettingsEntry> entries);
    bool addEntry(const SettingsEntry& entry);

    int  addCategories(QList<SettingsCategory> cats);
    bool addCategory(const SettingsCategory& cat);

private:
    Ui::SettingsDialog* ui;

    SettingsProxy                   _config;
    QMap<QString, QTabWidget*>      _tabWidgets;
    QMap<QString, SettingsCategory> _categories;

    void _updatePathLabel();
};

#endif // SETTINGSDIALOG_H
