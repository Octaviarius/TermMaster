#pragma once

#include "utils/Path.h"
#include "utils/Singleton.h"
#include "utils/utils.h"

#include <QAction>
#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QString>

class Command;
class CommandContainer;
class CommandGroup;
class CommandLayout;
class CommandManager;

class Command : public QObject
{
    Q_OBJECT
public:
    using List = QList<Command*>;
    using Map  = QMap<QString, Command*>;

    Command(QString         name,
            QString         text,
            QIcon           icon,
            KeySequenceList defaultShortcut = KeySequenceList(),
            KeySequenceList shortcut        = KeySequenceList());

    QString         name() const;
    QString         text() const;
    QString         tooltip() const;
    QIcon           icon() const;
    KeySequenceList shortcut() const;
    KeySequenceList defaultShortcut() const;

    Command& setName(QString name);
    Command& setText(QString text);
    Command& setTooltip(QString tooltip);
    Command& setIcon(QIcon icon);
    Command& setShortcut(KeySequenceList shortcuts);
    Command& setDefaultShortcut(KeySequenceList shortcuts);
    Command& setSingleInstance(bool isSingle);

    const QList<QAction*> actions() const;
    QAction*              newAction(QObject* parent = nullptr);

private:
    QString         _name;
    QString         _text;
    QString         _tooltip;
    QIcon           _icon;
    KeySequenceList _shortcut;
    KeySequenceList _defaultShortcut;

    QList<QAction*> _actions;
    bool            _isSingleInstance;

public slots:
    void resetShortcut();
    void changeShortcut(KeySequenceList shortcut);

private slots:
    void _actionDestroyed(QObject* action);
};

/******************************************************************************/

/** Manages commands in a plugin internals */
class CommandContainer : public QObject
{
    Q_OBJECT
public:
    CommandContainer(QObject* parent = nullptr);

    Command* newCommand(QString         name,
                        QString         text,
                        QIcon           icon            = QIcon(),
                        KeySequenceList defaultShortcut = KeySequenceList(),
                        KeySequenceList shortcut        = KeySequenceList());

    const QMap<QString, Command*> commands() const;

private:
    QMap<QString, Command*> _commands;
};

/******************************************************************************/

class CommandManager : public QObject, public Singleton<CommandManager>
{
    Q_OBJECT
public:
    friend class Singleton<CommandManager>;

    void registerCommand(Command* command);
    void registerCommandContainer(CommandContainer* container);

    const QMap<QString, Command*>& commands();
    Command*                       command(QString name);
    QAction*                       newAction(QString commandName, QObject* parent = nullptr);

    CommandLayout* layout(QString name);

private:
    CommandManager();

    Command::Map _commands;

signals:
    void shortcutChanged(Command* command);
};
