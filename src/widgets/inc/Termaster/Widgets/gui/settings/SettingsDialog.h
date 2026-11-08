#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QIcon>
#include <QTabWidget>
#include <QVector>
#include <Termaster/Widgets/gui/settings/ConfigContainer.h>

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
