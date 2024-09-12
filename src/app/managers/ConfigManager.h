#pragma once

#include "core/Path.h"
#include "core/Singleton.h"

#include <QSettings>

class Settings
{
public:
    Settings(const QSettings& settings = QSettings(), QString prefixPath = "", bool autocreate = true);
    Settings(const Settings& copy);

    void     setValue(QString path, const QVariant& v);
    QVariant value(QString path, const QVariant& defaultValue = QVariant());

    void       sync();
    QSettings& settings();

    Settings node(QString prefixPath);

private:
    QSharedPointer<QSettings> _settings;
    PurePosixPath             _prefix;
    bool                      _autocreate;
};

class ConfigManager : public Singleton<ConfigManager>
{
public:
    friend Singleton<ConfigManager>;

    static const QSettings::Format YamlFormat;

    Path appDataDirPath();
    Path sessionsDirPath();
    Path terminalsDirPath();
    Path generalSettingsFilePath();

    Settings applicationSettings(QString fileName, QString nodePath = "");
    Settings environment(const Path& path);

    Settings generalSettings(QString nodePath = "");
    Settings session(QString name);
    Settings terminal(QString name);
};
