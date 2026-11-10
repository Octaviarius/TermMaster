#pragma once

#include <QMap>
#include <QObject>
#include <QStringList>
#include <Termaster/Core/Path.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <functional>
#include <memory>

/** Owns the workspace document and external processor collection. Views use this owner's thread. */
class Workspace : public QObject
{
    Q_OBJECT
public:
    explicit Workspace(Path file, Settings::Node baseSettings);
    Workspace(Path file, SettingsOverlay baseSettings);
    ~Workspace();
    Q_DISABLE_COPY_MOVE(Workspace)

    bool load(QString* error = nullptr);
    bool create(const QString& name, QString* error = nullptr);
    bool sync(QString* error = nullptr);
    /** Validates and serializes a transaction candidate without changing files or borrowed views. */
    std::optional<QByteArray> prepareProcessorDefinitions(const QVariantMap& values, QString* error = nullptr) const;
    using ProcessorFiles = QMap<QString, QByteArray>;
    std::optional<ProcessorFiles> prepareProcessorFiles(const QVariantMap& values, QString* error = nullptr) const;
    ProcessorFiles                processorDiskFiles() const;
    Path                          processorsDirectory() const;
    bool                          acceptProcessorDefinitions(const QVariantMap&    values,
                                                             const ProcessorFiles& files,
                                                             QString*              error = nullptr,
                                                             bool                  dirty = false);
    /** Adopts an already persisted candidate without I/O, retaining borrowed settings views. */
    bool acceptPreparedDocument(const QByteArray& bytes, QString* error = nullptr, bool dirty = false);
    /** Verifies persisted bytes before an application transaction. */
    bool diskUnchanged(QString* error = nullptr) const;
    /** Explicit I/O to refresh only the disk baseline after rollback. */
    bool            captureDiskBaseline(QString* error = nullptr);
    Path            filePath() const;
    QString         name() const;
    bool            setName(const QString& name, QString* error = nullptr);
    bool            isDirty() const;
    SettingsOverlay settings();
    Settings::Node  sessions();
    Settings::Node  state();
    Settings::Node  processorDefinitions();
    bool            setProcessorDefinition(const QString& id, const QVariantMap& definition, QString* error = nullptr);
    bool            removeProcessorDefinition(const QString& id, QString* error = nullptr);
    /** Pure validator borrowed from the application; must not mutate this workspace. */
    void                setProcessorValidator(std::function<bool(const QVariantMap&, QString*)> validator);
    Q_SIGNAL void       processorsChanged();
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
    Path                                              _file;
    SettingsOverlay                                   _baseSettings;
    std::unique_ptr<Settings>                         _settings;
    std::unique_ptr<Settings>                         _processors;
    ProcessorFiles                                    _processorDisk;
    QVariantMap                                       _publishedProcessors;
    bool                                              _processorsPublished = false;
    std::function<bool(const QVariantMap&, QString*)> _processorValidator;
    bool                _validate(const Settings& settings, const Path& file, QString* error) const;
    bool                _readProcessorFiles(const Path&     file,
                                            QVariantMap&    values,
                                            ProcessorFiles& files,
                                            QString*        error,
                                            bool            parse = true) const;
    bool                _validateProcessors(const QVariantMap& values, QString* error) const;
    void                _publishProcessors();
    std::optional<Path> _directory(const QString& key) const;
};
