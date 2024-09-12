#include "MenuActionContainer.h"

MenuActionContainer::MenuActionContainer(QString name, QString title) : ActionContainer(name, title)
{
    _menu = new QMenu();
    _menu->setTitle(QObject::tr(this->title().toLatin1().constData()));
}

MenuActionContainer::~MenuActionContainer()
{
    delete _menu;
}

QMenu* MenuActionContainer::menu()
{
    return _menu;
}

bool MenuActionContainer::_addGroup(QString name)
{
    auto action = new QAction();
    action->setSeparator(true);
    _menu->addAction(action);
    _separators[name] = action;
    return true;
}

bool MenuActionContainer::_addAction(QAction* action, QString group)
{
    _menu->insertAction(_separators[group], action);
    return true;
}

bool MenuActionContainer::_addMenu(ActionContainer* menu, QString group)
{
    bool ret = false;

    auto m = dynamic_cast<MenuActionContainer*>(menu);
    if (!m)
    {
        qCritical() << QString("Container '{1}' is incompatible with 'MenuActionContainer' type").arg(menu->path());
    }
    else
    {
        _menu->insertMenu(_separators[group], m->menu());
        ret = true;
    }

    return ret;
}
