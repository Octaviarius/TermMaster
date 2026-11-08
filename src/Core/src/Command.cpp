#include <Termaster/Core/Command.h>

Command::Command(QString      name,
                 QString      text,
                 QIcon        icon,
                 QKeySequence defaultShortcut,
                 QKeySequence shortcut,
                 QObject*     owner) :
    QObject(owner), _name(name), _text(text), _icon(icon), _shortcut(shortcut.isEmpty() ? defaultShortcut : shortcut),
    _defaultShortcut(defaultShortcut)
{
}

Command::~Command()
{
    _destroying        = true;
    const auto actions = _actions;
    _actions.clear();
    for (const auto& action : actions)
    {
        if (action)
        {
            disconnect(action, nullptr, this, nullptr);
        }
    }

    for (const auto& action : actions)
    {
        if (action)
        {
            delete action.data();
        }
    }
}

QString Command::name() const
{
    return _name;
}
QString Command::text() const
{
    return _text;
}
QString Command::tooltip() const
{
    return _tooltip;
}
QIcon Command::icon() const
{
    return _icon;
}
QKeySequence Command::shortcut() const
{
    return _shortcut;
}

QKeySequence Command::defaultShortcut() const
{
    return _defaultShortcut;
}

Command& Command::setText(QString text)
{
    _text              = text;
    const auto actions = _actions;
    for (const auto& action : actions)
    {
        if (action)
        {
            action->setText(text);
        }
    }
    return *this;
}
Command& Command::setTooltip(QString tooltip)
{
    _tooltip           = tooltip;
    const auto actions = _actions;
    for (const auto& action : actions)
    {
        if (action)
        {
            action->setToolTip(tooltip);
        }
    }
    return *this;
}
Command& Command::setIcon(QIcon icon)
{
    _icon              = icon;
    const auto actions = _actions;
    for (const auto& action : actions)
    {
        if (action)
        {
            action->setIcon(icon);
        }
    }
    return *this;
}
Command& Command::setShortcut(QKeySequence shortcuts)
{
    _shortcut = shortcuts;

    const auto actions = _actions;
    for (const auto& action : actions)
    {
        if (action)
        {
            action->setShortcut(_shortcut);
        }
    }

    emit shortcutChanged(this);
    return *this;
}
Command& Command::setDefaultShortcut(QKeySequence shortcut)
{
    _defaultShortcut = shortcut;
    return *this;
}
Command& Command::setSingleInstance(bool isSingle)
{
    const auto existing = actions();
    if (isSingle && existing.size() > 1)
    {
        qWarning() << QString("Command '%1' cannot share %2 existing actions").arg(_name).arg(existing.size());
    }
    else
    {
        _isSingleInstance = isSingle;
        if (isSingle && !existing.isEmpty())
        {
            existing.first()->setParent(this);
        }
    }
    return *this;
}

QList<QAction*> Command::actions() const
{
    QList<QAction*> result;
    for (const auto& action : _actions)
    {
        if (action)
        {
            result.append(action.data());
        }
    }
    return result;
}

QAction* Command::newAction(QObject* parent)
{
    if (_destroying)
    {
        return nullptr;
    }
    const auto existing = actions();
    if (_isSingleInstance && !existing.isEmpty())
    {
        return existing.first();
    }

    auto* action = new QAction(_isSingleInstance ? this : parent);
    action->setIcon(_icon);
    action->setText(_text);
    action->setToolTip(_tooltip);
    action->setShortcut(_shortcut);
    _actions.append(action);
    connect(action, &QObject::destroyed, this, &Command::_actionDestroyed);
    return action;
}

void Command::resetShortcut()
{
    setShortcut(_defaultShortcut);
}
void Command::changeShortcut(QKeySequence shortcut)
{
    setShortcut(shortcut);
}

void Command::_actionDestroyed()
{
    _actions.removeIf([](const QPointer<QAction>& action) { return action.isNull(); });
}
