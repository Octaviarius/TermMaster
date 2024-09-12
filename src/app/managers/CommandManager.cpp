#include "CommandManager.h"

CommandManager::CommandManager()
{
}

Command* CommandManager::createCommand(QString         name,
                                       QString         text,
                                       QIcon           icon,
                                       QKeySequence defaultShortcut,
                                       QKeySequence shortcut)
{
    Command* command = nullptr;

    if (_commands.contains(name))
    {
        qWarning() << QString("Command '%1' is already registered, check conflicts").arg(name);
    }
    else
    {
        auto onActionDestroyed = [this](QObject* command) {
            auto cmd = qobject_cast<Command*>(command);
            this->_commands.remove(cmd->name());
            qDebug() << QString("Command '%1' is deregistered").arg(cmd->name());
        };

        command = new Command(name, text, icon, defaultShortcut, shortcut);

        _commands[command->name()] = command;
        qDebug() << QString("Command '%1' is registered").arg(command->name());
        connect(command, &Command::destroyed, onActionDestroyed);
    }

    return command;
}

Command::Map CommandManager::commands()
{
    return _commands;
}

Command* CommandManager::command(QString name)
{
    return _commands.value(name);
}

QAction* CommandManager::newAction(QString commandName, QObject* parent)
{
    auto cmd = command(commandName);

    if (!cmd)
    {
        return nullptr;
    }
    else
    {
        return cmd->newAction(parent);
    }
}
