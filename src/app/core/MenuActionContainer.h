#pragma once

#include "ActionContainer.h"

#include <QMenu>

class MenuActionContainer : public ActionContainer
{
public:
    MenuActionContainer(QString name, QString title = "");
    virtual ~MenuActionContainer();

    QMenu* menu();

protected:
    virtual bool _addGroup(QString name);
    virtual bool _addAction(QAction* action, QString group);
    virtual bool _addMenu(ActionContainer* menu, QString group);

private:
    QMenu*                 _menu;
    QMap<QString, QAction*> _separators;
};
