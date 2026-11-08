#include <Termaster/Core/MenubarActionContainer.h>
#include <utility>

MenubarActionContainer::MenubarActionContainer(QMenuBar* menubar, QObject* owner) :
    ActionContainerAdapter({}, {}, menubar, WidgetOwnership::Borrowed, owner ? owner : menubar)
{
}

QMenuBar* MenubarActionContainer::menubar() const
{
    return qobject_cast<QMenuBar*>(widget());
}

QAction* MenubarActionContainer::_menuAction() const
{
    return nullptr;
}
