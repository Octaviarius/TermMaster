#pragma once

#include "core/ISettings.h"
#include "core/Path.h"
#include "core/Singleton.h"
#include "widgets/config/ConfigContainer.h"

#include <QSerialPortInfo>
#include <QSettings>

class FileSettings : public ISettings
{
public:
    FileSettings(QString filePath, QString prefixPath = "", bool autocreate = true);

    void     _setValue(QString path, const QVariant& v) override;
    QVariant _value(QString path, const QVariant& fallback = QVariant()) override;

    bool exists(QString path) const override;

    void sync() override;

    QSettings& settings();

    ISettings* node(QString prefixPath) override;

private:
    QSettings     _settings;
    PurePosixPath _prefix;
    bool          _autocreate;
};

class UndoableSettings : public ISettings
{
public:
    UndoableSettings(ISettings* settings);

    ISettings* settings() const;
    void       setSettings(ISettings* settings);

    void     _setValue(QString path, const QVariant& v) override;
    QVariant _value(QString path, const QVariant& fallback = QVariant()) override;

    bool exists(QString path) const override;

    void sync() override;

    ISettings* node(QString prefixPath) override;

private:
    ISettings*  _settings;
    QVariantMap _tempValues;
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

    uint latestSessionId();
    uint latestTerminalId();

    ConfigContainer::List configContainers();
    ConfigContainer*      configContainer(QString name);
    bool                  addConfigContainer(ConfigContainer* container);

private:
    ConfigContainer::Map _configContainers;
};
