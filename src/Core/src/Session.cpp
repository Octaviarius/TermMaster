#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>
#include <Termaster/Core/Session.h>
#include <limits>
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
bool validLayoutId(const QVariant& value)
{
    static const QRegularExpression pattern("\\A[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\z");
    return value.typeId() == QMetaType::QString && pattern.match(value.toString()).hasMatch()
        && value.toString() != "00000000-0000-0000-0000-000000000000";
}
bool validSplitterSize(const QVariant& value)
{
    switch (value.typeId())
    {
        case QMetaType::Int:
        case QMetaType::LongLong:
            return value.toLongLong() >= 0 && value.toLongLong() <= std::numeric_limits<int>::max();
        case QMetaType::UInt:
        case QMetaType::ULongLong:
            return value.toULongLong() <= static_cast<qulonglong>(std::numeric_limits<int>::max());
        default:
            return false;
    }
}
bool validTerminalLayout(const QVariant& value, QString* error)
{
    if (value.typeId() != QMetaType::QVariantMap)
    {
        return fail(error, "Session presentation terminalLayout must be a mapping");
    }
    const auto layout       = value.toMap();
    const auto windowsValue = layout.value("windows");
    if (windowsValue.typeId() != QMetaType::QVariantList || windowsValue.toList().size() > 64)
    {
        return fail(error, "Terminal layout windows must be a list of at most 64 windows");
    }
    QSet<QString> identities;
    QSet<int>     windowNumbers;
    QSet<QString> groups;
    QSet<QString> terminals;
    const auto    tree = [&](const auto& self, const QVariant& nodeValue, int depth) -> bool {
        if (depth > 16 || nodeValue.typeId() != QMetaType::QVariantMap)
        {
            return fail(error, "Terminal layout tree must contain mappings with depth at most 16");
        }
        const auto node = nodeValue.toMap();
        const auto type = node.value("type");
        if (type.typeId() != QMetaType::QString)
        {
            return fail(error, "Terminal layout tree type must be group or split");
        }
        if (type.toString() == "group")
        {
            const auto id = node.value("id");
            if (!validLayoutId(id) || identities.contains(id.toString()) || groups.size() >= 256)
            {
                return fail(error, "Terminal layout requires unique UUID identities and at most 256 groups");
            }
            identities.insert(id.toString());
            groups.insert(id.toString());

            for (const auto& field : {"toolbarVisible", "statusBarVisible"})
            {
                if (node.contains(field) && node.value(field).typeId() != QMetaType::Bool)
                {
                    return fail(error, QString("Terminal group %1 must be boolean").arg(field));
                }
            }

            const auto tabsValue = node.value("tabs");
            if (tabsValue.typeId() != QMetaType::QVariantList)
            {
                return fail(error, "Terminal group tabs must be a list of object IDs");
            }
            const auto tabs = tabsValue.toList();
            if (tabs.size() > 100000 - terminals.size())
            {
                return fail(error, "Terminal layout exceeds 100000 terminal IDs");
            }
            QSet<QString> members;
            for (const auto& tab : tabs)
            {
                const auto terminal = tab.toString();
                if (tab.typeId() != QMetaType::QString || !validId(terminal) || terminals.contains(terminal))
                {
                    return fail(error, "Terminal layout requires unique safe terminal object IDs");
                }
                terminals.insert(terminal);
                members.insert(terminal);
            }
            if (node.contains("active"))
            {
                const auto active = node.value("active");
                if (active.typeId() != QMetaType::QString
                    || (!active.toString().isEmpty() && !members.contains(active.toString())))
                {
                    return fail(error, "Terminal group active tab must be empty or a member object ID");
                }
            }
            return true;
        }
        const auto orientation   = node.value("orientation");
        const auto childrenValue = node.value("children");
        const auto sizesValue    = node.value("sizes");
        if (type.toString() != "split" || orientation.typeId() != QMetaType::QString
            || (orientation.toString() != "horizontal" && orientation.toString() != "vertical")
            || childrenValue.typeId() != QMetaType::QVariantList || sizesValue.typeId() != QMetaType::QVariantList)
        {
            return fail(error, "Terminal split requires an orientation and lists of children and sizes");
        }
        const auto children = childrenValue.toList();
        const auto sizes    = sizesValue.toList();
        if (children.size() < 2 || children.size() > 256 || sizes.size() != children.size())
        {
            return fail(error, "Terminal split requires 2 to 256 children and one size per child");
        }
        for (const auto& size : sizes)
        {
            if (!validSplitterSize(size))
            {
                return fail(error, "Terminal split sizes must be nonnegative integers within the Qt int range");
            }
        }
        for (const auto& child : children)
        {
            if (!self(self, child, depth + 1))
            {
                return false;
            }
        }
        return true;
    };
    if (!tree(tree, layout.value("main"), 1))
    {
        return false;
    }
    for (const auto& windowValue : windowsValue.toList())
    {
        if (windowValue.typeId() != QMetaType::QVariantMap)
        {
            return fail(error, "Terminal layout window must be a mapping");
        }
        const auto window = windowValue.toMap();
        const auto id     = window.value("id");
        if (!validLayoutId(id) || identities.contains(id.toString()))
        {
            return fail(error, "Terminal layout windows require unique UUID identities");
        }
        identities.insert(id.toString());
        if (window.contains("number"))
        {
            const auto number = window.value("number");
            if (!validSplitterSize(number) || number.toInt() < 2 || windowNumbers.contains(number.toInt()))
            {
                return fail(error, "Terminal window numbers must be unique integers from 2 to 2147483647");
            }
            windowNumbers.insert(number.toInt());
        }

        constexpr qsizetype maxDecodedBytes = 16384;
        constexpr qsizetype maxEncodedBytes = ((maxDecodedBytes + 2) / 3) * 4;
        const auto          geometry        = window.value("geometry");
        const auto          encoded         = geometry.toString();
        if (geometry.typeId() != QMetaType::QString || encoded.isEmpty() || encoded.size() > maxEncodedBytes)
        {
            return fail(error, "Terminal window geometry must be nonempty bounded Base64");
        }
        const auto decoded = QByteArray::fromBase64Encoding(encoded.toLatin1(),
                                                            QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.size() > maxDecodedBytes
            || QString::fromLatin1(decoded.decoded.toBase64()) != encoded)
        {
            return fail(error, "Terminal window geometry must be canonical Base64 of at most 16 KiB");
        }
        if (!tree(tree, window.value("tree"), 1))
        {
            return false;
        }
    }
    if (layout.contains("activeGroup"))
    {
        const auto active = layout.value("activeGroup");
        if (!validLayoutId(active) || !groups.contains(active.toString()))
        {
            return fail(error, "Terminal layout activeGroup must identify a layout group");
        }
    }
    return true;
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
    const auto presentation = settings.value("presentation").toMap();
    if (presentation.contains("tabOrder"))
    {
        const auto value = presentation.value("tabOrder");
        if (value.typeId() != QMetaType::QVariantList && value.typeId() != QMetaType::QStringList)
        {
            return fail(error, "Session presentation tabOrder must be a list of object IDs");
        }
        const auto order = value.toList();
        if (order.size() > 100000)
        {
            return fail(error, "Session presentation tabOrder exceeds 100000 entries");
        }
        QSet<QString> seen;
        for (const auto& entry : order)
        {
            const auto id = entry.toString();
            if (entry.typeId() != QMetaType::QString || !validId(id) || seen.contains(id))
            {
                return fail(error, "Session presentation tabOrder must contain unique safe object IDs");
            }
            seen.insert(id);
        }
    }
    if (presentation.contains("terminalLayout") && !validTerminalLayout(presentation.value("terminalLayout"), error))
    {
        return false;
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
