#pragma once

#include <QStringList>
#include <Termaster/Core/Path.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <memory>

/** Owns one workspace YAML file. Views borrow this owner and use its thread. */
class Workspace
{
public:
    explicit Workspace(Path file, Settings::Node baseSettings);
    Workspace(Path file, SettingsOverlay baseSettings);
    ~Workspace();
    Q_DISABLE_COPY_MOVE(Workspace)

    bool                load(QString* error = nullptr);
    bool                create(const QString& name, QString* error = nullptr);
    bool                sync(QString* error = nullptr);
    Path                filePath() const;
    QString             name() const;
    bool                setName(const QString& name, QString* error = nullptr);
    bool                isDirty() const;
    SettingsOverlay     settings();
    Settings::Node      sessions();
    Settings::Node      state();
    Settings::Node      processorBlocks();
    bool                setProcessorBlock(const QString& id, const QVariantMap& definition, QString* error = nullptr);
    bool                removeProcessorBlock(const QString& id, QString* error = nullptr);
    Path                sessionsDirectory() const;
    QStringList         sessionIds() const;
    std::optional<Path> sessionDirectory(const QString& id, QString* error = nullptr) const;
    bool                registerSession(const QString& id, const Path& directory, QString* error = nullptr);
    /** Removes catalog and history references without deleting session files. */
    bool unregisterSession(const QString& id, QString* error = nullptr);
    /** Returns recent registered session IDs, most recent first, without modifying state. */
    QStringList         recentSessions() const;
    bool                rememberSession(const QString& id, QString* error = nullptr);
    std::optional<Path> pluginDirectory() const;
    std::optional<Path> scriptDirectory() const;

private:
    Path                      _file;
    SettingsOverlay           _baseSettings;
    std::unique_ptr<Settings> _settings;
    static bool               _validate(const Settings& settings, const Path& file, QString* error);
    std::optional<Path>       _directory(const QString& key) const;
};
