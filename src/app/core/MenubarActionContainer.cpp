#include "MenubarActionContainer.h"

#include "MenuActionContainer.h"

MenubarActionContainer::MenubarActionContainer(QMenuBar* menubar) : ActionContainer("")
{
    _menubar = menubar;
}

MenubarActionContainer::~MenubarActionContainer()
{
}

QMenuBar* MenubarActionContainer::menubar()
{
    return _menubar;
}

bool MenubarActionContainer::_addGroup(QString name)
{
    auto action = new QAction();
    action->setSeparator(true);
    _menubar->addAction(action);
    _separators[name] = action;
    return true;
}

bool MenubarActionContainer::_addAction(QAction* action, QString group)
{
    _menubar->insertAction(_separators[group], action);
    return true;
}

bool MenubarActionContainer::_addMenu(ActionContainer* menu, QString group)
{
    bool ret = false;

    auto m = dynamic_cast<MenuActionContainer*>(menu);

    if (!m)
    {
        qCritical() << QString("Container '{1}' is incompatible with 'MenuActionContainer' type").arg(menu->path());
    }
    else
    {
        _menubar->insertMenu(_separators[group], m->menu());
        ret = true;
    }

    return ret;
}
