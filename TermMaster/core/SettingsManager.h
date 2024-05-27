#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include "core/Registree.h"

#include <QSettings>
#include <QString>

class SettingsManager
{
public:
    SettingsManager() = delete;

    static QSettings application();
    static QSettings environment(QString path);
};

#endif // SETTINGSMANAGER_H
