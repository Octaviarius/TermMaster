#include "TerminalModule.h"

#include <QCoreApplication>
#include "modules/core/WindowManager.h"

bool TerminalModule::startup()
{
    auto w = WindowManager::instance().currentWindow();
    return true;
}

bool TerminalModule::shutdown()
{
    return true;
}
