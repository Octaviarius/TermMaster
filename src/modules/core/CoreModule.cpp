#include "CoreModule.h"

#include "CommandManager.h"

#include <QCoreApplication>

static CommandContainer* _mainCommands;

bool CoreModule::startup()
{
    _mainCommands = new CommandContainer();

    _mainCommands->newCommand("Core.ExitWindow", QIcon());
    _mainCommands->newCommand("Core.ExitApplication", QIcon());
    _mainCommands->newCommand("Core.HomePage", QIcon());
    _mainCommands->newCommand("Core.CheckUpdates", QIcon());
    _mainCommands->newCommand("Core.ManageModules", QIcon());

    CommandManager::instance().registerCommandContainer(_mainCommands);

    return true;
}

bool CoreModule::shutdown()
{
    delete _mainCommands;
    return true;
}
