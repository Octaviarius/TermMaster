#include <QRegularExpression>
#include <Termaster/Core/CommandRegistry.h>
#include <stdexcept>

CommandRegistry::CommandRegistry(QObject* parent) : QObject(parent)
{
}

CommandRegistry::~CommandRegistry()
{
    _destroying         = true;
    const auto commands = _commands;
    _commands.clear();
    for (const auto& command : commands)
    {
        if (command)
        {
            disconnect(command, nullptr, this, nullptr);
        }
    }

    for (const auto& command : commands)
    {
        if (command)
        {
            delete command.data();
        }
    }
}

Command* CommandRegistry::create(const QString&      id,
                                 const QString&      text,
                                 const QIcon&        icon,
                                 const QKeySequence& defaultShortcut)
{
    if (_destroying)
    {
        throw std::logic_error("Command registry is being destroyed");
    }
    static const QRegularExpression validId(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\\z"));
    if (!validId.match(id).hasMatch())
    {
        throw std::invalid_argument(QStringLiteral("Invalid command ID '%1'").arg(id).toStdString());
    }
    if (find(id))
    {
        throw std::logic_error(QStringLiteral("Command ID '%1' already exists").arg(id).toStdString());
    }

    auto* command = new Command(id, text, icon, defaultShortcut, {}, this);
    _commands.insert(id, command);
    connect(command, &QObject::destroyed, this, [this, id] {
        if (_commands.value(id).isNull())
        {
            _commands.remove(id);
        }
    });
    return command;
}

Command* CommandRegistry::find(const QString& id) const
{
    return _commands.value(id).data();
}

QList<Command*> CommandRegistry::commands() const
{
    QList<Command*> result;
    for (const auto& command : _commands)
    {
        if (command)
        {
            result.append(command.data());
        }
    }
    return result;
}

bool CommandRegistry::remove(const QString& id)
{
    const auto command = _commands.take(id);
    if (!command)
    {
        return false;
    }

    disconnect(command, nullptr, this, nullptr);
    delete command.data();
    return true;
}
