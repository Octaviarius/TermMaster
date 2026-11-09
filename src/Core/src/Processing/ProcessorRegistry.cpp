#include <QDebug>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <Termaster/Core/Processing/Lineflow.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/SettingsSchema.h>
#include <exception>
#include <stdexcept>

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

QString owned(const QString& text)
{
    return QString(text.constData(), text.size());
}

QVariant copyValue(const QVariant& value, int depth = 0)
{
    if (depth > 64)
    {
        throw std::runtime_error("Processor configuration nesting exceeds 64");
    }

    if (value.typeId() == QMetaType::QString)
    {
        return owned(value.toString());
    }
    if (value.typeId() == QMetaType::QByteArray)
    {
        const auto bytes = value.toByteArray();
        return QByteArray(bytes.constData(), bytes.size());
    }
    if (value.typeId() == QMetaType::QVariantMap)
    {
        QVariantMap result;
        const auto  map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
        {
            result.insert(owned(it.key()), copyValue(it.value(), depth + 1));
        }
        return result;
    }
    if (value.typeId() == QMetaType::QVariantList || value.typeId() == QMetaType::QStringList)
    {
        QVariantList result;
        for (const auto& item : value.toList())
        {
            result.append(copyValue(item, depth + 1));
        }
        return result;
    }
    return value;
}

QVariantMap merged(QVariantMap base, const QVariantMap& layer, int depth = 0)
{
    if (depth > 64)
    {
        throw std::runtime_error("Processor configuration nesting exceeds 64");
    }

    for (auto it = layer.cbegin(); it != layer.cend(); ++it)
    {
        const auto prior = base.value(it.key());
        if (prior.typeId() == QMetaType::QVariantMap && it.value().typeId() == QMetaType::QVariantMap)
        {
            base.insert(owned(it.key()), merged(prior.toMap(), it.value().toMap(), depth + 1));
        }
        else
        {
            base.insert(owned(it.key()), copyValue(it.value(), depth + 1));
        }
    }
    return base;
}

std::optional<QVariant> schemaDefault(const QVariantMap& definition, int depth = 0)
{
    if (depth > 64)
    {
        throw std::runtime_error("Processor schema default nesting exceeds 64");
    }

    const bool explicitDefault = definition.contains("default");
    const auto value           = definition.value("default");
    const auto properties      = definition.value("properties").toMap();
    const bool object          = definition.value("type").toString() == "object" || !properties.isEmpty();
    if (!object || (explicitDefault && value.typeId() != QMetaType::QVariantMap))
    {
        return explicitDefault ? std::optional<QVariant>(copyValue(value)) : std::nullopt;
    }

    QVariantMap defaults;
    for (auto it = properties.cbegin(); it != properties.cend(); ++it)
    {
        const auto child = schemaDefault(it.value().toMap(), depth + 1);
        if (child)
        {
            defaults.insert(owned(it.key()), *child);
        }
    }
    if (!explicitDefault && defaults.isEmpty())
    {
        return std::nullopt;
    }
    return QVariant(merged(defaults, explicitDefault ? value.toMap() : QVariantMap {}));
}

ProcessorRegistry::Type metadata(const ProcessorRegistry::Type& type)
{
    ProcessorRegistry::Type result;
    result.id             = owned(type.id);
    result.title          = owned(type.title);
    result.settingsSchema = QByteArray(type.settingsSchema.constData(), type.settingsSchema.size());
    result.kind           = owned(type.kind);
    result.origin         = owned(type.origin);
    result.group          = owned(type.group);
    result.defaults       = copyValue(type.defaults).toMap();
    result.editor         = copyValue(type.editor).toMap();
    result.definition     = copyValue(type.definition).toMap();
    for (const auto& reference : type.references)
    {
        result.references.append(owned(reference));
    }
    result.available         = type.available;
    result.unavailableReason = owned(type.unavailableReason);
    return result;
}

const QByteArray defaultSchema = R"(version: 1
type: termaster.settings-schema
description: Processor settings
content:
  type: object
  properties: {}
)";

bool validId(const QString& id)
{
    static const QRegularExpression pattern("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\\z");
    return pattern.match(id).hasMatch();
}

struct Lease
{
    std::function<void()> release;
    ~Lease()
    {
        release();
    }
};

class ForwardProcessor final : public Processor
{
public:
    explicit ForwardProcessor(std::shared_ptr<Processor> instance) : _instance(std::move(instance))
    {
    }
    QVariantMap configuration() const override
    {
        return _instance->configuration();
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        return _instance->accept(std::move(input), std::move(callbacks));
    }
    void cancel(std::function<void()> ready) override
    {
        _instance->cancel(std::move(ready));
    }

private:
    std::shared_ptr<Processor> _instance;
};
} // namespace

struct ProcessorRegistry::Entry
{
    QString                         owner;
    Type                            type;
    std::unique_ptr<SettingsSchema> schema;
    QVariantMap                     defaults;
    int                             instances = 0;
    bool                            saved     = false;
};

struct ProcessorRegistry::ProviderEntry
{
    QString  owner;
    Provider provider;
    int      instances = 0;
};

ProcessorRegistry::ProcessorRegistry(QObject* parent) : QObject(parent)
{
}
ProcessorRegistry::~ProcessorRegistry() = default;

std::shared_ptr<ProcessorRegistry::Entry> ProcessorRegistry::_definitionEntry(const QString&    owner,
                                                                              const Definition& definition)
{
    if (!validId(definition.id) || definition.title.trimmed().isEmpty() || definition.title.size() > 256
        || definition.title.contains(QChar::Null) || !validId(definition.kind))
    {
        throw std::invalid_argument("Invalid processor definition ID, title or kind");
    }
    auto entry         = std::make_shared<Entry>();
    entry->owner       = owned(owner);
    entry->saved       = true;
    entry->type.id     = owned(definition.id);
    entry->type.title  = owned(definition.title);
    entry->type.kind   = owned(definition.kind);
    entry->type.origin = owned(owner);
    if (definition.group.size() > 128 || definition.group.contains(QChar::Null)
        || definition.group != definition.group.trimmed())
    {
        throw std::invalid_argument("Processor group must be trimmed, at most 128 characters and contain no NUL");
    }
    entry->type.group          = owned(definition.group);
    entry->type.settingsSchema = definition.settingsSchema.isEmpty() ? defaultSchema :
                                                                       QByteArray(definition.settingsSchema.constData(),
                                                                                  definition.settingsSchema.size());
    entry->type.definition     = copyValue(definition.source).toMap();
    entry->type.editor         = copyValue(definition.editor).toMap();
    for (const auto& reference : definition.references)
    {
        if (!validId(reference))
        {
            throw std::invalid_argument("Invalid processor reference ID");
        }
        entry->type.references.append(owned(reference));
    }
    entry->schema = std::make_unique<SettingsSchema>(entry->type.settingsSchema);
    for (const auto& field : entry->schema->fields())
    {
        auto value = schemaDefault(field.definition);
        if (value)
        {
            entry->defaults.insert(owned(field.key), *value);
        }
    }
    entry->defaults = merged(entry->defaults, definition.defaults);
    if (!entry->schema->validate(entry->defaults).isEmpty())
    {
        throw std::invalid_argument("Invalid processor defaults");
    }
    entry->type.defaults = entry->defaults;
    if (definition.kind == "lineflow")
    {
        const auto nodes = definition.source.value("nodes");
        if (nodes.typeId() != QMetaType::QVariantList || nodes.toList().size() > 256)
        {
            throw std::invalid_argument("Lineflow nodes require a list of at most 256 nodes");
        }
        QSet<QString> names;
        for (const auto& value : nodes.toList())
        {
            if (value.typeId() != QMetaType::QVariantMap)
            {
                throw std::invalid_argument("Invalid Lineflow node");
            }
            const auto node = value.toMap();
            const auto name = node.value("name").toString();
            if (node.value("name").typeId() != QMetaType::QString || name.isEmpty() || name.size() > 256
                || name != name.trimmed() || name.contains(QChar::Null) || name.contains('/') || names.contains(name))
            {
                throw std::invalid_argument("Lineflow node names must be unique and contain no slash or NUL");
            }
            names.insert(name);
            if (node.contains("type") == node.contains("reference"))
            {
                throw std::invalid_argument("Lineflow node needs exactly one type or reference");
            }
            const auto target = node.value(node.contains("type") ? "type" : "reference");
            if (target.typeId() != QMetaType::QString || !validId(target.toString()))
            {
                throw std::invalid_argument("Invalid Lineflow target ID");
            }
            if (node.contains("configuration")
                && (node.contains("reference") || node.value("configuration").typeId() != QMetaType::QVariantMap))
            {
                throw std::invalid_argument("Invalid Lineflow node configuration");
            }
            entry->type.references.append(owned(target.toString()));
        }
    }
    entry->type.references.removeDuplicates();
    return entry;
}

std::optional<QList<ProcessorRegistry::Definition>> ProcessorRegistry::parseDefinitions(const QVariantMap& definitions,
                                                                                        QString*           error)
{
    if (error)
    {
        error->clear();
    }
    try
    {
        if (definitions.size() > 1024)
        {
            throw std::invalid_argument("At most 1024 processor definitions are supported");
        }
        // JSON bounds also reject opaque pointer values and deeply nested custom payloads.
        qsizetype                                 values  = 0;
        std::function<bool(const QVariant&, int)> bounded = [&](const QVariant& value, int depth) {
            if (++values > 65536 || depth > 64)
            {
                return false;
            }
            const auto type = value.typeId();
            if (type == QMetaType::QVariantMap)
            {
                for (const auto& child : value.toMap())
                {
                    if (!bounded(child, depth + 1))
                    {
                        return false;
                    }
                }
                return true;
            }
            if (type == QMetaType::QVariantList || type == QMetaType::QStringList)
            {
                for (const auto& child : value.toList())
                {
                    if (!bounded(child, depth + 1))
                    {
                        return false;
                    }
                }
                return true;
            }
            return !value.isValid() || type == QMetaType::QString || type == QMetaType::Bool || type == QMetaType::Int
                || type == QMetaType::UInt || type == QMetaType::LongLong || type == QMetaType::ULongLong
                || type == QMetaType::Double;
        };
        if (!bounded(definitions, 0)
            || QJsonDocument::fromVariant(definitions).toJson(QJsonDocument::Compact).size() > 4 * 1024 * 1024)
        {
            throw std::invalid_argument("Processor definitions exceed bounds or contain unsupported values");
        }
        QList<Definition>                     result;
        ProcessorRegistry                     validation;
        QMap<QString, std::shared_ptr<Entry>> entries;
        for (auto it = definitions.cbegin(); it != definitions.cend(); ++it)
        {
            if (it.value().typeId() != QMetaType::QVariantMap)
            {
                throw std::invalid_argument("Processor definition must be a mapping");
            }
            const auto map = it.value().toMap();
            if (map.value("id").typeId() != QMetaType::QString || map.value("id").toString() != it.key()
                || map.value("name").typeId() != QMetaType::QString || map.value("kind").typeId() != QMetaType::QString
                || map.value("source").typeId() != QMetaType::QVariantMap)
            {
                throw std::invalid_argument("Processor definition requires matching ID, name, kind and source mapping");
            }
            for (const auto& key : {"defaults", "editor"})
            {
                if (map.contains(key) && map.value(key).typeId() != QMetaType::QVariantMap)
                {
                    throw std::invalid_argument("Processor metadata must be a mapping");
                }
            }
            if (map.contains("settingsSchema") && map.value("settingsSchema").typeId() != QMetaType::QString)
            {
                throw std::invalid_argument("Processor schema must be YAML text");
            }
            if (map.contains("group") && map.value("group").typeId() != QMetaType::QString)
            {
                throw std::invalid_argument("Processor group must be a string");
            }
            Definition definition;
            definition.id             = it.key();
            definition.title          = map.value("name").toString();
            definition.kind           = map.value("kind").toString();
            definition.group          = map.value("group").toString();
            definition.source         = map.value("source").toMap();
            definition.defaults       = map.value("defaults").toMap();
            definition.editor         = map.value("editor").toMap();
            definition.settingsSchema = map.value("settingsSchema").toString().toUtf8();
            if (map.contains("references"))
            {
                if (map.value("references").typeId() != QMetaType::QVariantList
                    && map.value("references").typeId() != QMetaType::QStringList)
                {
                    throw std::invalid_argument("Processor references must be a list");
                }
                for (const auto& reference : map.value("references").toList())
                {
                    if (reference.typeId() != QMetaType::QString)
                    {
                        throw std::invalid_argument("Processor reference must be an ID string");
                    }
                    definition.references.append(reference.toString());
                }
            }
            entries.insert(definition.id, _definitionEntry("validation", definition));
            result.append(std::move(definition));
        }
        if (!validation._validateGraph(entries, error))
        {
            return std::nullopt;
        }
        return result;
    }
    catch (const std::exception& exception)
    {
        fail(error, QString::fromUtf8(exception.what()));
        return std::nullopt;
    }
}

bool ProcessorRegistry::_validateGraph(const QMap<QString, std::shared_ptr<Entry>>& entries, QString* error) const
{
    QSet<QString>                      visiting;
    QMap<QString, int>                 heights;
    qsizetype                          nodes  = 0;
    std::function<int(const QString&)> height = [&](const QString& id) -> int {
        if (visiting.contains(id))
        {
            fail(error, "Processor reference cycle at: " + id);
            return -1;
        }
        if (!entries.contains(id))
        {
            return 0;
        }
        if (heights.contains(id))
        {
            return heights.value(id);
        }
        if (++nodes > 16384)
        {
            fail(error, "Processor reference graph exceeds bounds");
            return -1;
        }
        if (visiting.size() >= 16)
        {
            fail(error, "Processor reference depth exceeds 16");
            return -1;
        }
        visiting.insert(id);
        int result = 1;
        for (const auto& reference : entries.value(id)->type.references)
        {
            const int child = height(reference);
            if (child < 0)
            {
                return -1;
            }
            result = std::max(result, child + 1);
        }
        visiting.remove(id);
        if (result > 16)
        {
            fail(error, "Processor reference depth exceeds 16");
            return -1;
        }
        heights.insert(id, result);
        return result;
    };
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
    {
        if (height(it.key()) < 0)
        {
            return false;
        }
    }
    return true;
}

bool ProcessorRegistry::validatePublication(const QString&           owner,
                                            const QList<Definition>& definitions,
                                            QString*                 error) const
{
    if (error)
    {
        error->clear();
    }
    try
    {
        if (owner.isEmpty() || owner.contains(QChar::Null) || definitions.size() > 1024)
        {
            throw std::invalid_argument("Invalid processor definition owner or count");
        }
        QVariantMap serialized;
        for (const auto& definition : definitions)
        {
            if (serialized.contains(definition.id))
            {
                throw std::invalid_argument("Duplicate processor definition ID");
            }
            QVariantList references;
            for (const auto& reference : definition.references)
            {
                references.append(reference);
            }
            serialized.insert(definition.id,
                              QVariantMap {
                                  {            "id",                                definition.id},
                                  {          "name",                             definition.title},
                                  {          "kind",                              definition.kind},
                                  {         "group",                             definition.group},
                                  {        "source",                            definition.source},
                                  {      "defaults",                          definition.defaults},
                                  {        "editor",                            definition.editor},
                                  {"settingsSchema", QString::fromUtf8(definition.settingsSchema)},
                                  {    "references",                                   references}
            });
        }
        if (!parseDefinitions(serialized, error))
        {
            return false;
        }
        auto entries = _entries;
        for (auto it = entries.begin(); it != entries.end();)
        {
            if (it.value()->saved && it.value()->owner == owner)
            {
                it = entries.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (const auto& definition : definitions)
        {
            if (entries.contains(definition.id))
            {
                throw std::invalid_argument(("Duplicate processor ID: " + definition.id).toStdString());
            }
            auto       entry    = _definitionEntry(owner, definition);
            const auto provider = _providers.value(definition.kind);
            if (provider && provider->provider.validate)
            {
                ++provider->instances;
                const auto release = qScopeGuard([provider] { --provider->instances; });
                const bool valid   = provider->provider.validate(entry->type.definition, error);
                if (error)
                {
                    *error = owned(*error);
                }
                if (!valid)
                {
                    return false;
                }
            }
            entries.insert(definition.id, std::move(entry));
        }
        if (!_validateGraph(entries, error))
        {
            return false;
        }
        for (const auto& entry : entries)
        {
            if (!entry->saved || entry->type.kind != "lineflow")
            {
                continue;
            }
            for (const auto& value : entry->type.definition.value("nodes").toList())
            {
                const auto node   = value.toMap();
                const auto target = entries.value(node.value(node.contains("type") ? "type" : "reference").toString());
                if (!target)
                {
                    continue;
                }
                const auto effective = merged(target->defaults, node.value("configuration").toMap());
                const auto errors    = target->schema->validate(effective);
                if (!errors.isEmpty())
                {
                    return fail(error,
                                "Invalid Lineflow node configuration: " + entry->type.id + '/'
                                    + node.value("name").toString() + ": " + errors.first().message);
                }
            }
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        return fail(error, QString::fromUtf8(exception.what()));
    }
    catch (...)
    {
        return fail(error, "Processor publication validation failed with an unknown exception");
    }
}

bool ProcessorRegistry::publishDefinitions(const QString& owner, const QList<Definition>& definitions, QString* error)
{
    if (!validatePublication(owner, definitions, error))
    {
        return false;
    }
    // Construct everything before replacing the observable catalog.
    QMap<QString, std::shared_ptr<Entry>> replacements;
    try
    {
        for (const auto& definition : definitions)
        {
            replacements.insert(definition.id, _definitionEntry(owner, definition));
        }
    }
    catch (const std::exception& exception)
    {
        return fail(error, QString::fromUtf8(exception.what()));
    }
    for (auto it = _entries.begin(); it != _entries.end();)
    {
        if (it.value()->saved && it.value()->owner == owner)
        {
            _retired.append(it.value());
            it = _entries.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (auto it = replacements.begin(); it != replacements.end(); ++it)
    {
        _entries.insert(it.key(), it.value());
    }
    _retired.removeIf([](const auto& entry) { return entry->instances == 0; });
    emit changed();
    return true;
}

void ProcessorRegistry::registerType(const QString& owner, Type type)
{
    if (!type.create || type.settingsSchema.isEmpty() || _entries.contains(type.id))
    {
        throw std::invalid_argument("Processor registration requires a unique ID and factory");
    }
    Definition definition {type.id,
                           type.title,
                           QStringLiteral("native"),
                           {},
                           type.settingsSchema,
                           type.defaults,
                           type.editor,
                           type.references,
                           type.group};
    auto       entry = _definitionEntry(owner, definition);
    if (owner.isEmpty() || owner.contains(QChar::Null))
    {
        throw std::invalid_argument("Invalid processor owner");
    }
    entry->saved       = false;
    entry->type.create = std::move(type.create);
    auto entries       = _entries;
    entries.insert(entry->type.id, entry);
    QString error;
    if (!_validateGraph(entries, &error))
    {
        throw std::invalid_argument(error.toStdString());
    }
    _entries = std::move(entries);
    emit changed();
}

bool ProcessorRegistry::unregisterType(const QString& owner, const QString& id, QString* error)
{
    if (error)
    {
        error->clear();
    }
    const auto entry = _entries.value(id);
    if (!entry || entry->owner != owner || entry->saved)
    {
        return fail(error, "Processor type is not registered to this owner");
    }
    if (entry->instances)
    {
        return fail(error, "Processor type still has prepared or live instances: " + id);
    }
    _entries.remove(id);
    emit changed();
    return true;
}

void ProcessorRegistry::registerProvider(const QString& owner, const QString& id, Provider provider)
{
    if (owner.isEmpty() || owner.contains(QChar::Null) || !validId(id) || id == "native" || id == "lineflow"
        || !provider.prepare || _providers.contains(id))
    {
        throw std::invalid_argument("Processor provider requires valid owner, unique kind and preparation callback");
    }
    auto entry      = std::make_shared<ProviderEntry>();
    entry->owner    = owned(owner);
    entry->provider = std::move(provider);
    _providers.insert(owned(id), std::move(entry));
    emit changed();
}

bool ProcessorRegistry::unregisterProvider(const QString& owner, const QString& id, QString* error)
{
    if (error)
    {
        error->clear();
    }
    const auto entry = _providers.value(id);
    if (!entry || entry->owner != owner)
    {
        return fail(error, "Processor provider is not registered to this owner");
    }
    _retiredProviders.append(entry);
    _providers.remove(id);
    _retiredProviders.removeIf([](const auto& retired) { return retired->instances == 0; });
    emit changed();
    return true;
}

bool ProcessorRegistry::canRemoveOwner(const QString& owner, QString* error) const
{
    if (error)
    {
        error->clear();
    }
    auto busy = [&](const auto& entries) {
        for (const auto& entry : entries)
        {
            if (entry->owner == owner && entry->instances)
            {
                return true;
            }
        }
        return false;
    };
    if (busy(_entries) || busy(_retired) || busy(_providers) || busy(_retiredProviders))
    {
        return fail(error, "Processor owner still has prepared or live instances: " + owner);
    }
    return true;
}

bool ProcessorRegistry::removeOwner(const QString& owner, QString* error)
{
    if (!canRemoveOwner(owner, error))
    {
        return false;
    }
    for (auto it = _entries.begin(); it != _entries.end();)
    {
        if (it.value()->owner == owner)
        {
            it = _entries.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (auto it = _providers.begin(); it != _providers.end();)
    {
        if (it.value()->owner == owner)
        {
            it = _providers.erase(it);
        }
        else
        {
            ++it;
        }
    }
    _retired.removeIf([&](const auto& entry) { return entry->owner == owner || entry->instances == 0; });
    _retiredProviders.removeIf([&](const auto& entry) { return entry->owner == owner || entry->instances == 0; });
    emit changed();
    return true;
}

bool ProcessorRegistry::_available(const QString& id, QSet<QString>& visiting, QString* error) const
{
    QMap<QString, QString>                 results;
    qsizetype                              visits = 0;
    std::function<QString(const QString&)> check  = [&](const QString& key) -> QString {
        if (results.contains(key))
        {
            return results.value(key);
        }
        if (++visits > 16384 || visiting.size() >= 16)
        {
            return QStringLiteral("Processor availability graph exceeds bounds");
        }
        const auto entry = _entries.value(key);
        if (!entry)
        {
            return "Unavailable processor: " + key;
        }
        if (visiting.contains(key))
        {
            return "Processor reference cycle: " + key;
        }
        QString problem;
        if (entry->saved && entry->type.kind != "lineflow")
        {
            const auto provider = _providers.value(entry->type.kind);
            if (!provider)
            {
                problem = "Unavailable processor provider: " + entry->type.kind;
            }
            else if (provider->provider.validate)
            {
                ++provider->instances;
                const auto release = qScopeGuard([provider] { --provider->instances; });
                try
                {
                    const bool valid = provider->provider.validate(entry->type.definition, &problem);
                    problem          = valid ? QString {} : owned(problem);
                    if (!valid && problem.isEmpty())
                    {
                        problem = "Invalid processor source: " + key;
                    }
                }
                catch (const std::exception& exception)
                {
                    problem = "Processor source validation failed: " + QString::fromUtf8(exception.what());
                }
                catch (...)
                {
                    problem = "Processor source validation failed with an unknown exception";
                }
            }
        }
        if (problem.isEmpty() && entry->saved && entry->type.kind == "lineflow")
        {
            for (const auto& value : entry->type.definition.value("nodes").toList())
            {
                const auto node   = value.toMap();
                const auto target = _entries.value(node.value(node.contains("type") ? "type" : "reference").toString());
                if (!target)
                {
                    continue;
                }
                const auto effective = merged(target->defaults, node.value("configuration").toMap());
                const auto errors    = target->schema->validate(effective);
                if (!errors.isEmpty())
                {
                    problem = "Invalid Lineflow node configuration: " + node.value("name").toString() + ": "
                            + errors.first().message;
                    break;
                }
            }
        }
        if (problem.isEmpty())
        {
            visiting.insert(key);
            for (const auto& reference : entry->type.references)
            {
                problem = check(reference);
                if (!problem.isEmpty())
                {
                    break;
                }
            }
            visiting.remove(key);
        }
        results.insert(key, problem);
        return problem;
    };
    const auto problem = check(id);
    if (!problem.isEmpty())
    {
        return fail(error, problem);
    }
    return true;
}

QList<ProcessorRegistry::Type> ProcessorRegistry::types() const
{
    QList<Type> result;
    for (auto it = _entries.cbegin(); it != _entries.cend(); ++it)
    {
        result.append(*type(it.key()));
    }
    return result;
}

std::optional<ProcessorRegistry::Type> ProcessorRegistry::type(const QString& id) const
{
    const auto entry = _entries.value(id);
    if (!entry)
    {
        return std::nullopt;
    }
    auto          result = metadata(entry->type);
    QSet<QString> visiting;
    result.available = _available(id, visiting, &result.unavailableReason);
    return result;
}

std::optional<QVariantMap> ProcessorRegistry::resolveConfiguration(const QString&     id,
                                                                   const QVariantMap& stored,
                                                                   const QVariantMap& overrides,
                                                                   QString*           error) const
{
    if (error)
    {
        error->clear();
    }
    const auto entry = _entries.value(id);
    if (!entry)
    {
        fail(error, "Unknown processor type: " + id);
        return std::nullopt;
    }
    try
    {
        auto       effective = merged(merged(entry->defaults, stored), overrides);
        const auto errors    = entry->schema->validate(effective);
        if (!errors.isEmpty())
        {
            fail(error, "Invalid processor configuration at " + errors.first().path + ": " + errors.first().message);
            return std::nullopt;
        }
        return effective;
    }
    catch (const std::exception& exception)
    {
        fail(error, QString::fromUtf8(exception.what()));
        return std::nullopt;
    }
}

bool ProcessorRegistry::validateConfiguration(const QString& id, const QVariantMap& configuration, QString* error) const
{
    return resolveConfiguration(id, configuration, {}, error).has_value();
}

std::optional<QList<ProcessorRegistry::PropertyNode>> ProcessorRegistry::properties(
    const QString&     id,
    const QVariantMap& storedConfiguration,
    const QVariantMap& overrides,
    QString*           error) const
{
    if (error)
    {
        error->clear();
    }
    try
    {
        qsizetype                                 values  = 0;
        std::function<bool(const QVariant&, int)> bounded = [&](const QVariant& value, int depth) {
            if (++values > 65536 || depth > 64)
            {
                return false;
            }
            if (value.typeId() == QMetaType::QVariantMap)
            {
                for (const auto& child : value.toMap())
                {
                    if (!bounded(child, depth + 1))
                    {
                        return false;
                    }
                }
            }
            else if (value.typeId() == QMetaType::QVariantList || value.typeId() == QMetaType::QStringList)
            {
                for (const auto& child : value.toList())
                {
                    if (!bounded(child, depth + 1))
                    {
                        return false;
                    }
                }
            }
            else
            {
                const auto type = value.typeId();
                return !value.isValid() || type == QMetaType::QString || type == QMetaType::QByteArray
                    || type == QMetaType::Bool || type == QMetaType::Int || type == QMetaType::UInt
                    || type == QMetaType::LongLong || type == QMetaType::ULongLong || type == QMetaType::Double;
            }
            return true;
        };
        if (!bounded(storedConfiguration, 0) || !bounded(overrides, 0)
            || QJsonDocument::fromVariant(storedConfiguration).toJson(QJsonDocument::Compact).size()
                       + QJsonDocument::fromVariant(overrides).toJson(QJsonDocument::Compact).size()
                   > 4 * 1024 * 1024)
        {
            fail(error, "Processor property configuration exceeds bounds");
            return std::nullopt;
        }
        struct Tree
        {
            std::shared_ptr<Entry> entry;
            QString                name;
            QVariantMap            base;
            QStringList            paths;
            std::vector<Tree>      children;
            bool                   composite = false;
        };
        qsizetype                                                                   expandedBytes = 0;
        QSet<QString>                                                               visiting;
        int                                                                         count = 0;
        std::function<std::optional<Tree>(const QString&, const QVariantMap&, int)> expand;
        expand = [&](const QString& target, const QVariantMap& stored, int depth) -> std::optional<Tree> {
            if (++count > 256 || depth > 16)
            {
                fail(error, "Processor property expansion exceeds 256 nodes or depth 16");
                return std::nullopt;
            }
            if (visiting.contains(target))
            {
                fail(error, "Processor property reference cycle: " + target);
                return std::nullopt;
            }
            const auto entry = _entries.value(target);
            if (!entry)
            {
                fail(error, "Unavailable processor property type: " + target);
                return std::nullopt;
            }
            const auto base = resolveConfiguration(target, stored, {}, error);
            if (!base)
            {
                return std::nullopt;
            }
            expandedBytes += QJsonDocument::fromVariant(*base).toJson(QJsonDocument::Compact).size();
            if (expandedBytes > 4 * 1024 * 1024)
            {
                fail(error, "Processor expanded property configuration exceeds 4 MiB");
                return std::nullopt;
            }
            Tree tree;
            tree.entry     = entry;
            tree.base      = *base;
            tree.composite = entry->saved && entry->type.kind == "lineflow";
            if (!tree.composite)
            {
                return tree;
            }
            visiting.insert(target);
            const auto leave = qScopeGuard([&] { visiting.remove(target); });
            for (const auto& value : entry->type.definition.value("nodes").toList())
            {
                const auto node    = value.toMap();
                const auto childId = node.value(node.contains("type") ? "type" : "reference").toString();
                auto       child   = expand(childId, node.value("configuration").toMap(), depth + 1);
                if (!child)
                {
                    return std::nullopt;
                }
                child->name = node.value("name").toString();
                if (child->composite)
                {
                    for (const auto& path : child->paths)
                    {
                        tree.paths.append(child->name + '/' + path);
                    }
                }
                else
                {
                    tree.paths.append(child->name);
                }
                tree.children.push_back(std::move(*child));
            }
            return tree;
        };
        const auto tree = expand(id, storedConfiguration, 1);
        if (!tree)
        {
            return std::nullopt;
        }
        QList<PropertyNode>                                                                      result;
        qsizetype                                                                                bytes = 0;
        std::function<bool(const Tree&, const QString&, const QVariantMap&, const QVariantMap&)> collect;
        collect = [&](const Tree&        node,
                      const QString&     path,
                      const QVariantMap& inheritedOverrides,
                      const QVariantMap& effectiveOverrides) {
            if (!node.composite)
            {
                const auto inherited = merged(node.base, inheritedOverrides);
                const auto effective = merged(node.base, effectiveOverrides);
                for (const auto& configuration : {inherited, effective})
                {
                    const auto errors = node.entry->schema->validate(configuration);
                    if (!errors.isEmpty())
                    {
                        return fail(error,
                                    "Invalid processor property configuration at " + path + ": "
                                        + errors.first().message);
                    }
                }
                bytes += node.entry->type.settingsSchema.size()
                       + QJsonDocument::fromVariant(inherited).toJson(QJsonDocument::Compact).size()
                       + QJsonDocument::fromVariant(effective).toJson(QJsonDocument::Compact).size()
                       + path.size() * qsizetype(sizeof(QChar));
                if (bytes > 4 * 1024 * 1024)
                {
                    return fail(error, "Processor property metadata exceeds 4 MiB");
                }
                result.append(
                    {owned(path),
                     owned(node.entry->type.id),
                     owned(node.entry->type.title),
                     QByteArray(node.entry->type.settingsSchema.constData(), node.entry->type.settingsSchema.size()),
                     inherited,
                     effective});
                return true;
            }
            const auto defaults  = node.base.value("overrides").toMap();
            const auto inherited = merged(defaults, inheritedOverrides);
            const auto effective = merged(defaults, effectiveOverrides);
            for (const auto& values : {inherited, effective})
            {
                for (auto it = values.cbegin(); it != values.cend(); ++it)
                {
                    if (!node.paths.contains(it.key()) || it.value().typeId() != QMetaType::QVariantMap)
                    {
                        return fail(error, "Unknown processor occurrence or invalid override map: " + it.key());
                    }
                }
                auto configuration = node.base;
                configuration.insert("overrides", values);
                const auto errors = node.entry->schema->validate(configuration);
                if (!errors.isEmpty())
                {
                    return fail(error, "Invalid composite processor property configuration: " + errors.first().message);
                }
            }
            const auto childOverrides = [](const Tree& child, const QVariantMap& values) {
                if (!child.composite)
                {
                    return values.value(child.name).toMap();
                }
                QVariantMap result;
                const auto  prefix = child.name + '/';
                for (auto it = values.cbegin(); it != values.cend(); ++it)
                {
                    if (it.key().startsWith(prefix))
                    {
                        result.insert(it.key().mid(prefix.size()), it.value());
                    }
                }
                return result;
            };
            for (const auto& child : node.children)
            {
                const auto childPath = path.isEmpty() ? child.name : path + '/' + child.name;
                if (!collect(child, childPath, childOverrides(child, inherited), childOverrides(child, effective)))
                {
                    return false;
                }
            }
            return true;
        };
        if (!collect(*tree, {}, {}, overrides))
        {
            return std::nullopt;
        }
        return result;
    }
    catch (const std::exception& exception)
    {
        fail(error, "Processor property expansion failed: " + QString::fromUtf8(exception.what()));
        return std::nullopt;
    }
}

QVariantMap ProcessorRegistry::Prepared::configuration() const
{
    return _configuration;
}
std::shared_ptr<Processor> ProcessorRegistry::Prepared::instantiate(const QVariantMap& overrides, QString* error) const
{
    if (error)
    {
        error->clear();
    }
    try
    {
        return _instantiate(overrides, error);
    }
    catch (const std::exception& exception)
    {
        fail(error, QString::fromUtf8(exception.what()));
        return {};
    }
    catch (...)
    {
        fail(error, "Processor instantiation failed with an unknown exception");
        return {};
    }
}

std::shared_ptr<Processor> ProcessorRegistry::_create(const Type::Factory&        factory,
                                                      const QVariantMap&          configuration,
                                                      const Processor::Limits&    limits,
                                                      std::shared_ptr<const void> lease,
                                                      QString*                    error)
{
    struct Hold
    {
        std::shared_ptr<const void> lease;
        Type::Factory               factory;
    };
    auto                       hold = std::make_shared<Hold>(Hold {std::move(lease), factory});
    std::unique_ptr<Processor> instance;
    try
    {
        instance = hold->factory(configuration, limits, error);
    }
    catch (const std::exception& exception)
    {
        fail(error, "Processor creation failed: " + QString::fromUtf8(exception.what()));
        return {};
    }
    catch (...)
    {
        fail(error, "Processor creation failed with an unknown exception");
        return {};
    }
    if (error)
    {
        *error = owned(*error);
    }
    if (!instance)
    {
        if (!error || error->isEmpty())
        {
            fail(error, "Processor factory returned no instance");
        }
        return {};
    }
    if (instance->parent() || instance->thread() != QThread::currentThread())
    {
        fail(error, "Processor factory must return an unparented instance on the calling thread");
        return {};
    }
    return std::shared_ptr<Processor>(instance.release(), [hold](Processor* processor) {
        // An explicit sentinel pins code even if a broken implementation discards
        // the acknowledgement callback or throws before retaining it.
        auto pin          = new std::shared_ptr<Hold>(hold);
        auto acknowledged = std::make_shared<bool>(false);
        try
        {
            processor->cancel([pin, processor, acknowledged] {
                if (*acknowledged)
                {
                    return;
                }
                *acknowledged = true;
                QTimer::singleShot(0, processor, [pin, processor] {
                    delete processor;
                    delete pin;
                });
            });
        }
        catch (...)
        {
            qCritical() << "Processor cancellation threw; instance remains pinned";
        }
    });
}

std::shared_ptr<const ProcessorRegistry::Prepared> ProcessorRegistry::prepare(const QString&           id,
                                                                              const QVariantMap&       configuration,
                                                                              const Processor::Limits& limits,
                                                                              QString*                 error)
{
    if (error)
    {
        error->clear();
    }
    if (limits.maxBytes <= 0 || limits.maxChunks <= 0 || limits.maxDepth <= 0 || limits.maxInstances <= 0
        || limits.eventBudget <= 0)
    {
        fail(error, "Processor limits must be positive");
        return {};
    }
    QSet<QString> visiting;
    int           instances = 0;
    try
    {
        return _prepare(id, configuration, limits, visiting, 1, instances, error);
    }
    catch (const std::exception& exception)
    {
        fail(error, "Processor preparation failed: " + QString::fromUtf8(exception.what()));
        return {};
    }
    catch (...)
    {
        fail(error, "Processor preparation failed with an unknown exception");
        return {};
    }
}

std::shared_ptr<const ProcessorRegistry::Prepared> ProcessorRegistry::_prepare(const QString&           id,
                                                                               const QVariantMap&       configuration,
                                                                               const Processor::Limits& limits,
                                                                               QSet<QString>&           visiting,
                                                                               int                      depth,
                                                                               int&                     instances,
                                                                               QString*                 error)
{
    if (depth > limits.maxDepth || ++instances > limits.maxInstances)
    {
        fail(error, "Processor expansion limits exceeded");
        return {};
    }
    if (visiting.contains(id))
    {
        fail(error, "Processor reference cycle: " + id);
        return {};
    }
    const auto entry = _entries.value(id);
    if (!entry)
    {
        fail(error, "Unavailable processor: " + id);
        return {};
    }
    const auto effective = resolveConfiguration(id, configuration, {}, error);
    if (!effective)
    {
        return {};
    }
    ++entry->instances;
    auto lease             = std::make_shared<Lease>();
    lease->release         = [entry] { --entry->instances; };
    auto result            = std::make_shared<Prepared>();
    result->_configuration = *effective;
    visiting.insert(id);
    const auto leave = qScopeGuard([&] { visiting.remove(id); });
    if (entry->saved && entry->type.kind == "lineflow")
    {
        struct Child
        {
            QString                         name;
            std::shared_ptr<const Prepared> prepared;
        };
        std::vector<Child> children;
        for (const auto& value : entry->type.definition.value("nodes").toList())
        {
            const auto node   = value.toMap();
            const auto target = node.value(node.contains("type") ? "type" : "reference").toString();
            auto       child  = _prepare(target,
                                         node.value("configuration").toMap(),
                                         limits,
                                         visiting,
                                         depth + 1,
                                         instances,
                                         error);
            if (!child)
            {
                return {};
            }
            const auto name = node.value("name").toString();
            if (child->_composite)
            {
                for (const auto& path : child->_paths)
                {
                    result->_paths.append(name + '/' + path);
                }
            }
            else
            {
                result->_paths.append(name);
            }
            children.push_back({name, std::move(child)});
        }
        result->_composite   = true;
        const auto paths     = result->_paths;
        const auto defaults  = effective->value("overrides").toMap();
        const auto snapshot  = result->_configuration;
        result->_instantiate = [lease,
                                entry,
                                children = std::move(children),
                                paths,
                                defaults,
                                snapshot,
                                limits](const QVariantMap& overrides, QString* error) -> std::shared_ptr<Processor> {
            auto values = merged(defaults, overrides);
            for (auto it = values.cbegin(); it != values.cend(); ++it)
            {
                if (!paths.contains(it.key()) || it.value().typeId() != QMetaType::QVariantMap)
                {
                    fail(error, "Unknown processor occurrence or invalid override map: " + it.key());
                    return {};
                }
            }
            auto finalConfiguration = snapshot;
            finalConfiguration.insert("overrides", values);
            const auto errors = entry->schema->validate(finalConfiguration);
            if (!errors.isEmpty())
            {
                fail(error, "Invalid composite processor configuration: " + errors.first().message);
                return {};
            }
            std::vector<std::shared_ptr<Processor>> nodes;
            for (const auto& child : children)
            {
                QVariantMap childOverrides;
                if (child.prepared->_composite)
                {
                    for (auto it = values.cbegin(); it != values.cend(); ++it)
                    {
                        if (it.key().startsWith(child.name + '/'))
                        {
                            childOverrides.insert(it.key().mid(child.name.size() + 1), it.value());
                        }
                    }
                }
                else
                {
                    childOverrides = values.value(child.name).toMap();
                }
                auto instance = child.prepared->instantiate(childOverrides, error);
                if (!instance)
                {
                    return {};
                }
                nodes.push_back(std::move(instance));
            }
            auto configuration = snapshot;
            configuration.insert("overrides", values);
            QStringList nodeNames;
            for (const auto& child : children)
            {
                nodeNames.append(child.name);
            }
            Type::Factory factory = [nodes     = std::move(nodes),
                                     nodeNames = std::move(nodeNames)](const QVariantMap&       config,
                                                                       const Processor::Limits& limits,
                                                                       QString*) mutable -> std::unique_ptr<Processor> {
                return Lineflow::compose(std::move(nodes), config, limits, std::move(nodeNames));
            };
            return _create(factory, configuration, limits, lease, error);
        };
        return result;
    }
    Type::Factory                                factory = entry->type.create;
    std::vector<std::shared_ptr<const Prepared>> referencePins;
    std::shared_ptr<ProviderEntry>               provider;
    std::shared_ptr<Lease>                       providerLease;
    if (entry->saved)
    {
        provider = _providers.value(entry->type.kind);
        if (!provider)
        {
            fail(error, "Unavailable processor provider: " + entry->type.kind);
            return {};
        }
        ++provider->instances;
        providerLease          = std::make_shared<Lease>();
        providerLease->release = [provider] { --provider->instances; };
        Provider::References references;
        for (const auto& reference : entry->type.references)
        {
            auto child = _prepare(reference, {}, limits, visiting, depth + 1, instances, error);
            if (!child)
            {
                return {};
            }
            references.insert(reference,
                              [child](const QVariantMap& overrides,
                                      const Processor::Limits&,
                                      QString* error) -> std::unique_ptr<Processor> {
                                  auto instance = child->instantiate(overrides, error);
                                  return instance ? std::make_unique<ForwardProcessor>(std::move(instance)) : nullptr;
                              });
        }
        if (provider->provider.validate)
        {
            const bool valid = provider->provider.validate(entry->type.definition, error);
            if (error)
            {
                *error = owned(*error);
            }
            if (!valid)
            {
                return {};
            }
        }
        factory = provider->provider.prepare(entry->type.definition, *effective, references, limits, error);
        if (error)
        {
            *error = owned(*error);
        }
        if (!factory)
        {
            if (!error || error->isEmpty())
            {
                fail(error, "Processor provider returned no prepared factory");
            }
            return {};
        }
        // Retain every referenced preparation through the returned factory lifetime.
        auto preparedFactory = std::move(factory);
        factory = [references, preparedFactory = std::move(preparedFactory)](const QVariantMap&       config,
                                                                             const Processor::Limits& limits,
                                                                             QString*                 error) {
            return preparedFactory(config, limits, error);
        };
    }
    else
    {
        // Native declarations may also depend on catalog entries for cycle and availability checks.
        for (const auto& reference : entry->type.references)
        {
            auto child = _prepare(reference, {}, limits, visiting, depth + 1, instances, error);
            if (!child)
            {
                return {};
            }
            referencePins.push_back(std::move(child));
        }
    }
    result->_instantiate = [lease,
                            providerLease,
                            entry,
                            referencePins,
                            factory   = std::move(factory),
                            effective = *effective,
                            limits](const QVariantMap& overrides, QString* error) -> std::shared_ptr<Processor> {
        auto       configuration = merged(effective, overrides);
        const auto errors        = entry->schema->validate(configuration);
        if (!errors.isEmpty())
        {
            fail(error, "Invalid processor configuration at " + errors.first().path + ": " + errors.first().message);
            return {};
        }
        struct Pins
        {
            std::shared_ptr<Lease>                       entry;
            std::shared_ptr<Lease>                       provider;
            std::vector<std::shared_ptr<const Prepared>> references;
        };
        auto pins = std::make_shared<Pins>(Pins {lease, providerLease, referencePins});
        return _create(factory, configuration, limits, pins, error);
    };
    return result;
}

std::shared_ptr<Processor> ProcessorRegistry::create(const QString&           id,
                                                     const QVariantMap&       configuration,
                                                     const Processor::Limits& limits,
                                                     QString*                 error)
{
    auto prepared = prepare(id, configuration, limits, error);
    return prepared ? prepared->instantiate({}, error) : nullptr;
}
