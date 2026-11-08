#include <QTimer>
#include <Termaster/Core/gui/action/ActionContainerAdapter.h>
#include <utility>

ActionContainerAdapter::ActionContainerAdapter(QString         name,
                                               QString         title,
                                               QWidget*        widget,
                                               WidgetOwnership ownership,
                                               QObject*        owner) :
    ActionContainer(std::move(name), std::move(title), owner), _widget(widget), _ownership(ownership)
{
    _changedConnection = connect(this, &ActionContainer::changed, this, &ActionContainerAdapter::_synchronize);
    if (widget)
    {
        _widgetConnection = connect(widget, &QObject::destroyed, this, [this]() {
            if (_tearingDown)
            {
                return;
            }
            _widget = nullptr;
            QPointer<ActionContainerAdapter> self(this);
            _clearProjection();
            if (!self)
            {
                return;
            }
            // The menu presentation has disappeared; remove its owned logical node too.
            if (auto parent = parentContainer())
            {
                parent->removeMenu(this);
            }
        });
    }
}

ActionContainerAdapter::~ActionContainerAdapter()
{
    _tearingDown = true;
    disconnect(_changedConnection);
    disconnect(_widgetConnection);
    _clearProjection();
    if (_ownership == WidgetOwnership::Owned)
    {
        delete _widget.data();
    }
}

QWidget* ActionContainerAdapter::widget() const
{
    return _widget.data();
}

bool ActionContainerAdapter::_isAvailable() const
{
    return !_tearingDown && !_widget.isNull();
}

bool ActionContainerAdapter::_acceptsAction(const QAction* action) const
{
    if (action == _menuAction())
    {
        return false;
    }
    for (const auto& separator : _separators)
    {
        if (separator.data() == action)
        {
            return false;
        }
    }
    for (const auto& group : groups())
    {
        for (const auto& item : items(group))
        {
            if (item.kind == Kind::Menu)
            {
                if (auto child = dynamic_cast<const ActionContainerAdapter*>(item.menu.data());
                    child && child->_menuAction() == action)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

bool ActionContainerAdapter::_acceptsMenu(const ActionContainer* menu) const
{
    const auto child = dynamic_cast<const ActionContainerAdapter*>(menu);
    if (!child || !child->_menuAction())
    {
        return false;
    }
    for (const auto& group : groups())
    {
        if (actions(group).contains(child->_menuAction()))
        {
            return false;
        }
    }
    return true;
}

void ActionContainerAdapter::_configureAction(QAction*)
{
}

void ActionContainerAdapter::_synchronize()
{
    if (_tearingDown || !_widget)
    {
        return;
    }
    if (_projecting)
    {
        _dirty = true;
        return;
    }
    _projecting = true;
    QPointer<ActionContainerAdapter> self(this);
    for (int pass = 0; pass < 8; ++pass)
    {
        _dirty             = false;
        const auto desired = _desiredActions();
        if (!self)
        {
            return;
        }
        QPointer<QWidget> target(_widget);
        if (!target)
        {
            _projecting = false;
            return;
        }
        const auto old = _rendered;
        for (const auto& action : old)
        {
            if (action && !desired.contains(action))
            {
                target->removeAction(action);
            }
            if (!self)
            {
                return;
            }
            if (!target)
            {
                _projecting = false;
                return;
            }
        }
        QPointer<QAction> before;
        for (auto it = desired.crbegin(); it != desired.crend(); ++it)
        {
            const QPointer<QAction> action = *it;
            if (!action)
            {
                continue;
            }
            const auto      current = target->actions();
            const qsizetype at      = current.indexOf(action.data());
            const qsizetype next    = before ? current.indexOf(before.data()) : current.size();
            if (at < 0 || at + 1 != next)
            {
                target->insertAction(before.data(), action.data());
            }
            if (!self)
            {
                return;
            }
            if (!target)
            {
                _projecting = false;
                return;
            }
            if (action)
            {
                _configureAction(action);
            }
            if (!self)
            {
                return;
            }
            if (!target)
            {
                _projecting = false;
                return;
            }
            before = action;
        }
        _rendered = desired;
        if (!_dirty)
        {
            break;
        }
    }
    _projecting = false;
    if (_dirty)
    {
        QTimer::singleShot(0, this, &ActionContainerAdapter::_synchronize);
    }
}

QList<QPointer<QAction>> ActionContainerAdapter::_desiredActions()
{
    const QStringList names = groups();
    for (const auto& old : _separators.keys())
    {
        if (!names.contains(old))
        {
            delete _separators.take(old).data();
        }
    }

    QList<QPointer<QAction>> result;
    for (const auto& group : names)
    {
        const auto entries = items(group);
        if (entries.isEmpty())
        {
            continue;
        }
        if (!result.isEmpty())
        {
            if (!_separators.value(group))
            {
                auto separator = new QAction(this);
                separator->setSeparator(true);
                _separators[group] = separator;
            }
            result.append(_separators[group]);
        }
        for (const auto& item : entries)
        {
            if (item.kind == Kind::Action)
            {
                result.append(item.action);
            }
            else if (auto child = dynamic_cast<const ActionContainerAdapter*>(item.menu.data()))
            {
                if (auto action = child->_menuAction())
                {
                    result.append(action);
                }
            }
        }
    }
    return result;
}

void ActionContainerAdapter::_clearProjection()
{
    QPointer<ActionContainerAdapter> self(this);
    QPointer<QWidget>                target(_widget);
    const auto                       rendered = std::exchange(_rendered, {});
    for (const auto& action : rendered)
    {
        if (!target)
        {
            break;
        }
        if (action)
        {
            target->removeAction(action);
        }
        if (!self)
        {
            return;
        }
    }
    const auto separators = std::exchange(_separators, {});
    for (const auto& separator : separators)
    {
        delete separator.data();
    }
}
