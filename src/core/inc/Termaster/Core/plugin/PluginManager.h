#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QPluginLoader>
#include <QSet>
#include <QTimer>
#include <Termaster/Core/plugin/Plugin.h>
#include <map>
#include <memory>

class SettingsManager;

/** Owns loaders and exposes the live plugin catalog. Use only on its QObject thread.
 * SettingsManager is borrowed and must outlive the manager. Plugin pointers are
 * deliberately not exposed: consumers must release uses inside their shutdown.
 */
class PluginManager final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum class Status
    {
        Disabled,
        Blocked,
        Stopped,
        Starting,
        Running,
        Stopping,
        Failed,
        Invalid
    };
    Q_ENUM(Status)

    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        VersionRole,
        PathRole,
        EnabledRole,
        StatusRole,
        ErrorRole,
        DependenciesRole
    };

    struct Entry
    {
        Plugin::Info info;
        QString      path;
        bool         enabled = false;
        Status       status  = Status::Stopped;
        QString      error;
    };

private:
    struct Record
    {
        Entry                          entry;
        QByteArray                     fingerprint;
        QString                        validationError;
        QString                        lifecycleError;
        std::unique_ptr<QPluginLoader> loader;
        Plugin*                        instance         = nullptr;
        bool                           shutdownComplete = false;
        bool                           failed           = false;
    };

    SettingsManager&                           _settings;
    QString                                    _directory;
    QFileSystemWatcher                         _watcher;
    QTimer                                     _debounce;
    QTimer                                     _poll;
    std::map<QString, std::unique_ptr<Record>> _records;
    QStringList                                _loadedOrder;
    QHash<QString, bool>                       _enabled;
    bool                                       _active      = false;
    bool                                       _busy        = false;
    bool                                       _pendingScan = false;

    bool          _checkCall(QString* error) const;
    void          _scheduleScan();
    bool          _scan(QString* error, bool force = false);
    bool          _reconcile(QString* error);
    void          _validateGraph(QStringList& order, QHash<QString, Record*>& byId);
    bool          _startRecord(Record& record);
    bool          _stopRecord(Record& record);
    bool          _stopAffected(QSet<QString> paths);
    QSet<QString> _dependentClosure(QSet<QString> paths) const;
    void          _readDescription(Record& record);
    void          _updateWatches(const QStringList& paths);
    void          _report(const QString& error);

public:
    explicit PluginManager(SettingsManager& settings, QString directory, QObject* parent = nullptr);
    ~PluginManager() override;
    Q_DISABLE_COPY_MOVE(PluginManager)

    int                    rowCount(const QModelIndex& parent = {}) const override;
    QVariant               data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QList<Entry> plugins() const;
    QString      directory() const;
    bool         start(QString* error = nullptr);
    bool         stop(QString* error = nullptr);
    bool         rescan(QString* error = nullptr);
    bool         setEnabled(const QString& id, bool enabled, QString* error = nullptr);
    bool         reload(const QString& id, QString* error = nullptr);

signals:
    void pluginStarted(const QString& id);
    void pluginStopped(const QString& id);
    void errorOccurred(const QString& error);
};
