#include "ActionContainer.h"

ActionContainer::ActionContainer(QString name, QString title) :
    _parent(nullptr), _name(name), _title(title == "" ? name : title)
{
}

ActionContainer::~ActionContainer()
{
    for (auto lst : _menus.values())
    {
        for (auto c : lst)
        {
            delete c;
        }
    }
}

ActionContainer* ActionContainer::parent() const
{
    return _parent;
}

ActionContainer* ActionContainer::root() const
{
    ActionContainer* ret = this->_parent;

    while (ret->_parent != nullptr)
    {
        ret = ret->_parent;
    }

    return ret;
}

PurePosixPath ActionContainer::path() const
{
    if (_parent == nullptr)
    {
        return _name;
    }
    else
    {
        return _parent->path() / _name;
    }
}

QString ActionContainer::name() const
{
    return _name;
}

QString ActionContainer::title() const
{
    return _title;
}

bool ActionContainer::addGroup(QString name)
{
    bool ret = false;

    if (!_groups.contains(name))
    {
        _groups.append(name);
        ret = _addGroup(name);

        if (!ret)
        {
            _groups.removeOne(name);
        }
    }

    return ret;
}

QStringList ActionContainer::groups() const
{
    return _groups;
}

bool ActionContainer::addMenu(ActionContainer* menu, QString group)
{
    bool ret = false;

    if (!menu)
    {
        qWarning() << QString("Null pointer in addMenu of '%1'").arg(path());
    }
    else if (menu->_parent != nullptr)
    {
        qWarning() << QString("Container '%1' already has a parent '%2', won't be added to '%3'")
                          .arg(menu->_name)
                          .arg(menu->_parent->path())
                          .arg(path());
    }
    else if (!_groups.contains(group))
    {
        qWarning() << QString("Group '%1' isn't exised in '%2'").arg(group).arg(path());
    }
    else
    {
        if (!_menus.contains(group))
        {
            _menus[group] = List();
        }

        auto& list = _menus[group];

        if (list.contains(menu))
        {
            qWarning() << QString("Container '%1' is already a child of the parent '%2'").arg(menu->_name).arg(path());
        }
        else
        {
            list.append(menu);
            menu->_parent = this;

            ret = _addMenu(menu, group);

            if (!ret)
            {
                qCritical() << QString("Container '%1' won't be added due to errors").arg(menu->path());
                menu->_parent = nullptr;
                list.removeOne(menu);
            }
        }
    }

    return ret;
}

ActionContainer::List ActionContainer::menus(QString group) const
{
    return _menus.value(group);
}

bool ActionContainer::addAction(QAction* action, QString group)
{
    bool ret = false;

    if (!action)
    {
        qWarning() << QString("Null pointer in addAction of '%1'").arg(path());
    }
    else if (!_groups.contains(group))
    {
        qWarning() << QString("Group '%1' isn't exised in '%2'").arg(group).arg(path());
    }
    else
    {
        if (!_actions.contains(group))
        {
            _actions[group] = QActionList();
        }

        auto& list = _actions[group];

        if (!list.contains(action))
        {
            list.append(action);

            ret = _addAction(action, group);

            if (!ret)
            {
                list.removeOne(action);
            }
        }
    }

    return ret;
}

QActionList ActionContainer::actions(QString group) const
{
    return _actions.value(group);
}
