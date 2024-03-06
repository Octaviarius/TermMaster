#include "Terminal.h"

Terminal::Terminal(QObject* parent) : QObject{parent}
{
}

void Terminal::setConfig(const PathedValue& config)
{
    _config = config;
}

PathedValue Terminal::config()
{
    return _config;
}
