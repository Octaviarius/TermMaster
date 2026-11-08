#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLibrary>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QThread>
#include <Termaster/Core/plugin/PluginManager.h>
#include <Termaster/Managers/SettingsManager.h>
#include <algorithm>
#include <exception>

PluginManager::PluginManager(SettingsManager& settings, QString directory, QObject* parent) :
    QAbstractListModel(parent), _settings(settings), _directory(QDir(directory).absolutePath()), _watcher(this),
    _debounce(this), _poll(this)
{
    _debounce.setSingleShot(true);
    _debounce.setInterval(150);
    _poll.setInterval(2000);

    connect(&_watcher, &QFileSystemWatcher::directoryChanged, this, &PluginManager::_scheduleScan);
    connect(&_watcher, &QFileSystemWatcher::fileChanged, this, &PluginManager::_scheduleScan);
    connect(&_debounce, &QTimer::timeout, this, [this]() { rescan(); });
    connect(&_poll, &QTimer::timeout, this, [this]() { rescan(); });
}

PluginManager::~PluginManager()
{
    stop();
    // A non-cooperative plugin must never have its executing code unmapped.
    for (auto& [path, record] : _records)
    {
        if (record->loader && record->loader->isLoaded())
        {
            qCritical() << "Plugin remains pinned at process shutdown:" << path << record->entry.error;
            record->loader.release();
        }
    }
}

int PluginManager::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(_records.size());
}

QVariant PluginManager::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0
        || index.row() >= rowCount())
    {
        return {};
    }

    auto it = _records.begin();
    std::advance(it, index.row());
    const auto& entry = it->second->entry;

    switch (role)
    {
        case Qt::DisplayRole:
        case NameRole:
            return entry.info.name;
        case IdRole:
            return entry.info.id;
        case VersionRole:
            return entry.info.version;
        case PathRole:
            return entry.path;
        case EnabledRole:
            return entry.enabled;
        case StatusRole:
            return static_cast<int>(entry.status);
        case ErrorRole:
            return entry.error;
        case DependenciesRole:
            return entry.info.dependencies;
        default:
            return {};
    }
}

QHash<int, QByteArray> PluginManager::roleNames() const
{
    return {
        {          IdRole,           "id"},
        {        NameRole,         "name"},
        {     VersionRole,      "version"},
        {        PathRole,         "path"},
        {     EnabledRole,      "enabled"},
        {      StatusRole,       "status"},
        {       ErrorRole,        "error"},
        {DependenciesRole, "dependencies"}
    };
}

QList<PluginManager::Entry> PluginManager::plugins() const
{
    QList<Entry> result;
    for (const auto& [path, record] : _records)
    {
        result.append(record->entry);
    }
    return result;
}

QString PluginManager::directory() const
{
    return _directory;
}

bool PluginManager::start(QString* error)
{
    if (!_checkCall(error))
    {
        return false;
    }
    if (_active)
    {
        return true;
    }

    QScopedValueRollback guard(_busy, true);
    QString              cause;
    if (!_settings.loadPluginStates(_enabled, &cause))
    {
        if (error)
        {
            *error = cause;
        }
        _report(cause);
        return false;
    }

    _active = true;
    _poll.start();
    return _scan(error, true);
}

bool PluginManager::stop(QString* error)
{
    if (!_checkCall(error))
    {
        return false;
    }

    QScopedValueRollback guard(_busy, true);
    _active = false;
    _debounce.stop();
    _poll.stop();
    beginResetModel();
    QSet<QString> all;
    for (const auto& path : _loadedOrder)
    {
        all.insert(path);
    }
    const bool success = _stopAffected(all);
    endResetModel();
    if (!success && error)
    {
        *error = QStringLiteral("Some plugins could not shut down or unload");
    }
    return success;
}

bool PluginManager::rescan(QString* error)
{
    if (!_checkCall(error))
    {
        if (_busy)
        {
            _pendingScan = true;
        }
        return false;
    }
    if (!_active)
    {
        if (error)
        {
            *error = QStringLiteral("Plugin manager is stopped");
        }
        return false;
    }

    QScopedValueRollback guard(_busy, true);
    return _scan(error);
}

bool PluginManager::setEnabled(const QString& id, bool enabled, QString* error)
{
    if (!_checkCall(error))
    {
        return false;
    }
    if (!_active)
    {
        if (error)
        {
            *error = QStringLiteral("Plugin manager is stopped");
        }
        return false;
    }

    QScopedValueRollback guard(_busy, true);
    QString              cause;
    if (!_settings.savePluginState(id, enabled, &cause))
    {
        if (error)
        {
            *error = cause;
        }
        _report(cause);
        return false;
    }

    _enabled[id] = enabled;
    beginResetModel();
    QSet<QString> affected;
    for (auto& [path, record] : _records)
    {
        record->entry.enabled = _enabled.value(record->entry.info.id, false);
        if (record->entry.info.id == id)
        {
            affected.insert(path);
        }
    }
    const auto retryPaths = _dependentClosure(affected);
    for (const auto& path : retryPaths)
    {
        auto& record = *_records.at(path);
        if (!record.loader)
        {
            record.failed = false;
        }
    }
    bool success = true;
    if (!enabled)
    {
        success = _stopAffected(_dependentClosure(affected));
    }
    success = _reconcile(error) && success;
    endResetModel();
    if (!success && error && error->isEmpty())
    {
        *error = QStringLiteral("Plugin transition failed");
    }
    return success;
}

bool PluginManager::reload(const QString& id, QString* error)
{
    if (!_checkCall(error))
    {
        return false;
    }
    if (!_active)
    {
        if (error)
        {
            *error = QStringLiteral("Plugin manager is stopped");
        }
        return false;
    }

    QScopedValueRollback guard(_busy, true);
    QSet<QString>        paths;
    for (auto& [path, record] : _records)
    {
        if (record->entry.info.id == id)
        {
            paths.insert(path);
        }
    }
    if (paths.isEmpty())
    {
        if (error)
        {
            *error = QStringLiteral("Unknown plugin: %1").arg(id);
        }
        return false;
    }

    beginResetModel();
    const auto retryPaths = _dependentClosure(paths);
    const bool stopped    = _stopAffected(retryPaths);
    for (auto& [path, record] : _records)
    {
        if (retryPaths.contains(path) && !record->loader)
        {
            record->failed = false;
        }
    }
    const bool started = _reconcile(error);
    endResetModel();
    if (!stopped && error)
    {
        *error = QStringLiteral("Reload blocked by incomplete shutdown");
    }
    return stopped && started;
}

bool PluginManager::_checkCall(QString* error) const
{
    if (error)
    {
        error->clear();
    }
    if (QThread::currentThread() != thread() || _busy)
    {
        if (error)
        {
            *error = QStringLiteral("Plugin manager requires a non-reentrant call on its owner thread");
        }
        return false;
    }
    return true;
}

void PluginManager::_scheduleScan()
{
    if (_active)
    {
        _debounce.start();
    }
}

bool PluginManager::_scan(QString* error, bool force)
{
    QHash<QString, QByteArray> files;
    QStringList                watched;
    QString                    ancestor = _directory;
    while (!QFileInfo::exists(ancestor) && QFileInfo(ancestor).dir().absolutePath() != ancestor)
    {
        ancestor = QFileInfo(ancestor).dir().absolutePath();
    }
    watched.append(ancestor);

    QString scanError;
    if (QFileInfo::exists(_directory) && !QFileInfo(_directory).isDir())
    {
        scanError = QStringLiteral("Plugin path is not a directory: %1").arg(_directory);
    }

    QDirIterator iterator(_directory,
                          QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    int          visited = 0;
    while (iterator.hasNext() && scanError.isEmpty())
    {
        const QString path = iterator.next();
        if (++visited > 8192 || files.size() >= 512)
        {
            scanError = QStringLiteral("Plugin catalog limit exceeded");
            break;
        }
        const QFileInfo info = iterator.fileInfo();
        if (info.isDir())
        {
            watched.append(path);
            continue;
        }
        if (!QLibrary::isLibrary(path))
        {
            continue;
        }
        if (info.size() > 256 * 1024 * 1024)
        {
            scanError = QStringLiteral("Plugin exceeds 256 MiB: %1").arg(path);
            break;
        }

        QFile              file(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        {
            scanError = QStringLiteral("Cannot read plugin %1: %2").arg(path, file.errorString());
            break;
        }
        files.insert(info.absoluteFilePath(), hash.result());
        watched.append(info.absoluteFilePath());
    }
    _updateWatches(watched);
    if (!scanError.isEmpty())
    {
        if (error)
        {
            *error = scanError;
        }
        _report(scanError);
        return false;
    }

    for (auto& [path, record] : _records)
    {
        if (record->loader && (!files.contains(path) || files.value(path) != record->fingerprint))
        {
            const QString message = QStringLiteral("Unload plugin before replacing its library: %1").arg(path);
            if (record->entry.error != message)
            {
                beginResetModel();
                record->entry.error = message;
                endResetModel();
                _report(message);
            }
            if (error)
            {
                *error = message;
            }
            return false;
        }
    }

    bool          changed = force || files.size() != static_cast<qsizetype>(_records.size());
    QSet<QString> affected;
    for (const auto& [path, record] : _records)
    {
        if (!files.contains(path) || files.value(path) != record->fingerprint)
        {
            changed = true;
            affected.insert(path);
        }
    }
    if (!changed)
    {
        _pendingScan = false;
        QSet<QString> failedPaths;
        for (const auto& path : _loadedOrder)
        {
            auto& record = *_records.at(path);
            if (record.entry.status != Status::Running || !record.instance)
            {
                continue;
            }
            try
            {
                if (record.instance->state() == Plugin::State::Running)
                {
                    continue;
                }
            }
            catch (...)
            {
            }
            failedPaths.insert(path);
        }
        if (failedPaths.isEmpty())
        {
            return true;
        }

        beginResetModel();
        _stopAffected(_dependentClosure(failedPaths));
        for (const auto& path : failedPaths)
        {
            auto& record          = *_records.at(path);
            record.failed         = true;
            record.lifecycleError = QStringLiteral("Plugin left Running state; explicit retry is required");
        }
        _reconcile(error);
        endResetModel();
        if (error)
        {
            *error = QStringLiteral("Plugin runtime failure");
        }
        return false;
    }

    beginResetModel();
    const bool stopped = _stopAffected(_dependentClosure(affected));
    for (auto it = _records.begin(); it != _records.end();)
    {
        auto& record = *it->second;
        if (affected.contains(it->first) && !record.loader)
        {
            it = _records.erase(it);
        }
        else
        {
            if (affected.contains(it->first))
            {
                record.validationError = QStringLiteral("Changed library remains pinned after shutdown failure");
            }
            ++it;
        }
    }
    QStringList paths = files.keys();
    paths.sort();
    for (const auto& path : paths)
    {
        if (_records.contains(path))
        {
            continue;
        }
        auto record         = std::make_unique<Record>();
        record->entry.path  = path;
        record->fingerprint = files.value(path);
        _readDescription(*record);
        _records.emplace(path, std::move(record));
    }
    for (auto& [path, record] : _records)
    {
        record->entry.enabled = _enabled.value(record->entry.info.id, false);
        if (force && !record->loader)
        {
            record->failed = false;
        }
    }
    const bool reconciled = _reconcile(error);
    endResetModel();
    if (_pendingScan)
    {
        _pendingScan = false;
        _scheduleScan();
    }
    return stopped && reconciled;
}

bool PluginManager::_reconcile(QString* error)
{
    QStringList             order;
    QHash<QString, Record*> byId;
    _validateGraph(order, byId);

    QSet<QString> stopPaths;
    for (const auto& [path, record] : _records)
    {
        if (!record->entry.enabled || !record->entry.error.isEmpty())
        {
            stopPaths.insert(path);
        }
    }
    bool success = _stopAffected(_dependentClosure(stopPaths));
    order.clear();
    byId.clear();
    _validateGraph(order, byId);
    for (const auto& id : order)
    {
        Record& record = *byId[id];
        if (!record.entry.enabled || !record.entry.error.isEmpty() || record.failed)
        {
            continue;
        }
        if (record.loader)
        {
            continue;
        }

        QString missing;
        for (const auto& dependency : record.entry.info.dependencies)
        {
            if (!byId[dependency]->entry.enabled || byId[dependency]->entry.status != Status::Running
                || !byId[dependency]->entry.error.isEmpty())
            {
                missing = dependency;
                break;
            }
        }
        if (!missing.isEmpty())
        {
            record.entry.status = Status::Blocked;
            record.entry.error  = QStringLiteral("Dependency is not running: %1").arg(missing);
            continue;
        }
        success = _startRecord(record) && success;
    }
    if (!success && error)
    {
        *error = QStringLiteral("One or more plugin lifecycle operations failed");
    }
    return success;
}

void PluginManager::_validateGraph(QStringList& order, QHash<QString, Record*>& byId)
{
    QHash<QString, int> counts;
    QHash<QString, int> names;
    for (const auto& [path, record] : _records)
    {
        if (record->validationError.isEmpty())
        {
            ++counts[record->entry.info.id];
        }
        ++names[QFileInfo(path).fileName().toCaseFolded()];
    }
    for (auto& [path, record] : _records)
    {
        auto& entry = record->entry;
        entry.error = record->validationError;
        if (counts.value(entry.info.id) > 1)
        {
            entry.error = QStringLiteral("Duplicate plugin ID: %1").arg(entry.info.id);
        }
        if (names.value(QFileInfo(path).fileName().toCaseFolded()) > 1)
        {
            entry.error = QStringLiteral("Duplicate library filename: %1").arg(QFileInfo(path).fileName());
        }
        if (!entry.error.isEmpty())
        {
            entry.status = Status::Invalid;
            continue;
        }
        byId[entry.info.id] = record.get();
        if (!record->loader)
        {
            entry.status = entry.enabled ? Status::Stopped : Status::Disabled;
        }
        else if (record->failed)
        {
            entry.status = Status::Failed;
        }
    }

    QHash<QString, int>         indegree;
    QHash<QString, QStringList> consumers;
    for (auto it = byId.cbegin(); it != byId.cend(); ++it)
    {
        auto& entry        = it.value()->entry;
        indegree[it.key()] = 0;
        for (const auto& dependency : entry.info.dependencies)
        {
            if (!byId.contains(dependency))
            {
                entry.error = QStringLiteral("Missing or invalid dependency: %1").arg(dependency);
                if (!it.value()->loader)
                {
                    entry.status = Status::Blocked;
                }
            }
            else
            {
                ++indegree[it.key()];
                consumers[dependency].append(it.key());
            }
        }
    }
    QStringList ready;
    for (auto it = indegree.cbegin(); it != indegree.cend(); ++it)
    {
        if (it.value() == 0)
        {
            ready.append(it.key());
        }
    }
    while (!ready.isEmpty())
    {
        ready.sort();
        const QString id = ready.takeFirst();
        order.append(id);
        for (const auto& consumer : consumers.value(id))
        {
            if (--indegree[consumer] == 0)
            {
                ready.append(consumer);
            }
        }
    }
    for (auto it = indegree.cbegin(); it != indegree.cend(); ++it)
    {
        if (it.value() > 0)
        {
            auto& entry = byId[it.key()]->entry;
            entry.error = QStringLiteral("Dependency cycle or dependency on a cycle");
            if (!byId[it.key()]->loader)
            {
                entry.status = Status::Blocked;
            }
        }
    }
    for (auto& [path, record] : _records)
    {
        if (record->failed && record->entry.error.isEmpty())
        {
            record->entry.status = Status::Failed;
            record->entry.error  = record->lifecycleError;
        }
    }
}

bool PluginManager::_startRecord(Record& record)
{
    auto& entry   = record.entry;
    entry.status  = Status::Starting;
    record.loader = std::make_unique<QPluginLoader>();
    record.loader->setLoadHints(QLibrary::ResolveAllSymbolsHint);
    record.loader->setFileName(entry.path);
    QString cause;
    try
    {
        QObject* root = record.loader->instance();
        if (!root)
        {
            cause = record.loader->errorString();
        }
        else
        {
            record.instance = qobject_cast<Plugin*>(root);
            if (!record.instance)
            {
                cause = QStringLiteral("Plugin root does not implement Plugin");
            }
            else
            {
                const auto info = record.instance->info();
                if (info.id != entry.info.id || info.name != entry.info.name || info.version != entry.info.version
                    || info.dependencies != entry.info.dependencies)
                {
                    cause = QStringLiteral("Runtime info differs from embedded metadata");
                }
                else if (record.instance->state() != Plugin::State::Stopped)
                {
                    cause = QStringLiteral("Plugin root must initially be stopped");
                }
                else if (record.instance->startup(&cause) && record.instance->state() == Plugin::State::Running)
                {
                    _loadedOrder.append(entry.path);
                    entry.status = Status::Running;
                    qInfo() << "Plugin started:" << entry.info.id;
                    emit pluginStarted(entry.info.id);
                    return true;
                }
                else if (cause.isEmpty())
                {
                    cause = QStringLiteral("Startup failed or did not enter Running state");
                }
            }
        }
    }
    catch (const std::exception& exception)
    {
        cause = QStringLiteral("Startup exception: %1").arg(QString::fromUtf8(exception.what()));
    }
    catch (...)
    {
        cause = QStringLiteral("Unknown startup exception");
    }

    // Error strings may refer to QStringLiteral storage inside the library.
    cause = QString(cause.constData(), cause.size());
    if (record.loader->isLoaded())
    {
        _loadedOrder.append(entry.path);
        _stopRecord(record);
    }
    else
    {
        record.loader.reset();
    }
    record.failed         = true;
    entry.status          = Status::Failed;
    entry.error           = QStringLiteral("%1: %2").arg(entry.info.id, cause);
    record.lifecycleError = entry.error;
    _report(entry.error);
    return false;
}

bool PluginManager::_stopRecord(Record& record)
{
    if (!record.loader)
    {
        return true;
    }
    auto& entry  = record.entry;
    entry.status = Status::Stopping;
    QString cause;
    bool    complete = record.shutdownComplete || !record.instance;
    try
    {
        if (!complete)
        {
            complete = record.instance->shutdown(&cause) && record.instance->state() == Plugin::State::Stopped;
        }
    }
    catch (const std::exception& exception)
    {
        cause = QStringLiteral("Shutdown exception: %1").arg(QString::fromUtf8(exception.what()));
    }
    catch (...)
    {
        cause = QStringLiteral("Unknown shutdown exception");
    }
    cause                   = QString(cause.constData(), cause.size());
    record.shutdownComplete = complete;
    if (complete && record.loader->unload())
    {
        record.instance = nullptr;
        record.loader.reset();
        record.shutdownComplete = false;
        record.failed           = false;
        _loadedOrder.removeAll(entry.path);
        entry.status = entry.enabled ? Status::Stopped : Status::Disabled;
        qInfo() << "Plugin stopped:" << entry.info.id;
        emit pluginStopped(entry.info.id);
        return true;
    }

    if (cause.isEmpty())
    {
        cause = complete ? record.loader->errorString() : QStringLiteral("Shutdown did not complete");
    }
    entry.status          = Status::Failed;
    entry.error           = QStringLiteral("Cannot unload %1: %2").arg(entry.info.id, cause);
    record.failed         = true;
    record.lifecycleError = entry.error;
    _report(entry.error);
    return false;
}

bool PluginManager::_stopAffected(QSet<QString> paths)
{
    bool              success = true;
    const QStringList order   = _loadedOrder;
    for (auto it = order.crbegin(); it != order.crend(); ++it)
    {
        if (!paths.contains(*it))
        {
            continue;
        }
        Record& record = *_records.at(*it);
        bool    pinned = false;
        for (const auto& otherPath : _loadedOrder)
        {
            const auto& other = *_records.at(otherPath);
            if (otherPath != *it && other.entry.info.dependencies.contains(record.entry.info.id))
            {
                pinned = true;
                break;
            }
        }
        if (pinned)
        {
            record.entry.error = QStringLiteral("Cannot unload while a dependent plugin remains loaded");
            success            = false;
            continue;
        }
        success = _stopRecord(record) && success;
    }
    return success;
}

QSet<QString> PluginManager::_dependentClosure(QSet<QString> paths) const
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        QSet<QString> ids;
        for (const auto& path : paths)
        {
            if (_records.contains(path))
            {
                ids.insert(_records.at(path)->entry.info.id);
            }
        }
        for (const auto& [path, record] : _records)
        {
            if (paths.contains(path))
            {
                continue;
            }
            for (const auto& dependency : record->entry.info.dependencies)
            {
                if (ids.contains(dependency))
                {
                    paths.insert(path);
                    changed = true;
                    break;
                }
            }
        }
    }
    return paths;
}

void PluginManager::_readDescription(Record& record)
{
    QPluginLoader reader;
    reader.setLoadHints(QLibrary::ResolveAllSymbolsHint);
    reader.setFileName(record.entry.path);
    const auto               metadata = reader.metaData();
    const auto               data     = metadata.value(QStringLiteral("MetaData")).toObject();
    const QRegularExpression idPattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$"));
    const QRegularExpression versionPattern(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+$"));
    auto&                    info = record.entry.info;
    info.id                       = data.value(QStringLiteral("id")).toString();
    info.name                     = data.value(QStringLiteral("name")).toString();
    info.version                  = data.value(QStringLiteral("version")).toString();
    if (metadata.value(QStringLiteral("IID")).toString() != QStringLiteral("org.octaviarius.Termaster.Plugin/1.0")
        || data.value(QStringLiteral("api")).toInt(-1) != 1 || !idPattern.match(info.id).hasMatch()
        || info.name.trimmed().isEmpty() || info.name.size() > 256 || !versionPattern.match(info.version).hasMatch()
        || !data.value(QStringLiteral("dependencies")).isArray())
    {
        record.validationError = QStringLiteral("Invalid plugin metadata or unsupported interface/API");
        return;
    }
    const auto dependencies = data.value(QStringLiteral("dependencies")).toArray();
    if (dependencies.size() > 128)
    {
        record.validationError = QStringLiteral("Too many plugin dependencies");
        return;
    }
    for (const auto& value : dependencies)
    {
        const QString id = value.toString();
        if (!value.isString() || !idPattern.match(id).hasMatch() || info.dependencies.contains(id))
        {
            record.validationError = QStringLiteral("Invalid or duplicate dependency ID");
            return;
        }
        info.dependencies.append(id);
    }
}

void PluginManager::_updateWatches(const QStringList& paths)
{
    QSet<QString>     desired(paths.cbegin(), paths.cend());
    const QStringList existing = _watcher.files() + _watcher.directories();
    for (const auto& path : existing)
    {
        if (!desired.remove(path))
        {
            _watcher.removePath(path);
        }
    }
    for (const auto& path : desired)
    {
        if (!_watcher.addPath(path))
        {
            qWarning() << "Cannot watch plugin path; polling remains active:" << path;
        }
    }
}

void PluginManager::_report(const QString& error)
{
    qWarning().noquote() << error;
    emit errorOccurred(error);
}
