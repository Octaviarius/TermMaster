#include "ConfigTerminalEmulationWidget.h"

#include "ui_ConfigTerminalEmulationWidget.h"

#include <QFontDatabase>
#include <QFontDialog>

ConfigTerminalEmulationWidget::ConfigTerminalEmulationWidget(Registree config, QWidget* parent) :
    ConfigBaseWidget(config, parent), ui(new Ui::ConfigTerminalEmulationWidget)
{
    ui->setupUi(this);
    _config = config;
}

ConfigTerminalEmulationWidget::~ConfigTerminalEmulationWidget()
{
    delete ui;
}

void ConfigTerminalEmulationWidget::commit()
{
}
