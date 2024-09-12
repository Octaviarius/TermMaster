#include "Command.h"

Command::Command(QString name, QString text, QIcon icon, QKeySequence defaultShortcut, QKeySequence shortcut) :
    QObject(), _name(name), _text(text), _isSingleInstance(false), _icon(icon), _shortcut(shortcut),
    _defaultShortcut(defaultShortcut)
{
}

Command::~Command()
{
    for (auto action : _actions)
    {
        delete action;
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

Command& Command::setName(QString name)
{
    _name = name;
    return *this;
}
Command& Command::setText(QString text)
{
    _text = text;
    for (auto action : _actions)
    {
        action->setText(tr(_text.toLatin1().data()));
    }
    return *this;
}
Command& Command::setTooltip(QString tooltip)
{
    _tooltip = tooltip;
    for (auto action : _actions)
    {
        action->setToolTip(tr(_tooltip.toLatin1().data()));
    }
    return *this;
}
Command& Command::setIcon(QIcon icon)
{
    _icon = icon;
    for (auto action : _actions)
    {
        action->setIcon(icon);
    }
    return *this;
}
Command& Command::setShortcut(QKeySequence shortcuts)
{
    _shortcut = shortcuts;

    for (auto action : _actions)
    {
        action->setShortcut(_shortcut);
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
    if (_actions.count() > 1)
    {
        qWarning() << QString("Command '%1' can't be set as single, it has already %2 instances")
                          .arg(name())
                          .arg(_actions.count());
    }
    else
    {
        _isSingleInstance = isSingle;
    }

    return *this;
}

const QList<QAction*> Command::actions() const
{
    return _actions;
}

QAction* Command::newAction(QObject* parent)
{
    QAction* action = nullptr;

    if (!_isSingleInstance)
    {
        action = new QAction(parent);
    }
    else
    {
        parent = this;
        if (_actions.empty())
        {
            action = new QAction(parent);
        }
    }

    if (!action)
    {
        action = _actions.first();
    }
    else
    {
        _actions += action;

        action->setIcon(_icon);
        action->setText(_text);
        action->setToolTip(_tooltip);
        action->setShortcut(_shortcut);

        connect(action, &QAction::destroyed, this, &Command::_actionDestroyed);
    }

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

void Command::_actionDestroyed(QObject* action)
{
    _actions.removeOne(action);
}
