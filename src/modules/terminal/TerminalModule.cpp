#include "TerminalModule.h"

#include <QCoreApplication>

bool TerminalModule::startup()
{
    qDebug() << "TerminalModule loaded!";
    return true;
}

bool TerminalModule::shutdown()
{
    qDebug() << "TerminalModule unloaded...";
    return true;
}
