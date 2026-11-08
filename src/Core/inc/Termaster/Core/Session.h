#pragma once

#include <Termaster/Core/Path.h>
#include <Termaster/Core/SettingsOverlay.h>

/** Owns a session directory document. Borrowed views share the owner's thread and lifetime. */
class Session
{
public:
    Session(Path directory, SettingsOverlay baseSettings);
    ~Session();
    Q_DISABLE_COPY_MOVE(Session)

    bool            load(QString* error = nullptr);
    bool            create(const QString& id, const QString& name, QString* error = nullptr);
    bool            sync(QString* error = nullptr);
    QString         id() const;
    QString         name() const;
    bool            setName(const QString& name, QString* error = nullptr);
    bool            isDirty() const;
    Path            directory() const;
    Path            filePath() const;
    SettingsOverlay settings();
    Settings::Node  objects();
    Settings::Node  links();
    Settings::Node  resources();
    Settings::Node  presentation();
    /** Resolves a resource after checking lexical and existing symlink ancestry confinement. */
    std::optional<Path> resourcePath(const QString& id, QString* error = nullptr) const;

private:
    Path                      _directory;
    SettingsOverlay           _baseSettings;
    std::unique_ptr<Settings> _settings;
    bool                      _validate(const Settings& settings, QString* error) const;
    std::optional<Path>       _resourcePath(const QVariant& value, QString* error) const;
};
