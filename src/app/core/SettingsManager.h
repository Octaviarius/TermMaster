#pragma once

#include "utils/Path.h"
#include "utils/Registree.h"
#include "utils/Singleton.h"
#include "utils/utils.h"

#include <QIcon>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QVersionNumber>

struct SettingCategory
{
    using List = QList<SettingCategory>;
    using Map  = QMap<QString, SettingCategory>;

    QString name;
    QIcon   icon;
};

/*
class Setting
{
public:
    Setting(QString filePath);
    ~Setting();

    void      setValue(QString path, const QVariant& value);
    QVariant& value(QString path, const QVariant& defaultValue);
    QVariant& value(QString path);

    QVariant constValue(QString path, const QVariant& defaultValue) const;
    QVariant constValue(QString path) const;

    Setting node(QString relPath);

    QVariant  operator[](QString path) const;
    QVariant& operator[](QString path);

    void save();
    void load(bool clean = true);
    void sync();

private:
    QSharedPointer<QMutex> _mutex;
    Registree              _reg;
    QString                _filePath;
};
*/

class SettingsProxy
{
public:
    SettingsProxy(const QSettings& settings = QSettings(), QString prefixPath = "", bool autocreate = true);
    SettingsProxy(const SettingsProxy& copy);

    void     setValue(QString path, const QVariant& v);
    QVariant value(QString path, const QVariant& defaultValue = QVariant());

    void       sync();
    QSettings& settings();

    SettingsProxy node(QString prefixPath);

private:
    QSharedPointer<QSettings> _settings;
    bool                      _autocreate;
};

class SettingsManager : public Singleton<SettingsManager>
{
public:
    friend Singleton<SettingsManager>;

    static const QString        shortVersion;
    static const QString        longVersion;
    static const QVersionNumber version;
    static const BuildTime      buildTime;

    static const QSettings::Format YamlFormat;
    Path                           appDataDirPath();
    Path                           sessionsDirPath();
    Path                           terminalsDirPath();
    Path                           generalSettingsFilePath();

    SettingsProxy applicationSettings(QString fileName, QString nodePath = "");
    SettingsProxy environment(const Path& path);

    SettingsProxy generalSettings(QString nodePath = "");
    SettingsProxy session(QString name);
    SettingsProxy terminal(QString name);

    void                        addCategory(const SettingCategory& cat);
    void                        addCategory(const SettingCategory::List& cats);
    const SettingCategory::Map& categories() const;

private:
    SettingCategory::Map _categories;
};
