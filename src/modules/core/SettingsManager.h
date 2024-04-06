#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include "utils/Path.h"
#include "utils/Registree.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSettings>

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

class SettingsManager final
{
public:
    SettingsManager()                       = delete;
    SettingsManager(const SettingsManager&) = delete;

    static const QString mainFileName;

    static const QSettings::Format YamlFormat;
    static Path                    appDataDirPath();

    static SettingsProxy application(QString name = mainFileName, QString nodePath = "");
    static SettingsProxy environment(const Path& path);
};

#endif // SETTINGSMANAGER_H
