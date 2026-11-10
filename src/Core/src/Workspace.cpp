#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/Workspace.h>
#include <Termaster/Core/WorkspaceFileTransaction.h>
#include <utility>

namespace
{
bool fail(QString* error, const QString& text)
{
    if (error)
    {
        *error = text;
    }
    return false;
}

bool validName(const QString& name, QString* error)
{
    if (name.trimmed().isEmpty() || name.size() > 256 || name.contains(QChar::Null))
    {
        return fail(error, "Workspace name must contain 1 to 256 characters and no NUL");
    }
    return true;
}

bool validSessionId(const QString& id, QString* error)
{
    static const QRegularExpression pattern("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\\z");
    if (!pattern.match(id).hasMatch())
    {
        return fail(
            error,
            "Session ID must contain 1 to 128 ASCII letters, digits, dots, underscores or hyphens and begin with a letter or digit");
    }
    return true;
}

std::optional<QString> resolvedMissingPath(QString path, QString* error)
{
    QStringList suffix;
    while (!QFileInfo::exists(path))
    {
        const QFileInfo info(path);
        if (info.isSymLink() || info.dir().absolutePath() == path)
        {
            fail(error, "Cannot resolve session path: " + path);
            return std::nullopt;
        }
        suffix.prepend(info.fileName());
        path = info.dir().absolutePath();
    }
    const auto canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty())
    {
        fail(error, "Cannot resolve session path: " + path);
        return std::nullopt;
    }
    return QDir::cleanPath(canonical + "/" + suffix.join('/'));
}

std::optional<Path> catalogDirectory(const Path& file, const QVariant& value, QString* error)
{
    if (value.typeId() != QMetaType::QString)
    {
        fail(error, "Session directory must be a relative path string");
        return std::nullopt;
    }
    auto relative = value.toString();
    relative.replace('\\', '/');
    const auto parts = relative.split('/');
    if (relative.isEmpty() || relative.contains(QChar::Null) || QDir::isAbsolutePath(relative) || relative.contains(':')
        || parts.contains("..") || parts.first() != "sessions" || QDir::cleanPath(relative) == "sessions")
    {
        fail(error, "Session directory must be a relative child of the workspace sessions directory");
        return std::nullopt;
    }
    const auto root          = file.parent().join("sessions");
    const auto path          = file.parent().join(relative).absolute();
    const auto canonicalRoot = resolvedMissingPath(root.toString(), error);
    const auto canonicalPath = resolvedMissingPath(path.toString(), error);
    if (!canonicalRoot || !canonicalPath)
    {
        return std::nullopt;
    }
    const auto confined = QDir(*canonicalRoot).relativeFilePath(*canonicalPath);
    if (confined == "." || confined == ".." || confined.startsWith("../") || QDir::isAbsolutePath(confined))
    {
        fail(error, "Session directory escapes the workspace sessions directory");
        return std::nullopt;
    }
    return path;
}

QString processorFile(const Path& workspace, const QString& id)
{
    return QDir::fromNativeSeparators(workspace.toString()) + ".d/processors/"
         + QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".yaml";
}
std::unique_ptr<Settings> processorCache()
{
    return std::make_unique<Settings>(QString {},
                                      Settings::Info {"termaster.processor-cache", "In-memory processor definitions"});
}
std::unique_ptr<Settings> document(const Path& file)
{
    return std::make_unique<Settings>(file, Settings::Info {"termaster.workspace", "Termaster workspace"});
}
} // namespace

Workspace::Workspace(Path file, Settings::Node baseSettings) :
    Workspace(std::move(file), SettingsOverlay(baseSettings, baseSettings))
{
}

Workspace::Workspace(Path file, SettingsOverlay baseSettings) :
    QObject(nullptr), _file(file.absolute()), _baseSettings(std::move(baseSettings)), _settings(document(_file)),
    _processors(processorCache())
{
}

Workspace::~Workspace() = default;

bool Workspace::load(QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!_file.isFile())
    {
        return fail(error, "Workspace file does not exist or is not a regular file: " + _file.toString());
    }
    const auto canonical = _file.canonical(error);
    if (!canonical)
    {
        return false;
    }

    if (!WorkspaceFileTransaction::recover(*canonical, error))
    {
        return false;
    }
    auto candidate = document(*canonical);
    if (!candidate->load(error) || !_validate(*candidate, *canonical, error))
    {
        return false;
    }

    QVariantMap    values;
    ProcessorFiles files;
    if (!_readProcessorFiles(*canonical, values, files, error) || !_validateProcessors(values, error))
    {
        return false;
    }
    auto       processors = processorCache();
    const auto bytes      = processors->serializedPatch(
        {
            {"definitions", values}
    },
        {},
        error);
    if (!bytes || !processors->replaceSerialized(*bytes, error))
    {
        return false;
    }
    _file          = *canonical;
    _settings      = std::move(candidate);
    _processors    = std::move(processors);
    _processorDisk = std::move(files);
    _publishProcessors();
    return true;
}

bool Workspace::create(const QString& name, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!validName(name, error))
    {
        return false;
    }
    if (!_file.parent().isDirectory())
    {
        return fail(error, "Workspace parent directory does not exist: " + _file.parent().toString());
    }

    const auto parent = _file.parent().canonical(error);
    if (!parent)
    {
        return false;
    }
    const auto file = parent->join(_file.name());

    QVariantMap    processorValues;
    ProcessorFiles processorFiles;
    if (!_readProcessorFiles(file, processorValues, processorFiles, error))
    {
        return false;
    }
    if (!processorFiles.isEmpty())
    {
        return fail(error, "Workspace processor directory already contains definitions");
    }
    auto candidate = document(file);
    if (!candidate->setValue("name", name, error) || !candidate->setValue("settings", QVariantMap {}, error)
        || !candidate->setValue("sessions", QVariantMap {}, error)
        || !candidate->setValue("directories", QVariantMap {}, error)
        || !candidate->setValue("state/processing/favorites", QStringList {"native.bypass"}, error))
    {
        return false;
    }

    if (!_validate(*candidate, file, error))
    {
        return false;
    }

    QFile reservation(file.toString());
    if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        return fail(error, "Cannot create workspace " + file.toString() + ": " + reservation.errorString());
    }
    reservation.close();
    if (!candidate->sync(error))
    {
        reservation.remove();
        return false;
    }

    _file     = file;
    _settings = std::move(candidate);
    _publishProcessors();
    return true;
}

std::optional<QByteArray> Workspace::prepareProcessorDefinitions(const QVariantMap& values, QString* error) const
{
    if (!_validateProcessors(values, error))
    {
        return std::nullopt;
    }
    const auto bytes = _settings->serializedPatch({}, {}, error);
    if (!bytes)
    {
        return std::nullopt;
    }
    Settings candidate(_file, Settings::Info {"termaster.workspace", "Transaction candidate"});
    if (!candidate.replaceSerialized(*bytes, error) || !_validate(candidate, _file, error))
    {
        return std::nullopt;
    }
    return bytes;
}

std::optional<Workspace::ProcessorFiles> Workspace::prepareProcessorFiles(const QVariantMap& values,
                                                                          QString*           error) const
{
    if (!_validateProcessors(values, error))
    {
        return std::nullopt;
    }
    ProcessorFiles files;
    qint64         total = 0;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
    {
        const auto path = processorFile(_file, it.key());
        Settings   document(path, Settings::Info {"termaster.processor-definition", "Workspace processor definition"});
        if (_processorDisk.contains(path) && !document.replaceSerialized(_processorDisk.value(path), error))
        {
            return std::nullopt;
        }
        const auto definition = it.value().toMap();
        const auto bytes      = document.values() == definition && _processorDisk.contains(path) ?
                                    std::optional<QByteArray>(_processorDisk.value(path)) :
                                    document.serializedContent(definition, error);
        if (!bytes)
        {
            return std::nullopt;
        }
        total += bytes->size();
        if (total > 4 * 1024 * 1024)
        {
            fail(error, "Processor documents exceed the 4 MiB collection limit");
            return std::nullopt;
        }
        files.insert(path, *bytes);
    }
    return files;
}
Workspace::ProcessorFiles Workspace::processorDiskFiles() const
{
    return _processorDisk;
}
Path Workspace::processorsDirectory() const
{
    return Path(_file.toString() + ".d/processors");
}
bool Workspace::acceptProcessorDefinitions(const QVariantMap&    values,
                                           const ProcessorFiles& files,
                                           QString*              error,
                                           bool                  dirty)
{
    if (!_validateProcessors(values, error))
    {
        return false;
    }
    const auto bytes = _processors->serializedPatch(
        {
            {"definitions", values}
    },
        {},
        error);
    if (!bytes || !_processors->replaceSerialized(*bytes, error, dirty))
    {
        return false;
    }
    _processorDisk = files;
    _publishProcessors();
    return true;
}

bool Workspace::acceptPreparedDocument(const QByteArray& bytes, QString* error, bool dirty)
{
    Settings candidate(_file, Settings::Info {"termaster.workspace", "Transaction candidate"});
    if (!candidate.replaceSerialized(bytes, error) || !_validate(candidate, _file, error)
        || !_settings->replaceSerialized(bytes, error, dirty))
    {
        return false;
    }
    _publishProcessors();
    return true;
}

bool Workspace::diskUnchanged(QString* error) const
{
    if (!_settings->diskUnchanged(error))
    {
        return false;
    }
    QVariantMap    values;
    ProcessorFiles files;
    if (!_readProcessorFiles(_file, values, files, error, false))
    {
        return false;
    }
    return files == _processorDisk
        || fail(error, "Processor files were externally changed since their last load or write");
}

bool Workspace::captureDiskBaseline(QString* error)
{
    QVariantMap    values;
    ProcessorFiles files;
    if (!_readProcessorFiles(_file, values, files, error) || !_settings->captureDiskBaseline(error))
    {
        return false;
    }
    _processorDisk = std::move(files);
    return true;
}

bool Workspace::sync(QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!_validate(*_settings, _file, error) || !diskUnchanged(error))
    {
        return false;
    }
    const auto values = processorDefinitions().values();
    if (!_validateProcessors(values, error))
    {
        return false;
    }
    const auto journal = WorkspaceFileTransaction::journalPath(_file);
    if (QFileInfo::exists(journal) || QFileInfo(journal).isSymLink())
    {
        return fail(error, "An interrupted processing transaction must be recovered before editing");
    }
    if (!isDirty())
    {
        return true;
    }

    const bool processorsChanged = _processors->isDirty();
    const auto main              = prepareProcessorDefinitions(values, error);
    const auto files             = processorsChanged ? prepareProcessorFiles(values, error) :
                                                       std::optional<ProcessorFiles>(_processorDisk);
    if (!main || !files)
    {
        return false;
    }
    QFile original(_file.toString());
    if (!original.open(QIODevice::ReadOnly))
    {
        return fail(error, "Cannot read workspace before save");
    }
    const auto originalBytes = original.read(4 * 1024 * 1024 + 1);
    if (original.error() != QFileDevice::NoError || originalBytes.size() > 4 * 1024 * 1024)
    {
        return fail(error, "Cannot read bounded workspace before save");
    }
    original.close();
    QList<WorkspaceFileTransaction::Change> changes {
        {QFileInfo(_file.toString()).canonicalFilePath(), originalBytes, *main}
    };
    QSet<QString> paths;
    for (const auto& path : _processorDisk.keys())
    {
        paths.insert(path);
    }
    for (const auto& path : files->keys())
    {
        paths.insert(path);
    }
    for (const auto& path : paths)
    {
        const auto before = _processorDisk.contains(path) ? std::optional<QByteArray>(_processorDisk.value(path)) :
                                                            std::nullopt;
        const auto after  = files->contains(path) ? std::optional<QByteArray>(files->value(path)) : std::nullopt;
        changes.append({path, before, after});
    }
    if (!WorkspaceFileTransaction::commit(_file, changes, error))
    {
        return false;
    }
    const auto snapshot          = _settings->serializedPatch({}, {}, error);
    const auto processorSnapshot = processorDefinitions().values();
    const auto diskSnapshot      = _processorDisk;
    const bool mainDirty         = _settings->isDirty();
    const bool processorDirty    = _processors->isDirty();
    QString    failure;
    if (!acceptPreparedDocument(*main, &failure)
        || (processorsChanged && !acceptProcessorDefinitions(values, *files, &failure))
        || !WorkspaceFileTransaction::finish(_file, &failure))
    {
        QString    rollback;
        const bool restored = WorkspaceFileTransaction::recover(_file, &rollback);
        if (!restored)
        {
            failure += "; rollback failed: " + rollback;
        }
        if (snapshot)
        {
            _settings->replaceSerialized(*snapshot, &rollback, mainDirty);
        }
        if (processorsChanged)
        {
            acceptProcessorDefinitions(processorSnapshot, diskSnapshot, &rollback, processorDirty);
        }
        if (restored)
        {
            captureDiskBaseline(&rollback);
        }
        if (error)
        {
            *error = failure;
        }
        return false;
    }
    return true;
}

Path Workspace::filePath() const
{
    return _file;
}

QString Workspace::name() const
{
    return _settings->value("name").toString();
}

bool Workspace::setName(const QString& name, QString* error)
{
    if (error)
    {
        error->clear();
    }
    return validName(name, error) && _settings->setValue("name", name, error);
}

bool Workspace::isDirty() const
{
    return _settings->isDirty() || _processors->isDirty();
}

SettingsOverlay Workspace::settings()
{
    return SettingsOverlay(_baseSettings, _settings->node("settings"));
}

Settings::Node Workspace::sessions()
{
    return _settings->node("sessions");
}

Settings::Node Workspace::state()
{
    return _settings->node("state");
}

Settings::Node Workspace::processorDefinitions()
{
    return _processors->node("definitions");
}

bool Workspace::setProcessorDefinition(const QString& id, const QVariantMap& definition, QString* error)
{
    auto definitions = _processors->value("definitions").toMap();
    definitions.insert(id, definition);
    if (!ProcessorRegistry::parseDefinitions(definitions, error)
        || (_processorValidator && !_processorValidator(definitions, error))
        || !_processors->setValue("definitions", definitions, error))
    {
        return false;
    }
    _publishProcessors();
    return true;
}

bool Workspace::removeProcessorDefinition(const QString& id, QString* error)
{
    if (error)
    {
        error->clear();
    }
    auto definitions = _processors->value("definitions").toMap();
    if (!definitions.remove(id))
    {
        return fail(error, "Processor definition is not registered: " + id);
    }
    if (!ProcessorRegistry::parseDefinitions(definitions, error)
        || (_processorValidator && !_processorValidator(definitions, error))
        || !_processors->setValue("definitions", definitions, error))
    {
        return false;
    }
    _publishProcessors();
    return true;
}

void Workspace::setProcessorValidator(std::function<bool(const QVariantMap&, QString*)> validator)
{
    _processorValidator = std::move(validator);
}

void Workspace::_publishProcessors()
{
    const auto definitions = _processors->value("definitions").toMap();
    if (_processorsPublished && definitions == _publishedProcessors)
    {
        return;
    }
    _publishedProcessors = definitions;
    _processorsPublished = true;
    emit processorsChanged();
}

Path Workspace::sessionsDirectory() const
{
    return _file.parent().join("sessions");
}

QStringList Workspace::sessionIds() const
{
    QStringList result;
    const auto  entries = _settings->value("sessions").toMap();
    for (auto entry = entries.cbegin(); entry != entries.cend(); ++entry)
    {
        if (validSessionId(entry.key(), nullptr) && entry.value().typeId() == QMetaType::QVariantMap
            && catalogDirectory(_file, entry.value().toMap().value("directory"), nullptr))
        {
            result.append(entry.key());
        }
    }
    return result;
}

std::optional<Path> Workspace::sessionDirectory(const QString& id, QString* error) const
{
    if (error)
    {
        error->clear();
    }
    if (!validSessionId(id, error))
    {
        return std::nullopt;
    }
    const auto entries = _settings->value("sessions").toMap();
    if (!entries.contains(id) || entries.value(id).typeId() != QMetaType::QVariantMap)
    {
        fail(error, "Session ID is not registered in this workspace");
        return std::nullopt;
    }
    return catalogDirectory(_file, entries.value(id).toMap().value("directory"), error);
}

bool Workspace::registerSession(const QString& id, const Path& directory, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!validSessionId(id, error))
    {
        return false;
    }
    if (!directory.isDirectory())
    {
        return fail(error, "Session directory does not exist: " + directory.toString());
    }
    const auto relative = QDir(_file.parent().toString()).relativeFilePath(directory.absolute().toString());
    if (!catalogDirectory(_file, relative, error))
    {
        return false;
    }
    auto entry = _settings->value("sessions").toMap().value(id).toMap();
    entry.insert("directory", relative);
    return _settings->setValue("sessions/" + id, entry, error);
}

bool Workspace::unregisterSession(const QString& id, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!validSessionId(id, error))
    {
        return false;
    }
    if (!_settings->value("sessions").toMap().contains(id))
    {
        return fail(error, "Session ID is not registered in this workspace");
    }
    auto recent = _settings->value("state/recentSessions").toList();
    recent.removeAll(QVariant(id));
    if (!_settings->remove("sessions/" + id, error))
    {
        return false;
    }
    return _settings->setValue("state/recentSessions", recent, error);
}

QStringList Workspace::recentSessions() const
{
    const auto  sessions = sessionIds();
    const auto  recent   = _settings->value("state/recentSessions").toList();
    QStringList result;
    for (const auto& entry : recent)
    {
        const auto id = entry.toString();
        if (sessions.contains(id) && !result.contains(id) && result.size() < 10)
        {
            result.append(id);
        }
    }
    return result;
}

bool Workspace::rememberSession(const QString& id, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!validSessionId(id, error))
    {
        return false;
    }
    if (!sessionDirectory(id, error))
    {
        return fail(error, "Session ID does not identify a saved session mapping");
    }
    if (!_validate(*_settings, _file, error))
    {
        return false;
    }

    auto recent = recentSessions();
    recent.removeAll(id);
    recent.prepend(id);
    while (recent.size() > 10)
    {
        recent.removeLast();
    }
    return _settings->setValue("state/recentSessions", recent, error);
}

std::optional<Path> Workspace::pluginDirectory() const
{
    return _directory("plugins");
}

std::optional<Path> Workspace::scriptDirectory() const
{
    return _directory("scripts");
}

bool Workspace::_validate(const Settings& settings, const Path& file, QString* error) const
{
    const auto name = settings.value("name");
    if (name.typeId() != QMetaType::QString)
    {
        return fail(error, "Workspace name must be a string");
    }
    if (!validName(name.toString(), error))
    {
        return false;
    }
    for (const auto& key : {"settings", "sessions"})
    {
        if (settings.value(key).typeId() != QMetaType::QVariantMap)
        {
            return fail(error, QString("Workspace %1 must be a mapping").arg(key));
        }
    }
    if (settings.contains("processorDefinitions"))
    {
        return fail(error,
                    "Inline processorDefinitions are unsupported; definitions belong in workspace processor files");
    }
    const auto entries = settings.value("sessions").toMap();
    for (auto entry = entries.cbegin(); entry != entries.cend(); ++entry)
    {
        if (!validSessionId(entry.key(), error))
        {
            return false;
        }
        if (entry.value().typeId() != QMetaType::QVariantMap)
        {
            return fail(error, "Workspace session entry must be a mapping");
        }
        if (!catalogDirectory(file, entry.value().toMap().value("directory"), error))
        {
            return false;
        }
    }
    if (settings.contains("state"))
    {
        const auto state = settings.value("state");
        if (state.typeId() != QMetaType::QVariantMap)
        {
            return fail(error, "Workspace state must be a mapping");
        }
        const auto map = state.toMap();
        if (map.contains("processing"))
        {
            const auto processing = map.value("processing");
            if (processing.typeId() != QMetaType::QVariantMap)
            {
                return fail(error, "Workspace processing preferences must be a mapping");
            }
            const auto preferences = processing.toMap();
            if (preferences.contains("favorites"))
            {
                const auto favorites = preferences.value("favorites");
                if ((favorites.typeId() != QMetaType::QVariantList && favorites.typeId() != QMetaType::QStringList)
                    || favorites.toList().size() > 128)
                {
                    return fail(error, "Workspace processor favorites must be a list of at most 128 IDs");
                }
                QSet<QString> seen;
                for (const auto& value : favorites.toList())
                {
                    const auto id = value.toString();
                    if (value.typeId() != QMetaType::QString || id.isEmpty() || id.size() > 256 || id != id.trimmed()
                        || id.contains(QChar::Null) || seen.contains(id))
                    {
                        return fail(
                            error,
                            "Workspace processor favorite IDs must be unique trimmed strings of 1 to 256 characters without NUL");
                    }
                    seen.insert(id);
                }
            }
        }
        if (map.contains("recentSessions"))
        {
            const auto recent = map.value("recentSessions");
            if (recent.typeId() != QMetaType::QVariantList || recent.toList().size() > 10)
            {
                return fail(error, "Workspace recent sessions must be a list of at most 10 session IDs");
            }
            QStringList seen;
            for (const auto& entry : recent.toList())
            {
                if (entry.typeId() != QMetaType::QString || !validSessionId(entry.toString(), error))
                {
                    return fail(error, "Workspace recent sessions must contain valid string session IDs");
                }
                const auto id = entry.toString();
                if (seen.contains(id))
                {
                    return fail(error, "Workspace recent sessions must contain unique session IDs");
                }
                seen.append(id);
            }
        }
    }
    if (!settings.contains("directories"))
    {
        return true;
    }
    const auto directories = settings.value("directories");
    if (directories.typeId() != QMetaType::QVariantMap)
    {
        return fail(error, "Workspace directories must be a mapping");
    }
    const auto map = directories.toMap();
    for (const auto& key : {"plugins", "scripts"})
    {
        if (!map.contains(key))
        {
            continue;
        }
        const auto value = map.value(key);
        if (value.typeId() != QMetaType::QString || value.toString().trimmed().isEmpty()
            || value.toString().contains(QChar::Null))
        {
            return fail(error, QString("Workspace directories/%1 must be a nonempty path string").arg(key));
        }
    }
    return true;
}

bool Workspace::_validateProcessors(const QVariantMap& values, QString* error) const
{
    return ProcessorRegistry::parseDefinitions(values, error).has_value()
        && (!_processorValidator || _processorValidator(values, error));
}
bool Workspace::_readProcessorFiles(const Path&     file,
                                    QVariantMap&    values,
                                    ProcessorFiles& files,
                                    QString*        error,
                                    bool            parse) const
{
    const auto    workspacePath = QDir::fromNativeSeparators(file.toString());
    const QString root          = workspacePath + ".d/processors";
    for (const auto& path : QStringList {workspacePath + ".d", root})
    {
        const QFileInfo info(path);
        if (info.isSymLink()
            || (info.exists() && (!info.isDir() || info.canonicalFilePath() != info.absoluteFilePath())))
        {
            return fail(error, "Processor directory is not a confined regular directory: " + path);
        }
    }
    if (!QFileInfo::exists(root))
    {
        return true;
    }
    QDirIterator iterator(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    int          inspected = 0;
    qint64       total     = 0;
    while (iterator.hasNext())
    {
        if (++inspected > 4096)
        {
            return fail(error, "Processor directory contains too many entries");
        }
        iterator.next();
        const auto entry = iterator.fileInfo();
        if (entry.suffix().compare("yaml", Qt::CaseInsensitive) != 0)
        {
            continue;
        }
        if (files.size() >= 1024 || entry.isSymLink() || !entry.isFile()
            || entry.canonicalFilePath() != entry.absoluteFilePath())
        {
            return fail(error, "Invalid or excessive processor document: " + entry.absoluteFilePath());
        }
        if (entry.size() < 0 || entry.size() > 4 * 1024 * 1024 - total)
        {
            return fail(error, "Processor documents exceed the 4 MiB collection limit");
        }
        QFile input(entry.absoluteFilePath());
        if (!input.open(QIODevice::ReadOnly))
        {
            return fail(error, "Cannot read processor document: " + input.errorString());
        }
        const auto bytes = input.read(4 * 1024 * 1024 - total + 1);
        if (input.error() != QFileDevice::NoError || bytes.size() > 4 * 1024 * 1024 - total)
        {
            return fail(error, "Cannot read bounded processor document");
        }
        total += bytes.size();
        if (!parse)
        {
            files.insert(entry.absoluteFilePath(), bytes);
            continue;
        }
        Settings document(entry.absoluteFilePath(),
                          Settings::Info {"termaster.processor-definition", "Workspace processor definition"});
        if (!document.replaceSerialized(bytes, error))
        {
            return false;
        }
        const auto definition = document.values();
        const auto id         = definition.value("id").toString();
        if (values.contains(id) || processorFile(file, id) != entry.absoluteFilePath())
        {
            return fail(error, "Processor filename does not match its unique stable ID: " + entry.fileName());
        }
        values.insert(id, definition);
        files.insert(entry.absoluteFilePath(), bytes);
    }
    return !parse || ProcessorRegistry::parseDefinitions(values, error).has_value();
}

std::optional<Path> Workspace::_directory(const QString& key) const
{
    const auto value = _settings->value("directories/" + key);
    if (value.typeId() != QMetaType::QString || value.toString().trimmed().isEmpty()
        || value.toString().contains(QChar::Null))
    {
        return std::nullopt;
    }
    const Path path(value.toString());
    return path.isAbsolute() ? path.absolute() : _file.parent().join(path).absolute();
}
