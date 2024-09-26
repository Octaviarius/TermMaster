#include "ToolbarActionContainer.h"

ToolbarActionContainer::ToolbarActionContainer(QToolBar* toolbar) : ActionContainer(""), _toolbar(toolbar)
{
}

ToolbarActionContainer::~ToolbarActionContainer()
{
}

bool ToolbarActionContainer::_addGroup(QString name)
{
    auto action = new QAction();
    action->setSeparator(true);
    _toolbar->addAction(action);
    _separators[name] = action;
    return true;
}

bool ToolbarActionContainer::_addAction(QAction* action, QString group)
{
    _toolbar->insertAction(_separators[group], action);
    return true;
}

bool ToolbarActionContainer::_addMenu(ActionContainer* menu, QString group)
{
    qCritical() << QString("Toolbar '%1' doesn't support menus").arg(path());
    return false;
}
