#pragma once

#include "widgets/config/ConfigBaseWidget.h"

#include <QIcon>
#include <QList>
#include <QMap>

class SettingEntry
{
public:
    using List = QList<SettingEntry*>;
    SettingEntry(QString name, QString title = "", QString tooltip = "", QIcon icon = QIcon());

private:
};

class SettingContainer
{
public:
    using List = QList<SettingContainer*>;
    using Map  = QMap<QString, SettingContainer*>;

    SettingContainer(QString name, QString title = "", QString tooltip = "", QIcon icon = QIcon());
    ~SettingContainer();

protected:
    List _containers;
    List _entries;

private:
    QString _name;
    QString _title;
    QString _tooltip;
    QIcon   _icon;

    SettingContainer* _parent;
};
