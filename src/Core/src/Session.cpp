#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <Termaster/Core/Session.h>
#include <utility>

namespace
{
Q_LOGGING_CATEGORY(storageTrace, "termaster.storage.session.trace", QtInfoMsg)

bool fail(QString* error, const QString& text)
{
    if (error)
    {
        *error = text;
    }
    return false;
}
bool validId(const QString& id)
{
    static const QRegularExpression pattern("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\\z");
    return pattern.match(id).hasMatch();
}
bool validName(const QString& name, QString* error)
{
    return (!name.trimmed().isEmpty() && name.size() <= 256 && !name.contains(QChar::Null))
        || fail(error, "Session name must contain 1 to 256 characters and no NUL");
}
std::unique_ptr<Settings> document(const Path& directory)
{
    return std::make_unique<Settings>(directory.join("session.yaml"),
                                      Settings::Info {"termaster.session", "Termaster session"});
}
} // namespace

Session::Session(Path directory, SettingsOverlay baseSettings) :
    _directory(directory.absolute()), _baseSettings(std::move(baseSettings)), _settings(document(_directory))
{
}
Session::~Session() = default;

bool Session::load(QString* error)
{
    qCDebug(storageTrace) << "Session load requested";
    if (error)
    {
        error->clear();
    }
    if (!filePath().isFile())
    {
        return fail(error, "Session document is not a regular file: " + filePath().toString());
    }
    const auto directory = _directory.canonical(error);
    const auto manifest  = filePath().canonical(error);
    if (!directory || !manifest || !manifest->relativeTo(*directory))
    {
        return fail(error, "Session document escapes its directory");
    }
    auto candidate = document(*directory);
    if (!candidate->load(error) || !_validate(*candidate, error))
    {
        return false;
    }
    _directory = *directory;
    _settings  = std::move(candidate);
    return true;
}

bool Session::create(const QString& id, const QString& name, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!validId(id))
    {
        return fail(error, "Session ID must be a safe identifier of at most 128 characters");
    }
    if (!validName(name, error))
    {
        return false;
    }
    const auto parent = _directory.parent().canonical(error);
    if (!parent || !parent->isDirectory())
    {
        return fail(error, "Session parent directory does not exist");
    }
    const auto directory = parent->join(_directory.name());
    auto       candidate = document(directory);
    if (!candidate->setValue("id", id, error) || !candidate->setValue("name", name, error))
    {
        return false;
    }
    for (const auto& key : {"settings", "objects", "links", "resources", "presentation"})
    {
        if (!candidate->setValue(key, QVariantMap {}, error))
        {
            return false;
        }
    }
    if (!QDir(parent->toString()).mkdir(directory.name()))
    {
        return fail(error, "Cannot create session directory without replacing existing data: " + directory.toString());
    }
    if (!candidate->sync(error))
    {
        QDir(parent->toString()).rmdir(directory.name());
        return false;
    }
    _directory = directory;
    _settings  = std::move(candidate);
    return true;
}

std::optional<QByteArray> Session::prepareObjects(const QVariantMap& values, QString* error) const
{
    const auto bytes = _settings->serializedPatch(
        {
            {"objects", values}
    },
        {},
        error);
    if (!bytes)
    {
        return std::nullopt;
    }
    Settings candidate(filePath(), Settings::Info {"termaster.session", "Transaction candidate"});
    if (!candidate.replaceSerialized(*bytes, error) || !_validate(candidate, error))
    {
        return std::nullopt;
    }
    return bytes;
}

bool Session::acceptPreparedDocument(const QByteArray& bytes, QString* error, bool dirty)
{
    Settings candidate(filePath(), Settings::Info {"termaster.session", "Transaction candidate"});
    if (!candidate.replaceSerialized(bytes, error) || !_validate(candidate, error)
        || !_settings->replaceSerialized(bytes, error, dirty))
    {
        return false;
    }
    return true;
}

bool Session::diskUnchanged(QString* error) const
{
    return _settings->diskUnchanged(error);
}

bool Session::captureDiskBaseline(QString* error)
{
    return _settings->captureDiskBaseline(error);
}

bool Session::sync(QString* error)
{
    qCDebug(storageTrace) << "Session sync requested";
    if (error)
    {
        error->clear();
    }
    const QFileInfo manifestInfo(filePath().toString());
    if (manifestInfo.exists() || manifestInfo.isSymLink())
    {
        const auto directory = _directory.canonical(error);
        const auto manifest  = filePath().canonical(error);
        if (!directory || !manifest || !manifest->relativeTo(*directory))
        {
            return fail(error, "Session document escapes its directory");
        }
    }
    return _validate(*_settings, error) && _settings->sync(error);
}
QString Session::id() const
{
    return _settings->value("id").toString();
}
QString Session::name() const
{
    return _settings->value("name").toString();
}
bool Session::setName(const QString& name, QString* error)
{
    if (error)
    {
        error->clear();
    }
    return validName(name, error) && _settings->setValue("name", name, error);
}
bool Session::isDirty() const
{
    return _settings->isDirty();
}
Path Session::directory() const
{
    return _directory;
}
Path Session::filePath() const
{
    return _directory.join("session.yaml");
}
SettingsOverlay Session::settings()
{
    return SettingsOverlay(_baseSettings, _settings->node("settings"));
}
Settings::Node Session::objects()
{
    return _settings->node("objects");
}
Settings::Node Session::links()
{
    return _settings->node("links");
}
Settings::Node Session::resources()
{
    return _settings->node("resources");
}
Settings::Node Session::presentation()
{
    return _settings->node("presentation");
}
std::optional<Path> Session::resourcePath(const QString& id, QString* error) const
{
    if (error)
    {
        error->clear();
    }
    if (!validId(id))
    {
        fail(error, "Invalid resource ID");
        return std::nullopt;
    }
    return _resourcePath(_settings->value("resources").toMap().value(id), error);
}

bool Session::_validate(const Settings& settings, QString* error) const
{
    if (settings.value("id").typeId() != QMetaType::QString || !validId(settings.value("id").toString()))
    {
        return fail(error, "Session ID must be a safe identifier of at most 128 characters");
    }
    if (settings.value("name").typeId() != QMetaType::QString || !validName(settings.value("name").toString(), error))
    {
        return fail(error, "Invalid session name");
    }
    for (const auto& key : {"settings", "objects", "links", "resources", "presentation"})
    {
        if (settings.value(key).typeId() != QMetaType::QVariantMap)
        {
            return fail(error, QString("Session %1 must be a mapping").arg(key));
        }
    }
    const auto objects = settings.value("objects").toMap();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it)
    {
        const auto object = it.value().toMap();
        if (!validId(it.key()) || it.value().typeId() != QMetaType::QVariantMap
            || object.value("settings").typeId() != QMetaType::QVariantMap)
        {
            return fail(error, "Invalid session object: " + it.key());
        }
        for (const auto& key : {"type", "module"})
        {
            if (object.value(key).typeId() != QMetaType::QString || object.value(key).toString().trimmed().isEmpty()
                || object.value(key).toString().contains(QChar::Null))
            {
                return fail(error, "Invalid object type or module: " + it.key());
            }
        }
    }
    const auto links = settings.value("links").toMap();
    for (auto it = links.cbegin(); it != links.cend(); ++it)
    {
        if (!validId(it.key()) || it.value().typeId() != QMetaType::QVariantMap)
        {
            return fail(error, "Invalid session link: " + it.key());
        }
        for (const auto& key : {"source", "target"})
        {
            const auto value    = it.value().toMap().value(key);
            const auto endpoint = value.toMap();
            if (value.typeId() != QMetaType::QVariantMap || endpoint.value("object").typeId() != QMetaType::QString
                || !objects.contains(endpoint.value("object").toString())
                || endpoint.value("port").typeId() != QMetaType::QString
                || endpoint.value("port").toString().trimmed().isEmpty()
                || endpoint.value("port").toString().contains(QChar::Null))
            {
                return fail(error, "Invalid link endpoint: " + it.key());
            }
        }
    }
    const auto resources = settings.value("resources").toMap();
    for (auto it = resources.cbegin(); it != resources.cend(); ++it)
    {
        if (!validId(it.key()) || !_resourcePath(it.value(), error))
        {
            return fail(error, "Invalid resource " + it.key() + (error ? ": " + *error : QString()));
        }
    }
    return true;
}

std::optional<Path> Session::_resourcePath(const QVariant& value, QString* error) const
{
    const auto text = value.toString();
    if (value.typeId() != QMetaType::QString || text.isEmpty() || text.contains(QChar::Null))
    {
        fail(error, "Resource path must be a nonempty relative string without NUL");
        return std::nullopt;
    }
    const auto windows = PurePath::windows(text);
    const auto posix   = PurePath::posix(text);
    if (!windows.anchor().isEmpty() || posix.isAbsolute())
    {
        fail(error, "Resource path must be relative");
        return std::nullopt;
    }
    const auto normalized = QDir::fromNativeSeparators(text).replace('\\', '/');
    for (const auto& part : normalized.split('/'))
    {
        if (part == ".." || part.isEmpty() || part == ".")
        {
            fail(error, "Resource path contains an invalid component");
            return std::nullopt;
        }
    }
    const auto base = _directory.canonical(error);
    if (!base)
    {
        return std::nullopt;
    }
    Path current = *base;
    for (const auto& part : normalized.split('/'))
    {
        current = current.join(part);
        const QFileInfo info(current.toString());
        if (info.exists() || info.isSymLink())
        {
            const auto canonical = current.canonical(error);
            if (!canonical || (!canonical->relativeTo(*base) && !(*canonical == *base)))
            {
                fail(error, "Resource path escapes the session directory");
                return std::nullopt;
            }
        }
    }
    return current;
}
