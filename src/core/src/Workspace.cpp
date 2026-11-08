#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    if (id.trimmed().isEmpty() || id.size() > 256 || id.contains(QChar::Null))
    {
        return fail(error, "Session ID must contain 1 to 256 characters and no NUL");
    }
    return true;
}

std::unique_ptr<Settings> document(const Path& file)
{
    return std::make_unique<Settings>(file, Settings::Info {"termaster.workspace", "Termaster workspace"});
}
} // namespace

Workspace::Workspace(Path file, Settings::Node baseSettings) :
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
    if (!candidate->load(error) || !_validate(*candidate, error))
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
    return _validate(*_settings, error) && _settings->sync(error);
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

QStringList Workspace::recentSessions() const
{
    const auto  sessions = _settings->value("sessions").toMap();
    const auto  recent   = _settings->value("state/recentSessions").toList();
    QStringList result;
    for (const auto& entry : recent)
    {
        const auto id = entry.toString();
        if (sessions.value(id).typeId() == QMetaType::QVariantMap)
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
    if (_settings->value("sessions").toMap().value(id).typeId() != QMetaType::QVariantMap)
    {
        return fail(error, "Session ID does not identify a saved session mapping");
    }
    if (!_validate(*_settings, error))
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

bool Workspace::_validate(const Settings& settings, QString* error)
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
