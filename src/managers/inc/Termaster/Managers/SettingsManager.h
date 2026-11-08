#pragma once

#include <QHash>
#include <QSerialPortInfo>
#include <QSettings>
#include <Termaster/Core/Path.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/Singleton.h>
#include <Termaster/Widgets/gui/settings/ConfigContainer.h>

class FileSettings : public Settings
{
public:
    FileSettings(QString filePath, QString prefixPath = "", bool autocreate = true);
};

class SettingsManager : public Singleton<SettingsManager>
{
private:
    QString              _pluginSettingsPath;
    ConfigContainer::Map _configContainers;

public:
    friend Singleton<SettingsManager>;

    explicit SettingsManager(QString pluginSettingsPath = QString());

    bool loadPluginStates(QHash<QString, bool>& states, QString* error = nullptr);
    bool savePluginState(const QString& id, bool enabled, QString* error = nullptr);

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
};
