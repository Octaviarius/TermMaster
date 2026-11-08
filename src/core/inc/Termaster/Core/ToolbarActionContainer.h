#pragma once

#include <QToolBar>
#include <Termaster/Core/ActionContainer.h>

class ToolbarActionContainer : public ActionContainer
{
public:
    ToolbarActionContainer(QToolBar* toolbar);
    virtual ~ToolbarActionContainer();

protected:
    virtual bool _addGroup(QString name);
    virtual bool _addAction(QAction* action, QString group);
    virtual bool _addMenu(ActionContainer* menu, QString group);

private:
    QToolBar*               _toolbar;
    QMap<QString, QAction*> _separators;
};
