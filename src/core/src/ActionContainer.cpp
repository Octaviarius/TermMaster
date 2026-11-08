#include <QThread>
#include <Termaster/Core/ActionContainer.h>
#include <stdexcept>
#include <utility>

ActionContainer::ActionContainer(QString name, QString title, QObject* owner) :
    QObject(owner), _name(std::move(name)), _title(title.isEmpty() ? _name : std::move(title))
{
    setObjectName(_name);
}

ActionContainer::~ActionContainer()
{
    _destroying = true;
    if (_parentContainer)
    {
        _parentContainer->_forgetMenu(this);
    }

    QList<Record> records;
    for (auto& group : _groups)
    {
        records.append(std::move(group.items));
    }
    _groups.clear();
    _release(records);
    _deleteMenus(records);
}

QString ActionContainer::name() const
{
    return _name;
}
QString ActionContainer::title() const
{
    return _title;
}

void ActionContainer::setTitle(const QString& title)
{
    if (!_canMutate() || title == _title)
    {
        return;
    }
    _title = title;
    emit titleChanged(_title);
}

ActionContainer* ActionContainer::parentContainer() const
{
    return _parentContainer.data();
}

ActionContainer* ActionContainer::root() const
{
    auto current = const_cast<ActionContainer*>(this);
    while (current->_parentContainer)
    {
        current = current->_parentContainer.data();
    }
    return current;
}

QString ActionContainer::path() const
{
    QStringList            parts;
    const ActionContainer* current = this;
    while (current->_parentContainer)
    {
        parts.prepend(current->_name);
        current = current->_parentContainer;
    }
    return '/' + parts.join('/');
}

ActionContainer* ActionContainer::findMenu(const QString& path) const
{
    auto       current = path.startsWith('/') ? root() : const_cast<ActionContainer*>(this);
    const auto parts   = path.split('/', Qt::SkipEmptyParts);
    for (const auto& part : parts)
    {
        if (part == ".")
        {
            continue;
        }
        if (part == "..")
        {
            current = current->parentContainer();
            if (!current)
            {
                return nullptr;
            }
            continue;
        }
        ActionContainer* found = nullptr;
        for (const auto& group : current->_groups)
        {
            for (const auto& record : group.items)
            {
                if (record.item.kind == Kind::Menu && record.item.menu && record.item.menu->name() == part)
                {
                    found = record.item.menu;
                    break;
                }
            }
            if (found)
            {
                break;
            }
        }
        if (!found)
        {
            return nullptr;
        }
        current = found;
    }
    return current;
}

bool ActionContainer::isAvailable() const
{
    return !_destroying && _isAvailable();
}

QStringList ActionContainer::groups() const
{
    QStringList result;
    for (const auto& group : _groups)
    {
        result.append(group.name);
    }
    return result;
}

qsizetype ActionContainer::groupIndex(const QString& name) const
{
    for (qsizetype index = 0; index < _groups.size(); ++index)
    {
        if (_groups[index].name == name)
        {
            return index;
        }
    }
    return -1;
}

void ActionContainer::addGroup(const QString& name)
{
    if (!_canMutate())
    {
        throw std::logic_error("Action container cannot be mutated on this thread or during destruction");
    }

    insertGroup(name, _groups.size());
}

void ActionContainer::insertGroup(const QString& name, qsizetype index)
{
    if (!_canMutate() || !isAvailable())
    {
        throw std::logic_error("Action container backend is unavailable or mutation is forbidden");
    }
    if (index < 0 || index > _groups.size() || groupIndex(name) >= 0)
    {
        throw std::invalid_argument("Group name is duplicated or insertion index is invalid");
    }
    _groups.insert(index, Group {name, {}});
    _notify();
}

void ActionContainer::insertGroupBefore(const QString& name, const QString& reference)
{
    if (!_canMutate())
    {
        throw std::logic_error("Action container cannot be mutated on this thread or during destruction");
    }
    const qsizetype index = groupIndex(reference);
    if (index < 0)
    {
        throw std::invalid_argument("Reference group does not exist");
    }
    insertGroup(name, index);
}

void ActionContainer::insertGroupAfter(const QString& name, const QString& reference)
{
    if (!_canMutate())
    {
        throw std::logic_error("Action container cannot be mutated on this thread or during destruction");
    }
    const qsizetype index = groupIndex(reference);
    if (index < 0)
    {
        throw std::invalid_argument("Reference group does not exist");
    }
    insertGroup(name, index + 1);
}

bool ActionContainer::moveGroup(const QString& name, qsizetype index)
{
    if (!_canMutate())
    {
        return false;
    }
    const qsizetype from = groupIndex(name);
    if (!_canMutate() || !isAvailable() || from < 0 || index < 0 || index >= _groups.size())
    {
        return false;
    }
    if (from == index)
    {
        return true;
    }
    auto group = _groups.takeAt(from);
    _groups.insert(index, std::move(group));
    _notify();
    return true;
}

bool ActionContainer::moveGroupBefore(const QString& name, const QString& reference)
{
    if (!_canMutate())
    {
        return false;
    }
    const qsizetype from = groupIndex(name);
    const qsizetype to   = groupIndex(reference);
    if (from < 0 || to < 0)
    {
        return false;
    }
    return moveGroup(name, to - (from < to ? 1 : 0));
}

bool ActionContainer::moveGroupAfter(const QString& name, const QString& reference)
{
    if (!_canMutate())
    {
        return false;
    }
    const qsizetype from = groupIndex(name);
    const qsizetype to   = groupIndex(reference);
    if (from < 0 || to < 0)
    {
        return false;
    }
    return moveGroup(name, to + (from > to ? 1 : 0));
}

bool ActionContainer::renameGroup(const QString& name, const QString& newName)
{
    if (!_canMutate())
    {
        return false;
    }
    const qsizetype index = groupIndex(name);
    if (!_canMutate() || !isAvailable() || index < 0)
    {
        return false;
    }
    if (name == newName)
    {
        return true;
    }
    if (groupIndex(newName) >= 0)
    {
        return false;
    }
    _groups[index].name = newName;
    _notify();
    return true;
}

bool ActionContainer::clearGroup(const QString& name)
{
    if (!_canMutate())
    {
        return false;
    }
    const qsizetype index = groupIndex(name);
    if (!_canMutate() || index < 0)
    {
        return false;
    }
    if (_groups[index].items.isEmpty())
    {
        return true;
    }
    auto records = std::exchange(_groups[index].items, {});
    _release(records);
    _notify();
    ActionContainer::_deleteMenus(records);
    return true;
}

bool ActionContainer::removeGroup(const QString& name)
{
    if (!_canMutate())
    {
        return false;
    }
    const qsizetype index = groupIndex(name);
    if (!_canMutate() || index < 0)
    {
        return false;
    }
    auto records = _groups.takeAt(index).items;
    _release(records);
    _notify();
    ActionContainer::_deleteMenus(records);
    return true;
}

bool ActionContainer::clear()
{
    if (!_canMutate())
    {
        return false;
    }
    if (_groups.isEmpty())
    {
        return true;
    }
    QList<Record> records;
    for (auto& group : _groups)
    {
        records.append(std::move(group.items));
    }
    _groups.clear();
    _release(records);
    _notify();
    ActionContainer::_deleteMenus(records);
    return true;
}

qsizetype ActionContainer::itemCount(const QString& group) const
{
    const qsizetype index = groupIndex(group);
    return index < 0 ? 0 : _groups[index].items.size();
}

QList<ActionContainer::Item> ActionContainer::items(const QString& group) const
{
    QList<Item>     result;
    const qsizetype index = groupIndex(group);
    if (index < 0)
    {
        return result;
    }
    for (const auto& record : _groups[index].items)
    {
        if ((record.item.kind == Kind::Action && record.item.action)
            || (record.item.kind == Kind::Menu && record.item.menu))
        {
            result.append(record.item);
        }
    }
    return result;
}

QList<QAction*> ActionContainer::actions(const QString& group) const
{
    QList<QAction*> result;
    for (const auto& item : items(group))
    {
        if (item.kind == Kind::Action)
        {
            result.append(item.action.data());
        }
    }
    return result;
}

ActionContainer::List ActionContainer::menus(const QString& group) const
{
    List result;
    for (const auto& item : items(group))
    {
        if (item.kind == Kind::Menu)
        {
            result.append(item.menu.data());
        }
    }
    return result;
}

void ActionContainer::addAction(QAction* action, const QString& group)
{
    if (!_canMutate())
    {
        throw std::logic_error("Action container cannot be mutated on this thread or during destruction");
    }

    insertAction(action, group, itemCount(group));
}

void ActionContainer::insertAction(QAction* action, const QString& group, qsizetype index)
{
    if (!_canMutate())
    {
        throw std::logic_error("Action container cannot be mutated on this thread or during destruction");
    }
    if (!isAvailable())
    {
        throw std::logic_error("Action container backend is unavailable");
    }
    const qsizetype target = groupIndex(group);
    if (!action || action->thread() != thread() || target < 0 || index < 0 || index > _groups[target].items.size()
        || _actionLocation(action) || !_acceptsAction(action) || !_nextId)
    {
        throw std::invalid_argument(
            "Action is null, duplicated, incompatible, on another thread, or has an invalid destination");
    }

    Record record;
    record.item.action         = action;
    record.id                  = _nextId++;
    record.destroyedConnection = connect(action, &QObject::destroyed, this, [this, id = record.id]() {
        _actionDestroyed(id);
    });
    _groups[target].items.insert(index, std::move(record));
    _notify();
}

bool ActionContainer::moveAction(QAction* action, const QString& group, qsizetype index)
{
    if (!_canMutate())
    {
        return false;
    }
    const auto location = _actionLocation(action);
    return location && _moveItem(*location, group, index);
}

bool ActionContainer::removeAction(QAction* action)
{
    if (!_canMutate())
    {
        return false;
    }
    const auto location = _actionLocation(action);
    if (!_canMutate() || !location)
    {
        return false;
    }
    const auto record = _groups[location->group].items.takeAt(location->item);
    disconnect(record.destroyedConnection);
    _notify();
    return true;
}

void ActionContainer::addMenu(ActionContainer* menu, const QString& group)
{
    if (!_canMutate())
    {
        throw std::logic_error("Action container cannot be mutated on this thread or during destruction");
    }

    insertMenu(menu, group, itemCount(group));
}

void ActionContainer::insertMenu(ActionContainer* menu, const QString& group, qsizetype index)
{
    if (!_canMutate())
    {
        throw std::logic_error("Action container cannot be mutated on this thread or during destruction");
    }
    if (!isAvailable())
    {
        throw std::logic_error("Action container backend is unavailable");
    }
    const qsizetype target = groupIndex(group);
    if (target < 0 || index < 0 || index > _groups[target].items.size() || !_validMenu(menu) || menu->_parentContainer
        || !_nextId)
    {
        throw std::invalid_argument("Menu is already attached, invalid, incompatible, or has an invalid destination");
    }

    Record record;
    record.item.kind = Kind::Menu;
    record.item.menu = menu;
    record.id        = _nextId++;
    _groups[target].items.insert(index, std::move(record));
    menu->_parentContainer = this;
    menu->setParent(this);
    _notify();
}

bool ActionContainer::moveMenu(ActionContainer* menu, const QString& group, qsizetype index)
{
    if (!_canMutate())
    {
        return false;
    }
    const auto location = _menuLocation(menu);
    return location && _moveItem(*location, group, index);
}

bool ActionContainer::transferMenu(ActionContainer* menu,
                                   ActionContainer& target,
                                   const QString&   group,
                                   qsizetype        index)
{
    if (!_canMutate())
    {
        return false;
    }
    if (&target == this)
    {
        return moveMenu(menu, group, index);
    }
    const auto      location    = _menuLocation(menu);
    const qsizetype destination = target.groupIndex(group);
    if (!_canMutate() || !location || !target._canMutate() || !target.isAvailable() || destination < 0 || index < 0
        || index > target._groups[destination].items.size() || !target._validMenu(menu) || !target._nextId)
    {
        return false;
    }

    auto record = _groups[location->group].items.takeAt(location->item);
    record.id   = target._nextId++;
    target._groups[destination].items.insert(index, std::move(record));
    menu->_parentContainer = &target;
    menu->setParent(&target);
    QPointer<ActionContainer> destinationGuard(&target);
    _notify();
    if (destinationGuard)
    {
        destinationGuard->_notify();
    }
    return true;
}

ActionContainer* ActionContainer::takeMenu(ActionContainer* menu)
{
    if (!_canMutate())
    {
        return nullptr;
    }
    const auto location = _menuLocation(menu);
    if (!_canMutate() || !location)
    {
        return nullptr;
    }
    QPointer<ActionContainer> result(menu);
    _forgetMenu(menu);
    return result.data();
}

bool ActionContainer::removeMenu(ActionContainer* menu)
{
    if (!_canMutate())
    {
        return false;
    }
    const auto location = _menuLocation(menu);
    if (!_canMutate() || !location)
    {
        return false;
    }
    QPointer<ActionContainer> removed(menu);
    _forgetMenu(menu);
    delete removed.data();
    return true;
}

bool ActionContainer::_canMutate() const
{
    return !_destroying && QThread::currentThread() == thread();
}

bool ActionContainer::_validMenu(const ActionContainer* menu) const
{
    if (!menu || menu->thread() != thread() || !menu->isAvailable() || menu->_name.isEmpty() || menu->_name == "."
        || menu->_name == ".." || menu->_name.contains('/') || menu->_name.contains('\\'))
    {
        return false;
    }
    for (const QObject* owner = this; owner; owner = owner->parent())
    {
        if (owner == menu)
        {
            return false;
        }
    }
    for (const ActionContainer* current = this; current; current = current->_parentContainer)
    {
        if (current == menu)
        {
            return false;
        }
    }
    for (const auto& group : _groups)
    {
        for (const auto& record : group.items)
        {
            if (record.item.kind == Kind::Menu && record.item.menu && record.item.menu->name() == menu->name())
            {
                return false;
            }
        }
    }
    return _acceptsMenu(menu);
}

std::optional<ActionContainer::Location> ActionContainer::_actionLocation(const QAction* action) const
{
    if (!action)
    {
        return {};
    }
    for (qsizetype group = 0; group < _groups.size(); ++group)
    {
        for (qsizetype item = 0; item < _groups[group].items.size(); ++item)
        {
            if (_groups[group].items[item].item.kind == Kind::Action
                && _groups[group].items[item].item.action.data() == action)
            {
                return Location {group, item};
            }
        }
    }
    return {};
}

std::optional<ActionContainer::Location> ActionContainer::_menuLocation(const ActionContainer* menu) const
{
    if (!menu)
    {
        return {};
    }
    for (qsizetype group = 0; group < _groups.size(); ++group)
    {
        for (qsizetype item = 0; item < _groups[group].items.size(); ++item)
        {
            if (_groups[group].items[item].item.kind == Kind::Menu
                && _groups[group].items[item].item.menu.data() == menu)
            {
                return Location {group, item};
            }
        }
    }
    return {};
}

bool ActionContainer::_moveItem(Location from, const QString& group, qsizetype index)
{
    const qsizetype target = groupIndex(group);
    if (!_canMutate() || !isAvailable() || target < 0 || index < 0
        || index > _groups[target].items.size() - (from.group == target ? 1 : 0))
    {
        return false;
    }
    if (from.group == target && from.item == index)
    {
        return true;
    }
    auto record = _groups[from.group].items.takeAt(from.item);
    _groups[target].items.insert(index, std::move(record));
    _notify();
    return true;
}

void ActionContainer::_actionDestroyed(quint64 id)
{
    for (auto& group : _groups)
    {
        for (qsizetype item = 0; item < group.items.size(); ++item)
        {
            if (group.items[item].id == id)
            {
                group.items.removeAt(item);
                _notify();
                return;
            }
        }
    }
}

void ActionContainer::_forgetMenu(ActionContainer* menu)
{
    const auto location = _menuLocation(menu);
    if (!location)
    {
        return;
    }
    _groups[location->group].items.removeAt(location->item);
    menu->_parentContainer = nullptr;
    if (menu->parent() == this)
    {
        menu->setParent(nullptr);
    }
    _notify();
}

void ActionContainer::_release(QList<Record>& records)
{
    for (auto& record : records)
    {
        disconnect(record.destroyedConnection);
        if (record.item.kind == Kind::Menu && record.item.menu)
        {
            record.item.menu->_parentContainer = nullptr;
            if (record.item.menu->parent() == this)
            {
                record.item.menu->setParent(nullptr);
            }
        }
    }
}

void ActionContainer::_notify()
{
    if (!_destroying)
    {
        emit changed();
    }
}

void ActionContainer::_deleteMenus(const QList<Record>& records)
{
    for (const auto& record : records)
    {
        if (record.item.kind == Kind::Menu)
        {
            delete record.item.menu.data();
        }
    }
}
