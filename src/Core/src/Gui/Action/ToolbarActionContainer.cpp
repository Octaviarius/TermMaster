#include <QMenu>
#include <QToolButton>
#include <Termaster/Core/Gui/Action/ToolbarActionContainer.h>
#include <utility>

ToolbarActionContainer::ToolbarActionContainer(QToolBar* toolbar, QObject* owner) :
    ActionContainerAdapter({},
                           toolbar ? toolbar->windowTitle() : QString(),
                           toolbar,
                           WidgetOwnership::Borrowed,
                           owner ? owner : toolbar)
{
    if (toolbar)
    {
        connect(this, &ActionContainer::titleChanged, toolbar, &QWidget::setWindowTitle);
    }
}

QToolBar* ToolbarActionContainer::toolbar() const
{
    return qobject_cast<QToolBar*>(widget());
}

QAction* ToolbarActionContainer::_menuAction() const
{
    return nullptr;
}

void ToolbarActionContainer::_configureAction(QAction* action)
{
    if (toolbar() && action->menu())
    {
        if (auto button = qobject_cast<QToolButton*>(toolbar()->widgetForAction(action)))
        {
            button->setMenu(action->menu());
            button->setPopupMode(QToolButton::InstantPopup);
        }
    }
}
