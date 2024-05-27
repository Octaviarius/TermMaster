#include "BaseModule.h"

#include "BaseModuleApi.h"

BaseModule::BaseModule() : Module()
{
    _name        = "Base";
    _description = "Base module provides common API";
    _author      = "Octaviarius (gomanchuk.as@gmail.com)";
    _version     = QVersionNumber(1, 0, 0);
}

bool BaseModule::_startup()
{
    _allocateApi<BaseModuleApi>();
    return true;
}

bool BaseModule::_shutdown()
{
    _cleanupApi();
    return true;
}
