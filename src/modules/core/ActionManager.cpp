#include "ActionManager.h"

ActionContainer::ActionContainer()
{
}

void ActionContainer::prependGroup(QString group)
{
    _groups.push_front(group);
}

void ActionContainer::appendGroup(QString group)
{
    _groups.push_back(group);
}

bool ActionContainer::insertGroup(QString before, QString group)
{
    auto prevIt = _groups.begin();

    for (auto it = _groups.begin() + 1; it != _groups.end(); it++)
    {
        if (*it == before)
        {
            _groups.insert(prevIt, group);
            return true;
        }
        prevIt++;
    }

    return false;
}

QList<QString> ActionContainer::groups()
{
    return _groups;
}

void ActionContainer::addChild(ActionContainer* container, QString group)
{
}

QList<ActionContainer*> ActionContainer::children(QString group)
{
    return _children.value(group);
}

QList<ActionContainer*> ActionContainer::children()
{
}

/******************************************************************************/

ActionBuilder::ActionBuilder(QString path)
{
    _path   = path;
    _action = new QAction();
    ActionManager::addAction(_path, _action);
}

ActionBuilder::~ActionBuilder()
{
}

QAction* ActionBuilder::action()
{
    return _action;
}

ActionBuilder& ActionBuilder::setText(QString text)
{
    _action->setText(text);
    return *this;
}
ActionBuilder& ActionBuilder::setIcon(QIcon icon)
{
    _action->setIcon(icon);
    return *this;
}
ActionBuilder& ActionBuilder::setTooltip(QString text)
{
    _action->setToolTip(text);
    return *this;
}

ActionBuilder& ActionBuilder::setKeySequence(QKeySequence shortcut)
{
    _action->setShortcut(shortcut);
    return *this;
}

ActionBuilder& ActionBuilder::setKeySequence(QList<QKeySequence> shortcuts)
{
    _action->setShortcuts(shortcuts);
    return *this;
}

ActionBuilder& ActionBuilder::setVisible(bool vis)
{
    _action->setVisible(vis);
    return *this;
}

ActionBuilder& ActionBuilder::setEnabled(bool en)
{
    _action->setEnabled(en);
    return *this;
}

ActionBuilder& ActionBuilder::setSeparator(bool sep)
{
    _action->setSeparator(sep);
    return *this;
}

ActionBuilder& ActionBuilder::setOnTriggered(QObject* receiver, void (QObject::*onTriggered)(bool))
{
    QObject::connect(_action, &QAction::triggered, receiver, onTriggered);
    return *this;
}

/******************************************************************************/

static QMap<QString, QAction*> _actionsMap;

ActionManager::ActionManager()
{
}

void ActionManager::addAction(QString path, QAction* action)
{
    if (!_actionsMap.contains(path))
    {
        _actionsMap[path] = action;
    }
}

const QMap<QString, QAction*> ActionManager::actions()
{
    return _actionsMap;
}

QAction* ActionManager::action(QString path)
{
    return _actionsMap.value(path);
}
