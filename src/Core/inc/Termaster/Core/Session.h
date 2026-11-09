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

    bool load(QString* error = nullptr);
    bool create(const QString& id, const QString& name, QString* error = nullptr);
    bool sync(QString* error = nullptr);
    /** Validates and serializes a transaction candidate without changing files or borrowed views. */
    std::optional<QByteArray> prepareObjects(const QVariantMap& values, QString* error = nullptr) const;
    /** Adopts an already persisted candidate without I/O, retaining borrowed settings views. */
    bool acceptPreparedDocument(const QByteArray& bytes, QString* error = nullptr, bool dirty = false);
    /** Verifies persisted bytes before an application transaction. */
    bool diskUnchanged(QString* error = nullptr) const;
    /** Explicit I/O to refresh only the disk baseline after rollback. */
    bool            captureDiskBaseline(QString* error = nullptr);
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
