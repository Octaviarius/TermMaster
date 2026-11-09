#include <QDebug>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
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
    return {owned(type.id),
            owned(type.title),
            QByteArray(type.settingsSchema.constData(), type.settingsSchema.size()),
            {}};
}
} // namespace

struct ProcessorRegistry::Entry
{
    QString                         owner;
    Type                            type;
    std::unique_ptr<SettingsSchema> schema;
    QVariantMap                     defaults;
    int                             instances = 0;
};

ProcessorRegistry::ProcessorRegistry()  = default;
ProcessorRegistry::~ProcessorRegistry() = default;

void ProcessorRegistry::registerType(const QString& owner, Type type)
{
    static const QRegularExpression idPattern("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\\z");
    if (owner.isEmpty() || owner.contains(QChar::Null))
    {
        throw std::invalid_argument("Processor registration requires a nonempty owner without NUL");
    }
    if (!idPattern.match(type.id).hasMatch())
    {
        throw std::invalid_argument(
            "Processor type ID must contain 1 to 128 ASCII letters, digits, dots, underscores or hyphens and begin with a letter or digit");
    }
    if (type.title.trimmed().isEmpty() || type.title.size() > 256 || type.title.contains(QChar::Null))
    {
        throw std::invalid_argument("Processor type title must contain 1 to 256 characters and no NUL");
    }
    if (!type.create)
    {
        throw std::invalid_argument("Processor registration requires a factory");
    }
    if (_entries.contains(type.id))
    {
        throw std::invalid_argument(("Processor type is already registered: " + type.id).toStdString());
    }

    auto entry   = std::make_shared<Entry>();
    entry->owner = owned(owner);
    entry->type  = metadata(type);
    try
    {
        entry->schema = std::make_unique<SettingsSchema>(entry->type.settingsSchema);
        for (const auto& field : entry->schema->fields())
        {
            const auto value = schemaDefault(field.definition);
            if (value)
            {
                entry->defaults.insert(owned(field.key), *value);
            }
        }

        const auto errors = entry->schema->validate(entry->defaults);
        if (!errors.isEmpty())
        {
            throw std::invalid_argument(
                ("Unusable default configuration at " + errors.first().path + ": " + errors.first().message)
                    .toStdString());
        }
    }
    catch (const std::exception& exception)
    {
        throw std::invalid_argument(
            ("Invalid processor registration for " + entry->type.id + ": " + QString::fromUtf8(exception.what()))
                .toStdString());
    }

    entry->type.create = std::move(type.create);
    const auto id      = entry->type.id;
    _entries.insert(id, std::move(entry));
}

bool ProcessorRegistry::unregisterType(const QString& owner, const QString& id, QString* error)
{
    if (error)
    {
        error->clear();
    }
    const auto entry = _entries.value(id);
    if (!entry || entry->owner != owner)
    {
        return fail(error, "Processor type is not registered to this owner");
    }
    if (entry->instances)
    {
        return fail(error, "Processor type still has live instances: " + id);
    }
    _entries.remove(id);
    return true;
}

bool ProcessorRegistry::canRemoveOwner(const QString& owner, QString* error) const
{
    if (error)
    {
        error->clear();
    }
    for (const auto& entry : _entries)
    {
        if (entry->owner == owner && entry->instances)
        {
            return fail(error, "Processor owner still has live instances: " + entry->type.id);
        }
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
    return true;
}

QList<ProcessorRegistry::Type> ProcessorRegistry::types() const
{
    QList<Type> result;
    for (const auto& entry : _entries)
    {
        result.append(metadata(entry->type));
    }
    return result;
}

std::optional<ProcessorRegistry::Type> ProcessorRegistry::type(const QString& id) const
{
    const auto entry = _entries.value(id);
    return entry ? std::optional<Type>(metadata(entry->type)) : std::nullopt;
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
        fail(error, "Processor configuration resolution failed: " + QString::fromUtf8(exception.what()));
        return std::nullopt;
    }
}

bool ProcessorRegistry::validateConfiguration(const QString& id, const QVariantMap& configuration, QString* error) const
{
    return resolveConfiguration(id, configuration, {}, error).has_value();
}

std::shared_ptr<Processor> ProcessorRegistry::create(const QString&           id,
                                                     const QVariantMap&       configuration,
                                                     const Processor::Limits& limits,
                                                     QString*                 error)
{
    if (error)
    {
        error->clear();
    }
    const auto errorOwnership = qScopeGuard([error] {
        if (error)
        {
            *error = QString(error->constData(), error->size());
        }
    });
    const auto entry          = _entries.value(id);
    if (!entry)
    {
        fail(error, "Unknown processor type: " + id);
        return {};
    }
    if (limits.maxBytes <= 0 || limits.maxChunks <= 0 || limits.maxDepth <= 0 || limits.maxInstances <= 0
        || limits.eventBudget <= 0)
    {
        fail(error, "Processor limits must be positive");
        return {};
    }
    try
    {
        const auto effective = resolveConfiguration(id, configuration, {}, error);
        if (!effective)
        {
            return {};
        }

        // Count factory execution too: a reentrant factory must not unload its own code.
        ++entry->instances;
        std::unique_ptr<Processor> instance;
        try
        {
            instance = entry->type.create(*effective, limits, error);
        }
        catch (...)
        {
            --entry->instances;
            throw;
        }
        if (!instance)
        {
            --entry->instances;
            if (!error || error->isEmpty())
            {
                fail(error, "Processor factory returned no instance");
            }
            return {};
        }
        if (instance->parent() || instance->thread() != QThread::currentThread())
        {
            instance.reset();
            --entry->instances;
            fail(error, "Processor factory must return an unparented instance on the calling thread");
            return {};
        }
        return std::shared_ptr<Processor>(instance.release(), [entry](Processor* processor) {
            // Last consumer release initiates shutdown, retaining both the instance and
            // its factory/module lease until the processor confirms worker quiescence.
            auto acknowledged = std::make_shared<bool>(false);
            try
            {
                processor->cancel([entry, processor, acknowledged] {
                    if (*acknowledged)
                    {
                        return;
                    }
                    *acknowledged = true;
                    // Return through the plugin's cancel stack before destroying its
                    // object, and retain the lease through the destructor's return.
                    QTimer::singleShot(0, processor, [entry, processor] {
                        delete processor;
                        --entry->instances;
                    });
                });
            }
            catch (...)
            {
                // A broken cancellation contract must pin executing native code.
                qCritical() << "Processor cancellation threw; instance remains pinned:" << entry->type.id;
            }
        });
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
}
