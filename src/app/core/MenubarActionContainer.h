#pragma once

#include "ActionContainer.h"

#include <QMenuBar>

class MenubarActionContainer : public ActionContainer
{
public:
    MenubarActionContainer(QMenuBar* menubar = nullptr);
    virtual ~MenubarActionContainer();

    QMenuBar* menubar();

protected:
    virtual bool _addGroup(QString name);
    virtual bool _addAction(QAction* action, QString group);
    virtual bool _addMenu(ActionContainer* menu, QString group);

private:
    QMenuBar*               _menubar;
    QMap<QString, QAction*> _separators;
};
