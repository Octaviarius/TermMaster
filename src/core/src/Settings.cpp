#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <Termaster/Core/Path.h>
#include <Termaster/Core/Settings.h>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <yaml-cpp/yaml.h>

namespace
{
constexpr qint64 MaxSettingsBytes = 4 * 1024 * 1024;

void validateTree(const YAML::Node& node, qsizetype& remaining, int depth = 0)
{
    if (depth > 65 || --remaining < 0)
    {
        throw std::runtime_error("YAML exceeds nesting or node limit");
    }
    if (node.IsMap())
    {
        QSet<QString> keys;
        for (const auto& entry : node)
        {
            if (!entry.first.IsScalar())
            {
                throw std::runtime_error("YAML mapping keys must be scalar");
            }
            const auto key = QString::fromStdString(entry.first.Scalar());
            if (keys.contains(key))
            {
                throw std::runtime_error("Duplicate YAML mapping key");
            }
            keys.insert(key);
            validateTree(entry.second, remaining, depth + 1);
        }
    }
    else if (node.IsSequence())
    {
        for (const auto& entry : node)
        {
            validateTree(entry, remaining, depth + 1);
        }
    }
}

void preserveQuotedStrings(YAML::Node node)
{
    if (node.IsScalar() && node.Tag() == "!")
    {
        // yaml-cpp drops the nonspecific quoted-string tag when dumping nodes.
        node.SetTag("tag:yaml.org,2002:str");
    }
    else if (node.IsMap())
    {
        for (auto entry : node)
        {
            preserveQuotedStrings(entry.first);
            preserveQuotedStrings(entry.second);
        }
    }
    else if (node.IsSequence())
    {
        for (auto entry : node)
        {
            preserveQuotedStrings(entry);
        }
    }
}

QByteArray serialize(const YAML::Node& root)
{
    qsizetype remaining = 100000;
    validateTree(root, remaining);
    auto output = YAML::Clone(root);
    preserveQuotedStrings(output);
    const auto bytes = QByteArray::fromStdString(YAML::Dump(output));
    if (bytes.size() > MaxSettingsBytes)
    {
        throw std::runtime_error("Settings exceed 4 MiB");
    }
    return bytes;
}

bool validKey(const QString& key)
{
    if (key.isEmpty() || key.count('/') >= 64)
    {
        return false;
    }
    for (const auto& part : key.split('/'))
    {
        if (part.isEmpty() || part == "." || part == "..")
        {
            return false;
        }
    }
    return true;
}

void validatePath(const QString& path)
{
    if (path.isEmpty() || path.contains(QChar::Null))
    {
        throw std::runtime_error("Settings path is empty or contains NUL");
    }
}

bool failure(QString* error, const QString& action, const QString& path, const std::exception& exception)
{
    if (error)
    {
        *error = QString("Cannot %1 %2: %3").arg(action, path, QString::fromUtf8(exception.what()));
    }
    return false;
}

QVariant fromYaml(const YAML::Node& node)
{
    if (node.IsNull() || !node.IsDefined())
    {
        return {};
    }
    if (node.IsMap())
    {
        QVariantMap map;
        for (const auto& entry : node)
        {
            map.insert(QString::fromStdString(entry.first.Scalar()), fromYaml(entry.second));
        }
        return map;
    }
    if (node.IsSequence())
    {
        QVariantList list;
        for (const auto& entry : node)
        {
            list.append(fromYaml(entry));
        }
        return list;
    }
    const auto text = QString::fromStdString(node.Scalar());
    const auto tag  = node.Tag();
    if (tag == "!" || tag == "tag:yaml.org,2002:str"
        || (!tag.empty() && tag != "?" && tag != "tag:yaml.org,2002:bool" && tag != "tag:yaml.org,2002:int"
            && tag != "tag:yaml.org,2002:float"))
    {
        return text;
    }
    if ((tag == "?" || tag.empty() || tag == "tag:yaml.org,2002:bool")
        && (text == "true" || text == "True" || text == "TRUE"))
    {
        return true;
    }
    if ((tag == "?" || tag.empty() || tag == "tag:yaml.org,2002:bool")
        && (text == "false" || text == "False" || text == "FALSE"))
    {
        return false;
    }
    static const QRegularExpression integer("^[+-]?[0-9]+$");
    static const QRegularExpression number("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$");
    bool                            ok = false;
    if (tag != "tag:yaml.org,2002:bool" && tag != "tag:yaml.org,2002:float" && integer.match(text).hasMatch())
    {
        const auto signedValue = text.toLongLong(&ok);
        if (ok)
        {
            return signedValue;
        }
        const auto unsignedValue = text.toULongLong(&ok);
        if (ok)
        {
            return unsignedValue;
        }
        // Preserve an out-of-range integer exactly rather than rounding it to double.
        return text;
    }
    if (tag != "tag:yaml.org,2002:bool" && tag != "tag:yaml.org,2002:int" && number.match(text).hasMatch())
    {
        const auto value = text.toDouble(&ok);
        if (ok && std::isfinite(value))
        {
            return value;
        }
    }
    return text;
}

YAML::Node envelope(const Settings::Info& info)
{
    YAML::Node root(YAML::NodeType::Map);
    root["version"] = 1;
    root["type"]    = info.type.toStdString();
    root["type"].SetTag("tag:yaml.org,2002:str");
    root["description"] = info.description.toStdString();
    root["description"].SetTag("tag:yaml.org,2002:str");
    root["content"] = YAML::Node(YAML::NodeType::Map);
    return root;
}

bool isStringMetadata(const YAML::Node& node)
{
    if (!node.IsScalar())
    {
        return false;
    }
    const auto tag = node.Tag();
    if (tag == "!" || tag == "tag:yaml.org,2002:str")
    {
        return true;
    }
    if (!tag.empty() && tag != "?")
    {
        return false;
    }
    // Numeric overflow must not turn an implicitly typed number into string metadata.
    static const QRegularExpression number("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$");
    bool                            boolean = false;
    double                          real    = 0;
    return !number.match(QString::fromStdString(node.Scalar())).hasMatch()
        && !YAML::convert<bool>::decode(node, boolean) && !YAML::convert<double>::decode(node, real);
}

void validateEnvelope(const YAML::Node& root, const Settings::Info& info)
{
    const auto version = root["version"];
    if (!version.IsScalar() || version.Scalar() != "1"
        || (version.Tag() != "?" && !version.Tag().empty() && version.Tag() != "tag:yaml.org,2002:int"))
    {
        throw std::runtime_error("Expected settings version 1 as an integer");
    }
    const auto type = fromYaml(root["type"]);
    if (type.typeId() != QMetaType::QString || type.toString() != info.type)
    {
        throw std::runtime_error("Settings document type does not match the expected type");
    }
    if (!isStringMetadata(root["description"]))
    {
        throw std::runtime_error("Settings description must be a string");
    }
    if (!root["content"].IsMap())
    {
        throw std::runtime_error("Settings content must be a mapping");
    }
}

YAML::Node detachContent(YAML::Node root)
{
    const auto content = YAML::Clone(root["content"]);
    root.remove("content");
    root["content"] = content;
    return root["content"];
}

YAML::Node toYaml(const QVariant& value, qsizetype& remaining, int depth = 0)
{
    if (depth > 64 || --remaining < 0)
    {
        throw std::runtime_error("Value exceeds nesting or node limit");
    }
    YAML::Node node;
    if (!value.isValid() || value.typeId() == QMetaType::Nullptr)
    {
        return node;
    }
    switch (value.typeId())
    {
        case QMetaType::QVariantMap:
        {
            node           = YAML::Node(YAML::NodeType::Map);
            const auto map = value.toMap();
            for (const auto& entry : map.asKeyValueRange())
            {
                node[entry.first.toStdString()] = toYaml(entry.second, remaining, depth + 1);
            }
            break;
        }
        case QMetaType::QVariantList:
        case QMetaType::QStringList:
            node = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& item : value.toList())
            {
                node.push_back(toYaml(item, remaining, depth + 1));
            }
            break;
        case QMetaType::QString:
            node = value.toString().toStdString();
            node.SetTag("tag:yaml.org,2002:str");
            break;
        case QMetaType::Bool:
            node = value.toBool();
            break;
        case QMetaType::Char:
        case QMetaType::SChar:
        case QMetaType::Short:
        case QMetaType::Int:
        case QMetaType::Long:
        case QMetaType::LongLong:
            node = value.toLongLong();
            break;
        case QMetaType::UChar:
        case QMetaType::UShort:
        case QMetaType::UInt:
        case QMetaType::ULong:
        case QMetaType::ULongLong:
            node = value.toULongLong();
            break;
        case QMetaType::Float:
        case QMetaType::Double:
            if (!std::isfinite(value.toDouble()))
            {
                throw std::runtime_error("Non-finite settings numbers are unsupported");
            }
            node = value.toDouble();
            node.SetTag("tag:yaml.org,2002:float");
            break;
        default:
            throw std::runtime_error("Unsupported settings value type");
    }
    return node;
}

YAML::Node lookup(const YAML::Node& node, const QStringList& parts, qsizetype index = 0)
{
    if (!node.IsMap())
    {
        return YAML::Node(YAML::NodeType::Undefined);
    }
    const auto child = node[parts[index].toStdString()];
    if (!child.IsDefined() || index == parts.size() - 1)
    {
        return child;
    }
    return lookup(child, parts, index + 1);
}

bool update(YAML::Node node, const QStringList& parts, qsizetype index, const YAML::Node& value, bool remove)
{
    const auto key = parts[index].toStdString();
    if (index == parts.size() - 1)
    {
        if (remove)
        {
            return node.remove(key);
        }
        // Replace the mapping entry instead of assigning through an aliased node.
        node.remove(key);
        node[key] = value;
        return true;
    }
    const auto child = static_cast<const YAML::Node&>(node)[key];
    if (!child.IsDefined())
    {
        if (remove)
        {
            return false;
        }
        node[key] = YAML::Node(YAML::NodeType::Map);
    }
    else if (!child.IsMap())
    {
        throw std::runtime_error("Settings path crosses a non-mapping value");
    }
    else
    {
        // Clone alone preserves aliases; detach the changed branch from sibling aliases.
        const auto detached = YAML::Clone(child);
        node.remove(key);
        node[key] = detached;
    }
    return update(node[key], parts, index + 1, value, remove);
}
} // namespace

struct Settings::Data
{
    QString              path;
    Info                 expectedInfo;
    YAML::Node           root {YAML::NodeType::Map};
    std::shared_ptr<int> lifetime = std::make_shared<int>(0);
    bool                 dirty    = false;
};

Settings::Settings(QString filePath) : Settings(std::move(filePath), Info {})
{
}

Settings::Settings(const Path& filePath) : Settings(filePath.toString(), Info {})
{
}

Settings::Settings(QString filePath, Info info) : _data(std::make_unique<Data>())
{
    _data->path         = std::move(filePath);
    _data->expectedInfo = std::move(info);
    _data->root.reset(envelope(_data->expectedInfo));
}

Settings::Settings(const Path& filePath, Info info) : Settings(filePath.toString(), std::move(info))
{
}

Settings::~Settings() = default;

QString Settings::filePath() const
{
    return _data->path;
}

Settings::Info Settings::info() const
{
    return {QString::fromStdString(_data->root["type"].Scalar()),
            QString::fromStdString(_data->root["description"].Scalar())};
}

int Settings::version() const
{
    return 1;
}

bool Settings::isDirty() const
{
    return _data->dirty;
}

bool Settings::load(QString* error)
{
    if (error)
    {
        error->clear();
    }
    try
    {
        validatePath(_data->path);
        YAML::Node root = envelope(_data->expectedInfo);
        QFile      file(_data->path);
        if (file.exists())
        {
            if (!file.open(QIODevice::ReadOnly))
            {
                throw std::runtime_error(file.errorString().toStdString());
            }
            const auto bytes = file.read(MaxSettingsBytes + 1);
            if (file.error() != QFileDevice::NoError)
            {
                throw std::runtime_error(file.errorString().toStdString());
            }
            if (bytes.size() > MaxSettingsBytes)
            {
                throw std::runtime_error("Settings file exceeds 4 MiB");
            }
            const auto documents = YAML::LoadAll(bytes.toStdString());
            if (documents.size() != 1 || !documents.front().IsMap())
            {
                throw std::runtime_error("Expected one YAML mapping document");
            }
            root.reset(documents.front());
            qsizetype remaining = 100000;
            validateTree(root, remaining);
            validateEnvelope(root, _data->expectedInfo);
        }
        _data->root.reset(root);
        _data->dirty = false;
        return true;
    }
    catch (const std::exception& exception)
    {
        return failure(error, "read", _data->path, exception);
    }
}

bool Settings::sync(QString* error)
{
    if (error)
    {
        error->clear();
    }
    try
    {
        validatePath(_data->path);
        if (!_data->dirty)
        {
            return true;
        }
        const auto bytes = serialize(_data->root);
        if (!QDir().mkpath(QFileInfo(_data->path).absolutePath()))
        {
            throw std::runtime_error("Cannot create settings directory");
        }
        QSaveFile file(_data->path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        {
            throw std::runtime_error(file.errorString().toStdString());
        }
        _data->dirty = false;
        return true;
    }
    catch (const std::exception& exception)
    {
        return failure(error, "write", _data->path, exception);
    }
}

bool Settings::contains(const QString& key) const
{
    return validKey(key) && lookup(_data->root["content"], key.split('/')).IsDefined();
}

QVariant Settings::value(const QString& key, const QVariant& fallback) const
{
    if (!validKey(key))
    {
        return fallback;
    }
    const auto node = lookup(_data->root["content"], key.split('/'));
    return node.IsDefined() ? fromYaml(node) : fallback;
}

QVariantMap Settings::values() const
{
    return fromYaml(_data->root["content"]).toMap();
}

bool Settings::setValue(const QString& key, const QVariant& value, QString* error)
{
    if (error)
    {
        error->clear();
    }
    try
    {
        if (!validKey(key))
        {
            throw std::runtime_error("Invalid settings key");
        }
        qsizetype  remaining = 100000;
        const auto converted = toYaml(value, remaining);
        auto       candidate = YAML::Clone(_data->root);
        update(detachContent(candidate), key.split('/'), 0, converted, false);
        serialize(candidate);
        _data->root.reset(candidate);
        _data->dirty = true;
        return true;
    }
    catch (const std::exception& exception)
    {
        return failure(error, "set", key, exception);
    }
}

bool Settings::remove(const QString& key, QString* error)
{
    if (error)
    {
        error->clear();
    }
    try
    {
        if (!validKey(key))
        {
            throw std::runtime_error("Invalid settings key");
        }
        auto candidate = YAML::Clone(_data->root);
        if (!update(detachContent(candidate), key.split('/'), 0, YAML::Node(), true))
        {
            return true;
        }
        serialize(candidate);
        _data->root.reset(candidate);
        _data->dirty = true;
        return true;
    }
    catch (const std::exception& exception)
    {
        return failure(error, "remove", key, exception);
    }
}

struct Settings::Node::Context
{
    Settings*          owner = nullptr;
    std::weak_ptr<int> lifetime;
    QString            prefix;
    bool               validPath = true;
};

Settings::Node::Node(std::shared_ptr<Context> context) : _context(std::move(context))
{
}

bool Settings::Node::isValid() const
{
    if (!_context || !_context->validPath || _context->lifetime.expired())
    {
        return false;
    }
    if (_context->prefix.isEmpty())
    {
        return true;
    }
    YAML::Node current = _context->owner->_data->root["content"];
    for (const auto& part : _context->prefix.split('/'))
    {
        if (!current.IsMap())
        {
            return false;
        }
        const auto child = static_cast<const YAML::Node&>(current)[part.toStdString()];
        if (!child.IsDefined())
        {
            return true;
        }
        current.reset(child);
    }
    return current.IsMap();
}

QString Settings::Node::path() const
{
    return _context ? _context->prefix : QString();
}

QString Settings::Node::_key(const QString& relative) const
{
    return !_context || _context->prefix.isEmpty() ? relative : _context->prefix + '/' + relative;
}

bool Settings::Node::contains(const QString& key) const
{
    return isValid() && validKey(key) && _context->owner->contains(_key(key));
}

QVariant Settings::Node::value(const QString& key, const QVariant& fallback) const
{
    return isValid() && validKey(key) ? _context->owner->value(_key(key), fallback) : fallback;
}

QVariantMap Settings::Node::values() const
{
    if (!isValid())
    {
        return {};
    }
    if (_context->prefix.isEmpty())
    {
        return _context->owner->values();
    }
    return _context->owner->value(_context->prefix).toMap();
}

bool Settings::Node::_writable(const QString& key, QString* error) const
{
    if (error)
    {
        error->clear();
    }
    if (!isValid() || !validKey(key))
    {
        if (error)
        {
            *error = !isValid() ? "Settings scope is invalid or its owner has been destroyed" : "Invalid settings key";
        }
        return false;
    }
    return true;
}

bool Settings::Node::setValue(const QString& key, const QVariant& value, QString* error)
{
    return _writable(key, error) && _context->owner->setValue(_key(key), value, error);
}

bool Settings::Node::remove(const QString& key, QString* error)
{
    return _writable(key, error) && _context->owner->remove(_key(key), error);
}

Settings::Node Settings::Node::node(const QString& path) const
{
    auto context       = _context ? std::make_shared<Context>(*_context) : std::make_shared<Context>();
    context->validPath = context->validPath && (path.isEmpty() || validKey(path));
    if (!path.isEmpty())
    {
        context->prefix    = _key(path);
        context->validPath = context->validPath && validKey(context->prefix);
    }
    return Node(std::move(context));
}

Settings::Node Settings::node(const QString& path)
{
    auto context       = std::make_shared<Node::Context>();
    context->owner     = this;
    context->lifetime  = _data->lifetime;
    context->prefix    = path;
    context->validPath = path.isEmpty() || validKey(path);
    return Node(std::move(context));
}
