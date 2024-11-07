#pragma once

#include "core/Path.h"
#include "core/Settings.h"
#include "core/Singleton.h"
#include "widgets/config/ConfigContainer.h"

#include <QSerialPortInfo>
#include <QSettings>

class FileSettings : public Settings
{
public:
    FileSettings(QString filePath, QString prefixPath = "", bool autocreate = true);
};

class SettingsManager : public Singleton<SettingsManager>
{
public:
    friend Singleton<SettingsManager>;

    static const QSettings::Format YamlFormat;

    PosixPath appDataDirPath();
    PosixPath sessionsDirPath();
    PosixPath terminalsDirPath();
    PosixPath generalSettingsFilePath();

    FileSettings* applicationSettings(QString fileName, QString nodePath = "");
    FileSettings* environment(const PosixPath& path);

    FileSettings* generalSettings(QString nodePath = "");
    FileSettings* session(uint id);
    FileSettings* terminal(uint id);

    int latestSessionId();
    int latestTerminalId();

    ConfigContainer::List configContainers();
    ConfigContainer*      configContainer(QString name);
    bool                  addConfigContainer(ConfigContainer* container);

private:
    ConfigContainer::Map _configContainers;
};
