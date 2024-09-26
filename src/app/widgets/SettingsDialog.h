#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "config/ConfigContainer.h"

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
    explicit SettingsDialog(ConfigContainer* settingsContainer, QWidget* parent = nullptr);
    ~SettingsDialog();

private:
    Ui::SettingsDialog*        ui;
    ConfigContainer*           _configContainer;
    QMap<QString, QTabWidget*> _tabWidgets;
};

#endif // SETTINGSDIALOG_H
