#pragma once

#include "core/Command.h"
#include "core/Singleton.h"

class CommandManager : public QObject, public Singleton<CommandManager>
{
    Q_OBJECT
public:
    friend class Singleton<CommandManager>;

    Command* createCommand(QString      name,
                           QString      text            = "",
                           QIcon        icon            = QIcon(),
                           QKeySequence defaultShortcut = QKeySequence(),
                           QKeySequence shortcut        = QKeySequence());

    Command::Map commands();
    Command*     command(QString name);
    QAction*     newAction(QString commandName, QObject* parent = nullptr);

    void resetShortcuts();

private:
    CommandManager();

    Command::Map _commands;

signals:
    void shortcutChanged(Command* command);
};
