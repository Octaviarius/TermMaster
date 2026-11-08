#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <Termaster/Core/Workspace.h>
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
    _file(file.absolute()), _baseSettings(std::move(baseSettings)), _settings(document(_file))
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

    auto candidate = document(*canonical);
    if (!candidate->load(error) || !_validate(*candidate, *canonical, error))
    {
        return false;
    }

    _file     = *canonical;
    _settings = std::move(candidate);
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

    auto candidate = document(file);
    if (!candidate->setValue("name", name, error) || !candidate->setValue("settings", QVariantMap {}, error)
        || !candidate->setValue("sessions", QVariantMap {}, error)
        || !candidate->setValue("directories", QVariantMap {}, error))
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
    return true;
}

bool Workspace::sync(QString* error)
{
    if (error)
    {
        error->clear();
    }
    return _validate(*_settings, _file, error) && _settings->sync(error);
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
    return _settings->isDirty();
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

bool Workspace::_validate(const Settings& settings, const Path& file, QString* error)
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
