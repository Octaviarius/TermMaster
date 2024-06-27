#include "ConfigTerminalEmulationWidget.h"

#include "ui_ConfigTerminalEmulationWidget.h"

#include <QFontDatabase>
#include <QFontDialog>

ConfigTerminalEmulationWidget::ConfigTerminalEmulationWidget(SettingsProxy config, QWidget* parent) :
    ConfigBaseWidget(config, parent), ui(new Ui::ConfigTerminalEmulationWidget)
{
    ui->setupUi(this);
}

ConfigTerminalEmulationWidget::~ConfigTerminalEmulationWidget()
{
    delete ui;
}

void ConfigTerminalEmulationWidget::commit()
{
}
