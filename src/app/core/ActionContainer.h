#pragma once

#include "core/Path.h"

#include <QAction>
#include <QList>
#include <QMap>
#include <QObject>

using QActionList = QList<QAction*>;

class ActionContainer
{
public:
    using List = QList<ActionContainer*>;
    using Map  = QMap<QString, ActionContainer*>;

    ActionContainer(QString name, QString title = "");
    virtual ~ActionContainer();

    ActionContainer* parent() const;
    ActionContainer* root() const;

    PurePosixPath path() const;

    QString name() const;
    QString title() const;

    bool        addGroup(QString name);
    QStringList groups() const;

    bool addMenu(ActionContainer* menu, QString group);
    List menus(QString group) const;

    bool            addAction(QAction* action, QString group);
    QList<QAction*> actions(QString group) const;

protected:
    virtual bool _addGroup(QString name)                             = 0;
    virtual bool _addAction(QAction* action, QString group)          = 0;
    virtual bool _addMenu(ActionContainer* container, QString group) = 0;

    QStringList                _groups;
    QMap<QString, List>        _menus;
    QMap<QString, QActionList> _actions;

private:
    QString _name;
    QString _title;

    ActionContainer* _parent;
};
