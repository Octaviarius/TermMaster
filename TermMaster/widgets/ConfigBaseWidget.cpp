#include "ConfigBaseWidget.h"

ConfigBaseWidget::ConfigBaseWidget(Registree config, QWidget* parent) : QWidget(parent), _config(config)
{
}

Registree& ConfigBaseWidget::config()
{
    return _config;
}
