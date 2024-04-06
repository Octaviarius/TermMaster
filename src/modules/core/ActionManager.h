#ifndef ACTIONMANAGER_H
#define ACTIONMANAGER_H

#include <QAction>
#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QString>

class ActionContainer
{
public:
    ActionContainer();

    void           appendGroup(QString group);
    void           prependGroup(QString group);
    bool           insertGroup(QString before, QString group);
    QList<QString> groups();

    void                    addChild(ActionContainer* container, QString group);
    QList<ActionContainer*> children(QString group);
    QList<ActionContainer*> children();

private:
    QList<QString> _groups;

    ActionContainer*                       _parent;
    QMap<QString, QList<ActionContainer*>> _children; // for speedup
};

class ActionBuilder
{
public:
    ActionBuilder(QString path);
    ~ActionBuilder();

    QAction* action();

    ActionBuilder& setText(QString text);
    ActionBuilder& setIcon(QIcon icon);
    ActionBuilder& setTooltip(QString text);
    ActionBuilder& setKeySequence(QKeySequence shortcut);
    ActionBuilder& setKeySequence(QList<QKeySequence> shortcuts);
    ActionBuilder& setVisible(bool vis);
    ActionBuilder& setEnabled(bool en);
    ActionBuilder& setSeparator(bool sep);
    ActionBuilder& setOnTriggered(QObject* receiver, void (QObject::*onTriggered)(bool));

private:
    QString  _path;
    QAction* _action;
};

class ActionManager
{
public:
    ActionManager();

    static void                          addAction(QString path, QAction* action);
    static const QMap<QString, QAction*> actions();
    static QAction*                      action(QString path);
};

#endif // ACTIONMANAGER_H
