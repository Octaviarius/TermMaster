#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <utility>

MenuActionContainer::MenuActionContainer(QString name, QString title, QObject* owner) :
    ActionContainerAdapter(std::move(name), std::move(title), new QMenu(), WidgetOwnership::Owned, owner)
{
    menu()->setTitle(this->title());
    connect(this, &ActionContainer::titleChanged, menu(), &QMenu::setTitle);
}

QMenu* MenuActionContainer::menu() const
{
    return qobject_cast<QMenu*>(widget());
}

QAction* MenuActionContainer::_menuAction() const
{
    return menu() ? menu()->menuAction() : nullptr;
}
