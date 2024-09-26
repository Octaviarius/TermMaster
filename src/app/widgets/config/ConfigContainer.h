#ifndef CONFIGCONTAINER_H
#define CONFIGCONTAINER_H

#include "IConfigWidget.h"

class ConfigContainer
{
public:
    using List = QList<ConfigContainer*>;
    using Map  = QMap<QString, ConfigContainer*>;

    ConfigContainer(QString name, QString title = "", QString tooltip = "", QIcon icon = QIcon());

    QString name() const;
    QString title() const;
    QString tooltip() const;
    QIcon   icon() const;

    bool addContainer(ConfigContainer* container);
    List containers() const;

    bool                                 addWidgetConstructor(IConfigWidget::Constructor constructor);
    QList<IConfigWidget::Constructor> widgetConstructors() const;

private:
    QString _name;
    QString _title;
    QString _tooltip;
    QIcon   _icon;

    List                                 _containers;
    QList<IConfigWidget::Constructor> _constructors;
};

#endif // CONFIGCONTAINER_H
