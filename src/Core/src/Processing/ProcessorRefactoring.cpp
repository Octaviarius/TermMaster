#include <QJsonDocument>
#include <QSet>
#include <QStringList>
#include <Termaster/Core/Processing/ProcessorRefactoring.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <functional>
#include <stdexcept>

namespace
{
void reject(const QString& message)
{
    throw std::runtime_error(message.toStdString());
}

bool validName(const QString& name)
{
    return !name.isEmpty() && name.size() <= 256 && name == name.trimmed() && !name.contains('/')
        && !name.contains(QChar::Null);
}

QVariantList nodes(const QVariantMap& definitions, const QString& id)
{
    return definitions.value(id).toMap().value("source").toMap().value("nodes").toList();
}

QString target(const QVariantMap& node)
{
    return node.value(node.contains("type") ? "type" : "reference").toString();
}

void checkObjects(const QVariantMap& objects)
{
    qsizetype                                 count = 0;
    std::function<void(const QVariant&, int)> visit = [&](const QVariant& value, int depth) {
        if (++count > 65536 || depth > 64)
        {
            reject("Session object refactoring exceeds structural bounds");
        }
        if (value.typeId() == QMetaType::QVariantMap)
        {
            for (const auto& child : value.toMap())
            {
                visit(child, depth + 1);
            }
        }
        else if (value.typeId() == QMetaType::QVariantList || value.typeId() == QMetaType::QStringList)
        {
            for (const auto& child : value.toList())
            {
                visit(child, depth + 1);
            }
        }
    };
    visit(objects, 0);
    if (QJsonDocument::fromVariant(objects).toJson(QJsonDocument::Compact).size() > 4 * 1024 * 1024)
    {
        reject("Session object refactoring exceeds 4 MiB");
    }
}
} // namespace

std::optional<ProcessorRefactoring::Result> ProcessorRefactoring::transform(const QVariantMap&   originalDefinitions,
                                                                            const QVariantMap&   finalDraftDefinitions,
                                                                            const QList<Rename>& renames,
                                                                            const QVariantMap&   sessionObjects,
                                                                            QString*             error)
{
    if (error)
    {
        error->clear();
    }
    try
    {
        if (!ProcessorRegistry::parseDefinitions(originalDefinitions, error)
            || !ProcessorRegistry::parseDefinitions(finalDraftDefinitions, error))
        {
            return std::nullopt;
        }
        if (renames.size() > 4096)
        {
            reject("At most 4096 node renames are supported");
        }
        checkObjects(sessionObjects);
        // The map retains each original node identity through sequential name changes.
        QMap<QString, QMap<QString, QString>> names;
        for (auto it = originalDefinitions.cbegin(); it != originalDefinitions.cend(); ++it)
        {
            if (it.value().toMap().value("kind").toString() == "lineflow")
            {
                for (const auto& value : nodes(originalDefinitions, it.key()))
                {
                    const auto name = value.toMap().value("name").toString();
                    names[it.key()].insert(name, name);
                }
            }
        }
        for (const auto& rename : renames)
        {
            if (!validName(rename.from) || !validName(rename.to) || rename.from == rename.to)
            {
                reject("Invalid node rename");
            }
            auto& block = names[rename.block];
            auto  found = block.end();
            for (auto it = block.begin(); it != block.end(); ++it)
            {
                if (it.value() == rename.to)
                {
                    reject("Node rename collides with an existing identity");
                }
                if (it.value() == rename.from)
                {
                    found = it;
                }
            }
            if (found == block.end())
            {
                reject("Node rename does not identify an original node");
            }
            found.value() = rename.to;
        }
        for (const auto& rename : renames)
        {
            if (!finalDraftDefinitions.contains(rename.block)
                || finalDraftDefinitions.value(rename.block).toMap().value("kind").toString() != "lineflow")
            {
                reject("Renamed Lineflow definition was removed or replaced");
            }
        }
        for (auto block = names.cbegin(); block != names.cend(); ++block)
        {
            for (auto node = block.value().cbegin(); node != block.value().cend(); ++node)
            {
                if (node.key() == node.value())
                {
                    continue;
                }
                bool present = false;
                for (const auto& value : nodes(finalDraftDefinitions, block.key()))
                {
                    present |= value.toMap().value("name").toString() == node.value();
                }
                if (!present)
                {
                    reject("Final draft does not contain the renamed node: " + node.value());
                }
            }
        }
        const auto rewrite = [&](const QString& root, const QVariant& value) -> QVariantMap {
            if (value.typeId() != QMetaType::QVariantMap)
            {
                reject("Processor occurrence overrides must be a map");
            }
            const auto overrides = value.toMap();
            if (originalDefinitions.value(root).toMap().value("kind").toString() != "lineflow")
            {
                return overrides;
            }
            QVariantMap result;
            for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
            {
                if (it.value().typeId() != QMetaType::QVariantMap)
                {
                    reject("Processor occurrence override must be a map: " + it.key());
                }
                const auto parts = it.key().split('/');
                if (parts.size() > 64 || parts.contains(QString {}))
                {
                    reject("Invalid processor occurrence address: " + it.key());
                }
                QString     block = root;
                QStringList translated;
                int         depth = 0;
                for (int index = 0; index < parts.size(); ++index)
                {
                    if (++depth > 16)
                    {
                        reject("Processor occurrence address exceeds depth bounds");
                    }
                    if (originalDefinitions.value(block).toMap().value("kind").toString() != "lineflow")
                    {
                        translated.append(parts.mid(index));
                        break;
                    }
                    QVariantMap oldNode;
                    for (const auto& node : nodes(originalDefinitions, block))
                    {
                        if (node.toMap().value("name").toString() == parts[index])
                        {
                            oldNode = node.toMap();
                            break;
                        }
                    }
                    if (oldNode.isEmpty())
                    {
                        reject("Stale processor occurrence address: " + it.key());
                    }
                    const auto  renamed = names.value(block).value(parts[index]);
                    QVariantMap newNode;
                    for (const auto& node : nodes(finalDraftDefinitions, block))
                    {
                        if (node.toMap().value("name").toString() == renamed)
                        {
                            newNode = node.toMap();
                            break;
                        }
                    }
                    if (newNode.isEmpty() || target(oldNode) != target(newNode))
                    {
                        reject("Processor occurrence was removed or replaced: " + it.key());
                    }
                    translated.append(renamed);
                    block = target(oldNode);
                }
                const auto address = translated.join('/');
                if (result.contains(address))
                {
                    reject("Processor override address collision: " + address);
                }
                result.insert(address, it.value());
            }
            return result;
        };
        Result result {finalDraftDefinitions, sessionObjects};
        for (auto it = result.definitions.begin(); it != result.definitions.end(); ++it)
        {
            auto definition = it.value().toMap();
            auto defaults   = definition.value("defaults").toMap();
            if (defaults.contains("overrides"))
            {
                defaults.insert("overrides", rewrite(it.key(), defaults.value("overrides")));
                definition.insert("defaults", defaults);
            }
            if (definition.value("kind").toString() == "lineflow")
            {
                auto source   = definition.value("source").toMap();
                auto children = source.value("nodes").toList();
                for (auto& child : children)
                {
                    auto node          = child.toMap();
                    auto configuration = node.value("configuration").toMap();
                    if (configuration.contains("overrides"))
                    {
                        configuration.insert("overrides", rewrite(target(node), configuration.value("overrides")));
                        node.insert("configuration", configuration);
                        child = node;
                    }
                }
                source.insert("nodes", children);
                definition.insert("source", source);
            }
            it.value() = definition;
        }
        for (auto it = result.sessionObjects.begin(); it != result.sessionObjects.end(); ++it)
        {
            if (it.value().typeId() != QMetaType::QVariantMap)
            {
                continue;
            }
            auto object     = it.value().toMap();
            auto settings   = object.value("settings").toMap();
            auto processing = settings.value("processing").toMap();
            bool changed    = false;
            for (const auto& direction : {"rx", "tx"})
            {
                auto selection        = processing.value(direction).toMap();
                bool selectionChanged = false;
                auto configuration    = selection.value("configuration").toMap();
                if (configuration.contains("overrides"))
                {
                    configuration.insert("overrides",
                                         rewrite(selection.value("type").toString(), configuration.value("overrides")));
                    selection.insert("configuration", configuration);
                    selectionChanged = true;
                }
                if (selection.contains("overrides"))
                {
                    selection.insert("overrides",
                                     rewrite(selection.value("type").toString(), selection.value("overrides")));
                    selectionChanged = true;
                }
                if (selectionChanged)
                {
                    processing.insert(direction, selection);
                    changed = true;
                }
            }
            if (changed)
            {
                settings.insert("processing", processing);
                object.insert("settings", settings);
                it.value() = object;
            }
        }
        if (!ProcessorRegistry::parseDefinitions(result.definitions, error))
        {
            return std::nullopt;
        }
        return result;
    }
    catch (const std::exception& exception)
    {
        if (error)
        {
            *error = QString::fromUtf8(exception.what());
        }
        return std::nullopt;
    }
}
