#include "ConfigContainer.h"

ConfigContainer::ConfigContainer(QString name, QString title, QString tooltip, QIcon icon) :
    _name(name), _title(_title == "" ? _name : _title), _tooltip(tooltip), _icon(icon)
{
}

QString ConfigContainer::name() const
{
    return _name;
}

QString ConfigContainer::title() const
{
    return _title;
}

QString ConfigContainer::tooltip() const
{
    return _tooltip;
}

QIcon ConfigContainer::icon() const
{
    return _icon;
}

bool ConfigContainer::addContainer(ConfigContainer* container)
{
    _containers.append(container);
    return true;
}

ConfigContainer::List ConfigContainer::containers() const
{
    return _containers;
}

bool ConfigContainer::addWidgetConstructor(IConfigWidget::Constructor constructor)
{
    _constructors.append(constructor);
    return true;
}

QList<IConfigWidget::Constructor> ConfigContainer::widgetConstructors() const
{
    return _constructors;
}
