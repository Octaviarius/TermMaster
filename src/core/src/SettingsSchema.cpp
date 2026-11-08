#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <Termaster/Core/SettingsSchema.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <valijson/adapters/qvariant_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validator.hpp>
#include <yaml-cpp/eventhandler.h>
#include <yaml-cpp/yaml.h>

static void initializeSettingsSchemaResource()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE(settingsSchemaMeta);
        return true;
    }();
    Q_UNUSED(initialized)
}

namespace
{
using Adapter                = valijson::adapters::QtVariantAdapter;
constexpr qsizetype MaxBytes = 4 * 1024 * 1024;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::invalid_argument(message);
    }
}

/** Reject excessive nesting and aliases before yaml-cpp constructs its node graph. */
class BoundedEvents final : public YAML::EventHandler
{
public:
    void OnDocumentStart(const YAML::Mark&) override
    {
    }
    void OnDocumentEnd() override
    {
    }
    void OnNull(const YAML::Mark&, YAML::anchor_t) override
    {
        _count();
    }
    void OnAlias(const YAML::Mark&, YAML::anchor_t) override
    {
        throw std::invalid_argument("YAML aliases are unsupported");
    }
    void OnScalar(const YAML::Mark&, const std::string&, YAML::anchor_t, const std::string&) override
    {
        _count();
    }
    void OnSequenceStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) override
    {
        _start();
    }
    void OnSequenceEnd() override
    {
        --_depth;
    }
    void OnMapStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) override
    {
        _start();
    }
    void OnMapEnd() override
    {
        --_depth;
    }

private:
    int       _depth = 0;
    qsizetype _nodes = 0;
    void      _count()
    {
        require(++_nodes <= 100000, "YAML exceeds node limit");
    }
    void _start()
    {
        _count();
        require(++_depth <= 64, "YAML exceeds nesting limit");
    }
};

void checkYaml(const YAML::Node& node, int depth = 0)
{
    require(depth <= 64, "YAML exceeds nesting limit");
    if (node.IsMap())
    {
        QSet<QString> keys;
        for (const auto& entry : node)
        {
            require(entry.first.IsScalar(), "YAML mapping keys must be strings");
            auto key = QString::fromStdString(entry.first.Scalar());
            require(!keys.contains(key), "Duplicate YAML mapping key");
            keys.insert(key);
            checkYaml(entry.second, depth + 1);
        }
    }
    else if (node.IsSequence())
    {
        for (const auto& item : node)
        {
            checkYaml(item, depth + 1);
        }
    }
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
    if (tag == "!" || tag == "tag:yaml.org,2002:str")
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
        throw std::invalid_argument("Integer exceeds supported 64-bit range");
    }
    if (tag != "tag:yaml.org,2002:bool" && tag != "tag:yaml.org,2002:int" && number.match(text).hasMatch())
    {
        const auto value = text.toDouble(&ok);
        require(ok && std::isfinite(value), "Number exceeds finite double range");
        return value;
    }
    require(tag.empty() || tag == "?", "Invalid explicit YAML scalar type");
    require(text.compare(".inf", Qt::CaseInsensitive) != 0 && text.compare("-.inf", Qt::CaseInsensitive) != 0
                && text.compare("+.inf", Qt::CaseInsensitive) != 0 && text.compare(".nan", Qt::CaseInsensitive) != 0,
            "Non-finite numbers are unsupported");
    return text;
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

bool validPath(const QString& path)
{
    if (path.isEmpty() || path.contains(QChar::Null) || path.count('/') >= 64)
    {
        return false;
    }
    for (const auto& part : path.split('/'))
    {
        if (part.isEmpty() || part == "." || part == "..")
        {
            return false;
        }
    }
    return true;
}

void supportedNumbers(const QVariant& value, int depth = 0, qsizetype* budget = nullptr)
{
    qsizetype localBudget = 100000;
    if (!budget)
    {
        budget = &localBudget;
    }
    require(depth <= 64 && --*budget >= 0, "Value exceeds nesting or node limit");
    switch (value.typeId())
    {
        case QMetaType::Char:
        case QMetaType::SChar:
        case QMetaType::UChar:
        case QMetaType::Short:
        case QMetaType::UShort:
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::Long:
        case QMetaType::ULong:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
            require(value.toDouble() >= -9007199254740991.0 && value.toDouble() <= 9007199254740991.0,
                    "Schema validation requires integers within the exact double range");
            break;
        default:
            break;
    }
    if (value.typeId() == QMetaType::ULongLong)
    {
        require(value.toULongLong() <= quint64(std::numeric_limits<qint64>::max()),
                "Schema integers exceed signed 64-bit range");
    }
    if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::Float)
    {
        const auto number = value.toDouble();
        require(std::isfinite(number), "Non-finite numbers are unsupported");
        require(std::trunc(number) != number || (number >= -9007199254740991.0 && number <= 9007199254740991.0),
                "Integral numbers exceed the exact validation range");
    }
    if (value.typeId() == QMetaType::QVariantMap)
    {
        for (const auto& item : value.toMap())
        {
            supportedNumbers(item, depth + 1, budget);
        }
    }
    else if (value.typeId() == QMetaType::QVariantList || value.typeId() == QMetaType::QStringList)
    {
        for (const auto& item : value.toList())
        {
            supportedNumbers(item, depth + 1, budget);
        }
    }
    else
    {
        require(!value.isValid() || value.typeId() == QMetaType::Nullptr || value.typeId() == QMetaType::QString
                    || value.typeId() == QMetaType::Bool || value.typeId() == QMetaType::Char
                    || value.typeId() == QMetaType::SChar || value.typeId() == QMetaType::UChar
                    || value.typeId() == QMetaType::Short || value.typeId() == QMetaType::UShort
                    || value.typeId() == QMetaType::Int || value.typeId() == QMetaType::UInt
                    || value.typeId() == QMetaType::Long || value.typeId() == QMetaType::ULong
                    || value.typeId() == QMetaType::LongLong || value.typeId() == QMetaType::ULongLong
                    || value.typeId() == QMetaType::Double || value.typeId() == QMetaType::Float,
                "Unsupported value type");
    }
}

QString stringMember(const QVariantMap& map, const QString& key, const QString& fallback = {})
{
    if (!map.contains(key))
    {
        return fallback;
    }
    require(map.value(key).typeId() == QMetaType::QString, "Schema annotation must be a string");
    return map.value(key).toString();
}

int orderMember(const QVariantMap& map)
{
    if (!map.contains("order"))
    {
        return 0;
    }
    auto value = map.value("order");
    require(value.typeId() == QMetaType::LongLong || value.typeId() == QMetaType::Int, "UI order must be an integer");
    auto order = value.toLongLong();
    require(order >= std::numeric_limits<int>::min() && order <= std::numeric_limits<int>::max(),
            "UI order exceeds integer range");
    return int(order);
}

QVariantMap mapMember(const QVariantMap& map, const QString& key)
{
    if (!map.contains(key))
    {
        return {};
    }
    require(map.value(key).typeId() == QMetaType::QVariantMap, "Schema annotation must be a mapping");
    return map.value(key).toMap();
}

QVariant resolvePointer(const QVariantMap& root, const QString& reference)
{
    require(reference == "#" || reference.startsWith("#/"), "Only local JSON Pointer references are supported");
    QVariant current = root;
    if (reference == "#")
    {
        return current;
    }
    for (auto part : reference.mid(2).split('/'))
    {
        require(!QRegularExpression("~(?![01])").match(part).hasMatch(), "Invalid JSON Pointer escape");
        part.replace("~1", "/");
        part.replace("~0", "~");
        require(current.typeId() == QMetaType::QVariantMap && current.toMap().contains(part),
                "Unresolved local schema reference");
        current = current.toMap().value(part);
    }
    require(current.typeId() == QMetaType::QVariantMap || current.typeId() == QMetaType::Bool,
            "Reference target must be a schema");
    return current;
}

void inspectSchema(const QVariant&    value,
                   const QVariantMap& root,
                   QSet<QString>      references = {},
                   int                depth      = 0,
                   qsizetype*         budget     = nullptr)
{
    qsizetype localBudget = 100000;
    if (!budget)
    {
        budget = &localBudget;
    }
    require(depth <= 64 && --*budget >= 0, "Schema reference expansion exceeds nesting or node limit");
    if (value.typeId() == QMetaType::Bool)
    {
        return;
    }
    require(value.typeId() == QMetaType::QVariantMap, "Expected a schema mapping or boolean");
    auto map = value.toMap();
    require(!map.contains("x-ui"), "Use flat x-widget, x-group and x-order annotations");
    stringMember(map, "x-widget");
    stringMember(map, "x-group");
    mapMember(map, "x-widget-options");
    if (map.contains("x-order"))
    {
        orderMember({
            {"order", map.value("x-order")}
        });
    }
    if (map.contains("x-settings-path"))
    {
        require(validPath(stringMember(map, "x-settings-path")), "Invalid field settings path");
    }
    if (map.contains("$ref"))
    {
        auto reference = stringMember(map, "$ref");
        require(!references.contains(reference), "Cyclic schema references are unsupported");
        references.insert(reference);
        inspectSchema(resolvePointer(root, reference), root, references, depth + 1, budget);
    }
    for (const auto& keyword : {"properties", "patternProperties", "definitions", "$defs"})
    {
        if (map.contains(keyword))
        {
            for (const auto& item : mapMember(map, keyword))
            {
                inspectSchema(item, root, references, depth + 1, budget);
            }
        }
    }
    for (const auto& keyword :
         {"additionalProperties", "additionalItems", "contains", "propertyNames", "not", "if", "then", "else"})
    {
        if (map.contains(keyword))
        {
            inspectSchema(map.value(keyword), root, references, depth + 1, budget);
        }
    }
    for (const auto& keyword : {"items", "allOf", "anyOf", "oneOf"})
    {
        if (map.contains(keyword))
        {
            auto item = map.value(keyword);
            if (item.typeId() == QMetaType::QVariantList)
            {
                for (const auto& child : item.toList())
                {
                    inspectSchema(child, root, references, depth + 1, budget);
                }
            }
            else
            {
                inspectSchema(item, root, references, depth + 1, budget);
            }
        }
    }
    if (map.contains("dependencies"))
    {
        for (const auto& item : mapMember(map, "dependencies"))
        {
            if (item.typeId() != QMetaType::QVariantList)
            {
                inspectSchema(item, root, references, depth + 1, budget);
            }
        }
    }
}

QVariantMap resolvedField(QVariantMap definition, const QVariantMap& root)
{
    // Draft 7 ignores siblings of $ref during validation. UI annotations may override referenced annotations.
    if (!definition.contains("$ref"))
    {
        return definition;
    }
    auto referenced = resolvePointer(root, stringMember(definition, "$ref"));
    require(referenced.typeId() == QMetaType::QVariantMap, "Form fields must resolve to schema mappings");
    auto result = resolvedField(referenced.toMap(), root);
    for (auto it = definition.cbegin(); it != definition.cend(); ++it)
    {
        if (it.key() == "title" || it.key() == "description" || it.key() == "readOnly" || it.key() == "default"
            || it.key() == "x-widget" || it.key() == "x-group" || it.key() == "x-order"
            || it.key() == "x-widget-options" || it.key() == "x-settings-path")
        {
            result.insert(it.key(), it.value());
        }
    }
    return result;
}
} // namespace

struct SettingsSchema::Data
{
    QList<Field>     fields;
    QList<Group>     groups;
    valijson::Schema schema;
};

QVariant SettingsSchema::parseValue(const QByteArray& yaml)
{
    try
    {
        require(yaml.size() <= MaxBytes, "YAML exceeds 4 MiB");
        std::istringstream stream(yaml.toStdString());
        YAML::Parser       parser(stream);
        BoundedEvents      events;
        int                documents = 0;
        while (parser.HandleNextDocument(events))
        {
            require(++documents == 1, "Expected one YAML document");
        }
        require(documents == 1, "Expected one YAML document");
        auto node = YAML::Load(yaml.toStdString());
        checkYaml(node);
        return fromYaml(node);
    }
    catch (const std::exception&)
    {
        // Parser exceptions can include scalar contents, so never forward them.
        throw std::invalid_argument("Invalid YAML value or resource limit exceeded");
    }
}

QByteArray SettingsSchema::serializeValue(const QVariant& value)
{
    qsizetype remaining = 100000;
    auto      bytes     = QByteArray::fromStdString(YAML::Dump(toYaml(value, remaining)));
    require(bytes.size() <= MaxBytes, "YAML exceeds 4 MiB");
    return bytes;
}

SettingsSchema::SettingsSchema(const QByteArray& yamlDescriptor) : _data(std::make_unique<Data>())
{
    try
    {
        auto document = parseValue(yamlDescriptor);
        require(document.typeId() == QMetaType::QVariantMap, "Schema envelope must be a mapping");
        auto envelope = document.toMap();
        require(envelope.value("version").typeId() == QMetaType::LongLong
                    && envelope.value("version").toLongLong() == 1,
                "Expected schema version 1");
        require(stringMember(envelope, "type") == "termaster.settings-schema", "Unexpected schema document type");
        require(envelope.contains("description"), "Schema description is required");
        stringMember(envelope, "description");
        require(envelope.contains("content"), "Schema content is required");
        auto root = mapMember(envelope, "content");
        require(stringMember(root, "type") == "object", "Form schema root must have object type");
        supportedNumbers(root);
        inspectSchema(root, root);

        initializeSettingsSchemaResource();
        QFile metaFile(":/Termaster/Core/SettingsSchemaDraft7.json");
        require(metaFile.open(QIODevice::ReadOnly), "Draft 7 metaschema resource is unavailable");
        auto                   metaValue = QJsonDocument::fromJson(metaFile.readAll()).toVariant();
        valijson::Schema       metaSchema;
        valijson::SchemaParser parser(valijson::SchemaParser::kDraft7);
        parser.populateSchema(Adapter(metaValue), metaSchema);
        valijson::Validator validator(valijson::Validator::kStrongTypes);
        require(validator.validate(metaSchema, Adapter(QVariant(root)), nullptr), "Invalid Draft 7 schema structure");
        parser.populateSchema(Adapter(QVariant(root)), _data->schema);

        auto          rootUi = root;
        QSet<QString> groupIds;
        if (rootUi.contains("x-groups"))
        {
            require(rootUi.value("x-groups").typeId() == QMetaType::QVariantList, "UI groups must be a sequence");
            for (const auto& value : rootUi.value("x-groups").toList())
            {
                require(value.typeId() == QMetaType::QVariantMap, "UI group must be a mapping");
                auto map = value.toMap();
                auto id  = stringMember(map, "id");
                require(!id.isEmpty() && !groupIds.contains(id), "UI group identifiers must be nonempty and unique");
                groupIds.insert(id);
                _data->groups.append({id, stringMember(map, "title", id), orderMember(map)});
            }
        }
        auto        required = root.value("required").toList();
        QStringList paths;
        auto        properties = mapMember(root, "properties");
        for (auto it = properties.cbegin(); it != properties.cend(); ++it)
        {
            require(it.value().typeId() == QMetaType::QVariantMap, "Form fields must be schema mappings");
            auto  definition = resolvedField(it.value().toMap(), root);
            Field field;
            field.key          = it.key();
            field.definition   = definition;
            field.settingsPath = stringMember(definition, "x-settings-path", field.key);
            require(validPath(field.settingsPath), "Invalid field settings path");
            paths.append(field.settingsPath);
            field.type = definition.value("type").typeId() == QMetaType::QString ? definition.value("type").toString() :
                                                                                   QString();
            field.title       = stringMember(definition, "title", field.key);
            field.description = stringMember(definition, "description");
            if (definition.contains("readOnly"))
            {
                require(definition.value("readOnly").typeId() == QMetaType::Bool, "readOnly must be boolean");
            }
            field.readOnly     = definition.value("readOnly").toBool();
            field.required     = required.contains(field.key);
            field.hasDefault   = definition.contains("default");
            field.defaultValue = definition.value("default");
            field.presentation = mapMember(definition, "x-widget-options");
            field.widget       = stringMember(definition, "x-widget");
            field.group        = stringMember(definition, "x-group");
            field.order        = definition.contains("x-order") ? orderMember({
                                                                      {"order", definition.value("x-order")}
            }) :
                                                                  0;
            if (!field.group.isEmpty() && !groupIds.contains(field.group))
            {
                groupIds.insert(field.group);
                _data->groups.append({field.group, field.group, 0});
            }
            _data->fields.append(field);
        }
        QSet<QString> uniquePaths;
        for (const auto& path : paths)
        {
            require(!uniquePaths.contains(path), "Field settings paths overlap");
            uniquePaths.insert(path);
        }
        for (const auto& path : paths)
        {
            auto parent = path;
            while (parent.contains('/'))
            {
                parent.truncate(parent.lastIndexOf('/'));
                require(!uniquePaths.contains(parent), "Field settings paths overlap");
            }
        }
        std::stable_sort(_data->groups.begin(), _data->groups.end(), [](const Group& a, const Group& b) {
            return a.order == b.order ? a.id < b.id : a.order < b.order;
        });
        auto groupOrder = [this](const QString& id) {
            for (qsizetype i = 0; i < _data->groups.size(); ++i)
            {
                if (_data->groups[i].id == id)
                {
                    return i + 1;
                }
            }
            return qsizetype(0);
        };
        std::stable_sort(_data->fields.begin(), _data->fields.end(), [&](const Field& a, const Field& b) {
            if (a.group != b.group)
            {
                return groupOrder(a.group) < groupOrder(b.group);
            }
            return a.order == b.order ? a.key < b.key : a.order < b.order;
        });
    }
    catch (const std::invalid_argument&)
    {
        throw;
    }
    catch (const std::exception&)
    {
        throw std::invalid_argument("Invalid settings schema descriptor or resource limit exceeded");
    }
}

SettingsSchema::~SettingsSchema() = default;

const QList<SettingsSchema::Field>& SettingsSchema::fields() const
{
    return _data->fields;
}

const QList<SettingsSchema::Group>& SettingsSchema::groups() const
{
    return _data->groups;
}

QList<SettingsSchema::Error> SettingsSchema::validate(const QVariantMap& document) const
{
    try
    {
        supportedNumbers(document);
        valijson::Validator         validator(valijson::Validator::kStrongTypes);
        valijson::ValidationResults results;
        const bool                  valid = validator.validate(_data->schema, Adapter(QVariant(document)), &results);
        QList<Error>                errors;
        QSet<QString>               paths;
        for (const auto& result : results)
        {
            auto path = QString::fromStdString(result.jsonPointer);
            if (!paths.contains(path))
            {
                paths.insert(path);
                errors.append({path, "Value does not satisfy the settings schema"});
                if (errors.size() >= 64)
                {
                    break;
                }
            }
        }
        if (!valid && errors.isEmpty())
        {
            errors.append({QString(), "Value does not satisfy the settings schema"});
        }
        return errors;
    }
    catch (const std::exception&)
    {
        return {
            {QString(), "Unsupported settings value or resource limit exceeded"}
        };
    }
}
