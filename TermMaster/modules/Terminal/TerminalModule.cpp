#include "TerminalModule.h"

#include "TerminalModuleApi.h"

TerminalModule::TerminalModule() : Module()
{
    _name        = "Terminal";
    _description = "Provides some types of terminals and related API";
    _author      = "Octaviarius (gomanchuk.as@gmail.com)";
    _version     = QVersionNumber(1, 0, 0);
    _depModules  = QStringSet({"Base"});
}

bool TerminalModule::_startup()
{
    _allocateApi<TerminalModuleApi>();
    return true;
}

bool TerminalModule::_shutdown()
{
    _cleanupApi();
    return true;
}
