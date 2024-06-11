#pragma once

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
    Command(QString         name,
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

    const QList<QAction*> actions() const;
    QAction*              makeAction(QObject* parent = nullptr);

private:
    QString         _name;
    QString         _text;
    QString         _tooltip;
    QIcon           _icon;
    KeySequenceList _shortcut;
    KeySequenceList _defaultShortcut;

    QList<QAction*> _actions;

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
                        QIcon           icon,
                        KeySequenceList defaultShortcut = KeySequenceList(),
                        KeySequenceList shortcut        = KeySequenceList());

    const QMap<QString, Command*> commands() const;

private:
    QMap<QString, Command*> _commands;
};

/******************************************************************************/
/*
class CommandLayout
{
public:
    CommandLayout(bool isGroup);
    CommandLayout(const CommandLayout& copy);
    ~CommandLayout();

    bool isGroup();

private:
    CommandLayout*                       _parent;
    QMap<QString, QList<CommandLayout*>> _children;
};
*/
/******************************************************************************/

class CommandManager : public QObject, public Singleton<CommandManager>
{
    Q_OBJECT
public:
    friend class Singleton<CommandManager>;

    void                           registerCommand(Command* command);
    void                           registerCommandContainer(CommandContainer* container);
    const QMap<QString, Command*>& commands();

private:
    CommandManager();

    QMap<QString, Command*> _commands;

signals:
    void shortcutChanged(Command* command);

};

