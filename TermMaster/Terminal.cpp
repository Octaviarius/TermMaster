#include "Terminal.h"

Terminal::Terminal(QObject* parent) : QObject{parent}
{
}

void Terminal::setConfig(const Registree& config)
{
    _config = config;
}

Registree Terminal::config()
{
    return _config;
}
