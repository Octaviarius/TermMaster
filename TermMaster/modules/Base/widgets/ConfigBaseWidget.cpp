#include "ConfigBaseWidget.h"

ConfigBaseWidget::ConfigBaseWidget(SettingsProxy config, QWidget* parent) : QWidget(parent), _config(config)
{
}

const SettingsProxy& ConfigBaseWidget::config()
{
    return _config;
}
