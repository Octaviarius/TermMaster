#include "CommandManager.h"

Command::Command(QString name, QString text, QIcon icon, KeySequenceList defaultShortcut, KeySequenceList shortcut) :
    QObject(), _name(name), _text(text), _icon(icon), _shortcut(shortcut), _defaultShortcut(defaultShortcut)
{
}

QString Command::name() const
{
    return _name;
}
QString Command::text() const
{
    return _text;
}
QString Command::tooltip() const
{
    return _tooltip;
}
QIcon Command::icon() const
{
    return _icon;
}
KeySequenceList Command::shortcut() const
{
    return _shortcut;
}

KeySequenceList Command::defaultShortcut() const
{
    return _defaultShortcut;
}

Command& Command::setName(QString name)
{
    _name = name;
    return *this;
}
Command& Command::setText(QString text)
{
    _text = text;
    for (auto action : _actions)
    {
        action->setText(tr(_text.toLatin1().data()));
    }
    return *this;
}
Command& Command::setTooltip(QString tooltip)
{
    _tooltip = tooltip;
    for (auto action : _actions)
    {
        action->setToolTip(tr(_tooltip.toLatin1().data()));
    }
    return *this;
}
Command& Command::setIcon(QIcon icon)
{
    _icon = icon;
    for (auto action : _actions)
    {
        action->setIcon(icon);
    }
    return *this;
}
Command& Command::setShortcut(KeySequenceList shortcuts)
{
    _shortcut = shortcuts;

    for (auto action : _actions)
    {
        action->setShortcuts(_shortcut);
    }

    emit CommandManager::instance().shortcutChanged(this);
    return *this;
}
Command& Command::setDefaultShortcut(KeySequenceList shortcut)
{
    _defaultShortcut = shortcut;
    return *this;
}

const QList<QAction*> Command::actions() const
{
    return _actions;
}

QAction* Command::newAction(QObject* parent)
{
    auto action = new QAction(parent);
    _actions    += action;

    action->setIcon(_icon);
    action->setText(_text);
    action->setToolTip(_tooltip);
    action->setShortcuts(_shortcut);

    connect(action, &QAction::destroyed, this, &Command::_actionDestroyed);
    return action;
}

void Command::resetShortcut()
{
    _shortcut = _defaultShortcut;
}
void Command::changeShortcut(KeySequenceList shortcut)
{
    _shortcut = shortcut;
}

void Command::_actionDestroyed(QObject* action)
{
    _actions.removeOne(action);
}

CommandContainer::CommandContainer(QObject* parent) : QObject(parent)
{
}

Command* CommandContainer::newCommand(QString         name,
                                      QString         text,
                                      QIcon           icon,
                                      KeySequenceList defaultShortcut,
                                      KeySequenceList shortcut)
{
    if (_commands.contains(name))
    {
        qWarning() << QString("");
        return nullptr;
    }
    else
    {
        auto command    = new Command(name, text, icon, defaultShortcut, shortcut);
        _commands[name] = command;
        command->setParent(this);
        connect(this, &CommandContainer::destroyed, command, &Command::deleteLater);
        return command;
    }
}

const QMap<QString, Command*> CommandContainer::commands() const
{
    return _commands;
}

/******************************************************************************/

CommandManager::CommandManager()
{
}

void CommandManager::registerCommand(Command* command)
{
    if (_commands.contains(command->name()))
    {
        qWarning() << QString("Command '%1' is already registered, check conflicts").arg(command->name());
    }
    else
    {
        auto onActionDestroyed = [this](QObject* command) {
            auto cmd = qobject_cast<Command*>(command);
            this->_commands.remove(cmd->name());
            qDebug() << QString("Command '%1' is deregistered").arg(cmd->name());
        };

        _commands[command->name()] = command;
        qDebug() << QString("Command '%1' is registered").arg(command->name());
        connect(command, &Command::destroyed, onActionDestroyed);
    }
}

void CommandManager::registerCommandContainer(CommandContainer* container)
{
    for (auto it : container->commands().values())
    {
        registerCommand(it);
    }
}

const QMap<QString, Command*>& CommandManager::commands()
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
