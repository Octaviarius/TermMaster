#include "LineflowEditorModel.h"

#include <QJsonDocument>
#include <QMimeData>
#include <QUuid>
#include <Termaster/Core/Gui/Icons.h>
#include <algorithm>

namespace
{
bool fail(QString* error, const QString& message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}
QVariantList nodes(const QVariantMap& definitions, const QString& block)
{
    return definitions.value(block).toMap().value("source").toMap().value("nodes").toList();
}
void putNodes(QVariantMap& definitions, const QString& block, QVariantList list)
{
    auto definition = definitions.value(block).toMap();
    auto source     = definition.value("source").toMap();
    source.insert("nodes", list);
    definition.insert("source", source);
    definitions.insert(block, definition);
}
} // namespace
LineflowEditorModel::LineflowEditorModel(QVariantMap definitions, ProcessorRegistry& registry, QObject* parent) :
    QStandardItemModel(parent), _baseline(definitions), _draft(std::move(definitions)), _registry(registry),
    _token(QUuid::createUuid().toByteArray())
{
    connect(&_registry, &ProcessorRegistry::changed, this, [this] { _rebuild(); });
    if (!blockIds().isEmpty())
    {
        _selected = blockIds().first();
    }
    markApplied();
    _rebuild();
}
QVariantMap LineflowEditorModel::definitions() const
{
    return _draft;
}
QVariantMap LineflowEditorModel::baseline() const
{
    return _baseline;
}
QList<LineflowEditorModel::Rename> LineflowEditorModel::renames() const
{
    QList<Rename> result;
    for (auto block = _originalNames.cbegin(); block != _originalNames.cend(); ++block)
    {
        QSet<QString> used;
        for (const auto& value : nodes(_baseline, block.key()))
        {
            used.insert(value.toMap().value("name").toString());
        }
        for (const auto& value : nodes(_draft, block.key()))
        {
            used.insert(value.toMap().value("name").toString());
        }
        QList<Rename> final;
        int           suffix = 0;
        for (auto node = block.value().cbegin(); node != block.value().cend(); ++node)
        {
            if (node.key() == node.value())
            {
                continue;
            }
            QString temporary;
            do
            {
                temporary = QString("__termasterRename%1").arg(++suffix);
            } while (used.contains(temporary));
            used.insert(temporary);
            result.append({block.key(), node.value(), temporary});
            final.append({block.key(), temporary, node.key()});
        }
        result.append(final);
    }
    return result;
}
QString LineflowEditorModel::unavailableReason(const QString& id)
{
    return _unavailableReason(id);
}

QStringList LineflowEditorModel::blockIds() const
{
    QStringList result;
    for (auto it = _draft.cbegin(); it != _draft.cend(); ++it)
    {
        if (it.value().toMap().value("kind") == "lineflow")
        {
            result.append(it.key());
        }
    }
    return result;
}
QString LineflowEditorModel::blockName(const QString& id) const
{
    return _draft.value(id).toMap().value("name").toString();
}
int LineflowEditorModel::blockChannels(const QString& id) const
{
    return _draft.value(id).toMap().value("source").toMap().value("channels", 1).toInt();
}

QPair<int, int> LineflowEditorModel::processorShape(const QString& id) const
{
    if (_draft.value(id).toMap().value("kind") == "lineflow")
    {
        const int channels = blockChannels(id);
        return {channels, channels};
    }
    const auto metadata = _registry.type(id);
    return metadata ? QPair<int, int> {metadata->inputCount, metadata->outputCount} : QPair<int, int> {1, 1};
}

QStringList LineflowEditorModel::_portNames(const QString& id, bool input) const
{
    const auto definition = _draft.value(id).toMap();
    const bool local      = definition.value("kind") == "lineflow";
    const auto metadata   = _registry.type(id);
    const auto shape      = processorShape(id);
    const int  count      = input ? shape.first : shape.second;
    auto       names      = local    ? definition.value(input ? "inputNames" : "outputNames").toStringList() :
                            metadata ? (input ? metadata->inputNames : metadata->outputNames) :
                                       QStringList {};
    if (names.size() == count)
    {
        return names;
    }
    if (local)
    {
        return count == 2 ?
                   (input ? QStringList {"Device Rx", "Terminal Tx"} : QStringList {"Terminal Rx", "Device Tx"}) :
                   QStringList {input ? "Input" : "Output"};
    }
    names.clear();
    for (int port = 0; port < count; ++port)
    {
        names.append(QString(input ? "Input%1" : "Output%1").arg(port + 1));
    }
    return names;
}

bool LineflowEditorModel::setBlockChannels(const QString& id, int channels, QString* error)
{
    if (!blockIds().contains(id) || (channels != 1 && channels != 2))
    {
        return fail(error, tr("Select a block and a supported topology."));
    }
    if (blockChannels(id) == channels)
    {
        return true;
    }
    auto draft      = _draft;
    auto definition = draft.value(id).toMap();
    auto source     = definition.value("source").toMap();
    source.insert("channels", channels);
    definition.insert("source", source);
    draft.insert(id, definition);
    return _commit(draft, error);
}

QString LineflowEditorModel::blockGroup(const QString& id) const
{
    return _draft.value(id).toMap().value("group").toString();
}

bool LineflowEditorModel::setBlockGroup(const QString& id, const QString& group, QString* error)
{
    if (!blockIds().contains(id))
    {
        return fail(error, tr("Block no longer exists."));
    }
    if (blockGroup(id) == group)
    {
        return true;
    }
    auto draft      = _draft;
    auto definition = draft.value(id).toMap();
    definition.insert("group", group);
    draft.insert(id, definition);
    return _commit(draft, error);
}

QString LineflowEditorModel::selectedBlock() const
{
    return _selected;
}
void LineflowEditorModel::setSelectedBlock(const QString& id)
{
    if (_selected == id)
    {
        return;
    }
    _selected = blockIds().contains(id) ? id : QString {};
    _rebuild();
    emit draftChanged();
}
int LineflowEditorModel::_nodeIndex(const QString& block, const QString& name) const
{
    const auto list = nodes(_draft, block);
    for (qsizetype index = 0; index < list.size(); ++index)
    {
        if (list[index].toMap().value("name") == name)
        {
            return int(index);
        }
    }
    return -1;
}
QString LineflowEditorModel::_defaultNodeName(const QString& block, const QString& catalogId) const
{
    const auto definition = _draft.value(catalogId).toMap();
    const auto metadata   = _registry.type(catalogId);
    QString    title      = definition.value("kind") == "lineflow" ? definition.value("name").toString() :
                            metadata                               ? metadata->title :
                                                                     QString {};
    title.replace('/', ' ');
    title.remove(QChar::Null);
    title = title.trimmed();
    if (title.isEmpty())
    {
        title = "Block";
    }

    for (int number = 1;; ++number)
    {
        const auto suffix = number == 1 ? QString {} : QString(" %1").arg(number);
        auto       prefix = title.left(256 - suffix.size()).trimmed();
        if (!prefix.isEmpty() && prefix.at(prefix.size() - 1).isHighSurrogate())
        {
            prefix.chop(1);
        }
        const auto name = prefix + suffix;
        if (_nodeIndex(block, name) < 0)
        {
            return name;
        }
    }
}

QVariantMap LineflowEditorModel::node(const QString& block, const QString& name) const
{
    const int index = _nodeIndex(block, name);
    return index < 0 ? QVariantMap {} : nodes(_draft, block)[index].toMap();
}
bool LineflowEditorModel::_commit(QVariantMap draft, QString* error)
{
    auto parsed = ProcessorRegistry::parseDefinitions(draft, error);
    if (!parsed || !_registry.validatePublication("workspace", *parsed, error))
    {
        return false;
    }
    _draft = std::move(draft);
    _rebuild();
    emit draftChanged();
    return true;
}
bool LineflowEditorModel::validate(QString* error) const
{
    auto parsed = ProcessorRegistry::parseDefinitions(_draft, error);
    return parsed && _registry.validatePublication("workspace", *parsed, error);
}
void LineflowEditorModel::markApplied()
{
    _baseline = _draft;
    _originalNames.clear();
    for (const auto& block : blockIds())
    {
        for (const auto& value : nodes(_draft, block))
        {
            const auto name = value.toMap().value("name").toString();
            _originalNames[block].insert(name, name);
        }
    }
}
void LineflowEditorModel::markApplied(QVariantMap definitions)
{
    _draft = std::move(definitions);
    if (!blockIds().contains(_selected))
    {
        _selected = blockIds().value(0);
    }
    markApplied();
    _rebuild();
    emit draftChanged();
}
QString LineflowEditorModel::createBlock(const QString& name, QString* error)
{
    return createBlock(name, 1, error);
}

QString LineflowEditorModel::createBlock(const QString& name, int channels, QString* error)
{
    const auto id    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto       draft = _draft;
    draft.insert(id,
                 QVariantMap {
                     {    "id",                                                               id},
                     {  "name",                                                             name},
                     {  "kind",                                                       "lineflow"},
                     {"source", QVariantMap {{"channels", channels}, {"nodes", QVariantList {}}}}
    });
    if (!_commit(draft, error))
    {
        return {};
    }
    setSelectedBlock(id);
    return id;
}
QString LineflowEditorModel::duplicateBlock(const QString& id, const QString& name, QString* error)
{
    if (!blockIds().contains(id))
    {
        fail(error, tr("Select a Lineflow block."));
        return {};
    }
    const auto copy       = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto       definition = _draft.value(id).toMap();
    definition.insert("id", copy);
    definition.insert("name", name);
    auto draft = _draft;
    draft.insert(copy, definition);
    if (!_commit(draft, error))
    {
        return {};
    }
    setSelectedBlock(copy);
    return copy;
}
bool LineflowEditorModel::removeBlock(const QString& id, QString* error)
{
    if (!blockIds().contains(id))
    {
        return fail(error, tr("Select a Lineflow block."));
    }
    auto draft = _draft;
    draft.remove(id);
    if (!_commit(draft, error))
    {
        return false;
    }
    _originalNames.remove(id);
    if (_selected == id)
    {
        setSelectedBlock(blockIds().value(0));
    }
    return true;
}
bool LineflowEditorModel::renameBlock(const QString& id, const QString& name, QString* error)
{
    if (!blockIds().contains(id))
    {
        return fail(error, tr("Select a Lineflow block."));
    }
    auto draft      = _draft;
    auto definition = draft.value(id).toMap();
    definition.insert("name", name);
    draft.insert(id, definition);
    return _commit(draft, error);
}
QString LineflowEditorModel::insertNode(const QString& block, int row, const QString& catalogId, QString* error)
{
    const auto shape = processorShape(catalogId);
    return insertNodeAt(block, row, catalogId, shape.first == 2 && shape.second == 2 ? "duplex" : "rx", error);
}

QString LineflowEditorModel::insertNodeAt(const QString& block,
                                          int            row,
                                          const QString& catalogId,
                                          const QString& lane,
                                          QString*       error)
{
    if (!blockIds().contains(block))
    {
        fail(error, tr("Select a Lineflow block."));
        return {};
    }
    auto list = nodes(_draft, block);
    if (row < 0)
    {
        row = int(list.size());
    }
    if (row > list.size())
    {
        fail(error, tr("Invalid insertion position."));
        return {};
    }
    const auto  name = _defaultNodeName(block, catalogId);
    QVariantMap entry {
        {"name", name}
    };
    if (_draft.value(catalogId).toMap().value("kind") == "lineflow")
    {
        entry.insert("reference", catalogId);
    }
    else
    {
        entry.insert("type", catalogId);
    }
    const auto shape  = processorShape(catalogId);
    const bool duplex = shape.first == 2 && shape.second == 2;
    if ((!duplex && (shape.first != 1 || shape.second != 1))
        || (duplex && (lane != "duplex" || blockChannels(block) != 2)) || (!duplex && lane != "rx" && lane != "tx")
        || (blockChannels(block) == 1 && lane != "rx"))
    {
        fail(error, tr("The processor shape does not fit this lane."));
        return {};
    }
    entry.insert("lane", lane);
    list.insert(row, entry);
    auto draft = _draft;
    putNodes(draft, block, list);
    return _commit(draft, error) ? name : QString {};
}
bool LineflowEditorModel::moveNodeToLane(const QString& block,
                                         const QString& name,
                                         int            row,
                                         const QString& lane,
                                         QString*       error)
{
    auto      list = nodes(_draft, block);
    const int from = _nodeIndex(block, name);
    if (from < 0 || row < 0 || row > list.size())
    {
        return fail(error, tr("Invalid operation position."));
    }
    auto       entry  = list.takeAt(from).toMap();
    const auto target = entry.value(entry.contains("reference") ? "reference" : "type").toString();
    const auto shape  = processorShape(target);
    const bool duplex = shape.first == 2 && shape.second == 2;
    if ((duplex && (lane != "duplex" || blockChannels(block) != 2)) || (!duplex && lane != "rx" && lane != "tx")
        || (blockChannels(block) == 1 && lane != "rx"))
    {
        return fail(error, tr("The processor shape does not fit this lane."));
    }
    entry.insert("lane", lane);
    list.insert(row > from ? row - 1 : row, entry);
    auto draft = _draft;
    putNodes(draft, block, list);
    return _commit(draft, error);
}

bool LineflowEditorModel::canDropAt(const QMimeData* data, int row, const QString& lane, const QStringList& order) const
{
    const auto key = _dropKey(data, row, lane, order);
    if (key && _laneProbe && _laneProbe->key == *key)
    {
        return _laneProbe->accepted;
    }
    QMimeData snapshot;
    if (key)
    {
        snapshot.setData(key->format, key->payload);
    }
    const bool accepted = key ? bool(_dropDraft(&snapshot, row, key->lane, key->order)) :
                                bool(_dropDraft(data, row, lane, order));
    _laneProbe          = key ? std::optional<DropProbe>({*key, accepted}) : std::nullopt;
    return accepted;
}

void LineflowEditorModel::_invalidateDropProbes()
{
    _laneProbe.reset();
    _modelProbe.reset();
}

std::optional<LineflowEditorModel::DropKey> LineflowEditorModel::_dropKey(const QMimeData*   data,
                                                                          int                row,
                                                                          const QString&     lane,
                                                                          const QStringList& order) const
{
    if (!data)
    {
        return {};
    }
    const auto formats = mimeTypes();
    const auto format  = data->hasFormat(formats[1]) ? formats[1] : formats[0];
    if (!data->hasFormat(format))
    {
        return {};
    }
    const auto payload = data->data(format);
    // Retain at most one bounded probe per API, never the caller's QMimeData object.
    if (payload.size() > 64 * 1024 || order.size() > 2048)
    {
        return {};
    }
    qsizetype orderSize = 0;
    for (const auto& name : order)
    {
        orderSize += name.size();
        if (orderSize > 32 * 1024)
        {
            return {};
        }
    }
    QStringList ownedOrder;
    ownedOrder.reserve(order.size());
    for (const auto& name : order)
    {
        ownedOrder.append(QString(name.constData(), name.size()));
    }
    return DropKey {format,
                    QByteArray(payload.constData(), payload.size()),
                    row,
                    QString(lane.constData(), lane.size()),
                    std::move(ownedOrder)};
}

std::optional<QVariantMap> LineflowEditorModel::_dropDraft(const QMimeData*   data,
                                                           int                row,
                                                           const QString&     lane,
                                                           const QStringList& order) const
{
    if (!data || _selected.isEmpty() || row < 0 || row > rowCount())
    {
        return {};
    }
    auto draft = _draft;
    auto list  = nodes(draft, _selected);
    if (!order.isEmpty())
    {
        if (order.size() != list.size())
        {
            return {};
        }
        QMap<QString, QVariant> byName;
        for (const auto& value : list)
        {
            byName.insert(value.toMap().value("name").toString(), value);
        }
        QVariantList reordered;
        for (const auto& name : order)
        {
            if (!byName.contains(name))
            {
                return {};
            }
            reordered.append(byName.take(name));
        }
        // Only independent opposite-lane order may normalize. Per-lane order and
        // every existing duplex barrier remain authoritative.
        for (const auto& laneName : {QStringLiteral("rx"), QStringLiteral("tx")})
        {
            const auto projection = [&](const QVariantList& entries) {
                QStringList result;
                for (const auto& value : entries)
                {
                    const auto entry     = value.toMap();
                    const auto entryLane = entry.value("lane", "rx").toString();
                    if (entryLane == laneName || entryLane == "duplex")
                    {
                        result.append(entry.value("name").toString());
                    }
                }
                return result;
            };
            if (projection(list) != projection(reordered))
            {
                return {};
            }
        }
        list = std::move(reordered);
    }

    QVariantMap entry;
    QString     target;
    if (data->hasFormat(mimeTypes()[1]))
    {
        target          = QString::fromUtf8(data->data(mimeTypes()[1]));
        const auto name = _defaultNodeName(_selected, target);
        entry.insert("name", name);
        entry.insert(draft.value(target).toMap().value("kind") == "lineflow" ? "reference" : "type", target);
    }
    else if (data->hasFormat(mimeTypes()[0]))
    {
        const auto map  = QJsonDocument::fromJson(data->data(mimeTypes()[0])).toVariant().toMap();
        int        from = -1;
        for (int index = 0; index < list.size(); ++index)
        {
            if (list[index].toMap().value("name") == map.value("name"))
            {
                from = index;
                break;
            }
        }
        if (map.value("token").toString().toLatin1() != _token || map.value("block") != _selected || from < 0)
        {
            return {};
        }
        entry = list.takeAt(from).toMap();
        if (row > from)
        {
            --row;
        }
        target = entry.value(entry.contains("reference") ? "reference" : "type").toString();
    }
    else
    {
        return {};
    }
    const auto shape  = processorShape(target);
    const bool duplex = shape.first == 2 && shape.second == 2;
    if ((!duplex && (shape.first != 1 || shape.second != 1))
        || (duplex && (lane != "duplex" || blockChannels(_selected) != 2)) || (!duplex && lane != "rx" && lane != "tx")
        || (blockChannels(_selected) == 1 && lane != "rx"))
    {
        return {};
    }
    entry.insert("lane", lane);
    list.insert(row, entry);
    putNodes(draft, _selected, list);
    const auto parsed = ProcessorRegistry::parseDefinitions(draft);
    return parsed && _registry.validatePublication("workspace", *parsed) ?
               std::optional<QVariantMap>(std::move(draft)) :
               std::nullopt;
}

bool LineflowEditorModel::dropAt(const QMimeData* data, int row, const QString& lane, const QStringList& order)
{
    const auto draft = _dropDraft(data, row, lane, order);
    if (!draft)
    {
        emit rejected(tr("This processor cannot be placed here."));
        return false;
    }
    QString error;
    if (!_commit(*draft, &error))
    {
        emit rejected(error);
        return false;
    }
    return true;
}

bool LineflowEditorModel::setNodeFlipped(const QString& block, const QString& name, bool flipped, QString* error)
{
    const int index = _nodeIndex(block, name);
    if (index < 0)
    {
        return fail(error, tr("Operation no longer exists."));
    }
    auto       list   = nodes(_draft, block);
    auto       entry  = list[index].toMap();
    const auto target = entry.value(entry.contains("reference") ? "reference" : "type").toString();
    const auto shape  = processorShape(target);
    if (shape.first != 2 || shape.second != 2 || entry.value("lane", "rx") != "duplex")
    {
        return fail(error, tr("Only a duplex operation can be flipped."));
    }
    if (entry.value("flipped", false).toBool() == flipped)
    {
        return true;
    }
    entry.insert("flipped", flipped);
    list[index] = entry;
    auto draft  = _draft;
    putNodes(draft, block, list);
    return _commit(draft, error);
}

bool LineflowEditorModel::removeNode(const QString& block, const QString& name, QString* error)
{
    const int index = _nodeIndex(block, name);
    if (index < 0)
    {
        return fail(error, tr("Operation no longer exists."));
    }
    auto draft = _draft;
    auto list  = nodes(draft, block);
    list.removeAt(index);
    putNodes(draft, block, list);
    if (!_commit(draft, error))
    {
        return false;
    }
    _originalNames[block].remove(name);
    return true;
}
bool LineflowEditorModel::renameNode(const QString& block, const QString& from, const QString& to, QString* error)
{
    const int index = _nodeIndex(block, from);
    if (index < 0)
    {
        return fail(error, tr("Operation no longer exists."));
    }
    if (from == to)
    {
        return true;
    }
    auto draft = _draft;
    auto list  = nodes(draft, block);
    auto entry = list[index].toMap();
    entry.insert("name", to);
    list[index] = entry;
    putNodes(draft, block, list);
    if (!_commit(draft, error))
    {
        return false;
    }
    auto& names = _originalNames[block];
    if (names.contains(from))
    {
        const auto original = names.take(from);
        names.insert(to, original);
    }
    return true;
}
bool LineflowEditorModel::moveNode(const QString& block, int from, int to, QString* error)
{
    auto list = nodes(_draft, block);
    if (from < 0 || from >= list.size() || to < 0 || to >= list.size())
    {
        return fail(error, tr("Invalid operation position."));
    }
    list.move(from, to);
    auto draft = _draft;
    putNodes(draft, block, list);
    return _commit(draft, error);
}
bool LineflowEditorModel::setNodeConfiguration(const QString&     block,
                                               const QString&     name,
                                               const QVariantMap& configuration,
                                               QString*           error)
{
    const int index = _nodeIndex(block, name);
    if (index < 0)
    {
        return fail(error, tr("Operation no longer exists."));
    }
    auto list  = nodes(_draft, block);
    auto entry = list[index].toMap();
    if (entry.contains("reference"))
    {
        return fail(error, tr("Edit a referenced block in its own definition."));
    }
    if (entry.value("configuration").toMap() == configuration)
    {
        return true;
    }
    entry.insert("configuration", configuration);
    list[index] = entry;
    auto draft  = _draft;
    putNodes(draft, block, list);
    auto parsed = ProcessorRegistry::parseDefinitions(draft, error);
    if (!parsed || !_registry.validatePublication("workspace", *parsed, error))
    {
        return false;
    }
    _invalidateDropProbes();
    _draft = std::move(draft);
    emit draftChanged();
    return true;
}
QString LineflowEditorModel::_unavailableReason(const QString& id, QSet<QString> path)
{
    if (_availability.contains(id))
    {
        return _availability.value(id);
    }
    if (path.contains(id) || path.size() >= 16)
    {
        return tr("Reference cycle or depth limit.");
    }
    const auto definition = _draft.value(id).toMap();
    if (definition.value("kind") != "lineflow")
    {
        const auto type    = _registry.type(id);
        const bool removed = type && type->origin == "workspace" && !_draft.contains(id);
        const auto reason  = type && type->available && !removed ? QString {} :
                             removed || !type                    ? tr("Processor is unavailable: %1").arg(id) :
                                                                   type->unavailableReason;
        _availability.insert(id, reason);
        return reason;
    }
    path.insert(id);
    for (const auto& value : nodes(_draft, id))
    {
        const auto node   = value.toMap();
        const auto target = node.value(node.contains("reference") ? "reference" : "type").toString();
        const auto reason = _unavailableReason(target, path);
        if (!reason.isEmpty())
        {
            _availability.insert(id, reason);
            return reason;
        }
    }
    _availability.insert(id, {});
    return {};
}
void LineflowEditorModel::_rebuild()
{
    _invalidateDropProbes();
    clear();
    _availability.clear();
    _rendered = 0;
    setHorizontalHeaderLabels({tr("Operation"), tr("Processor")});
    if (!_selected.isEmpty())
    {
        _append(invisibleRootItem(), _selected, false, {}, 0);
    }
}
void LineflowEditorModel::_append(QStandardItem* parent,
                                  const QString& block,
                                  bool           linked,
                                  QSet<QString>  path,
                                  int            depth)
{
    if (depth >= 16 || path.contains(block) || _rendered >= 2048)
    {
        return;
    }
    path.insert(block);
    for (const auto& value : nodes(_draft, block))
    {
        if (_rendered++ >= 2048)
        {
            return;
        }
        const auto node       = value.toMap();
        const auto target     = node.value(node.contains("reference") ? "reference" : "type").toString();
        const auto definition = _draft.value(target).toMap();
        const auto metadata   = _registry.type(target);
        QString    title      = definition.value("name").toString();
        if (title.isEmpty())
        {
            title = metadata ? metadata->title : target;
        }
        const bool localBlock  = definition.value("kind") == "lineflow";
        const auto reason      = _unavailableReason(target);
        const bool unavailable = !reason.isEmpty();
        auto*      item        = new QStandardItem(node.value("name").toString());
        auto*      description = new QStandardItem(title);
        item->setEditable(false);
        description->setEditable(false);
        for (auto* column : {item, description})
        {
            const auto shape = processorShape(target);
            column->setData(node.value("lane", "rx"), LaneRole);
            column->setData(shape.first, InputCountRole);
            column->setData(shape.second, OutputCountRole);
            column->setData(_portNames(target, true), InputNamesRole);
            column->setData(_portNames(target, false), OutputNamesRole);
            column->setData(node.value("flipped", false), FlippedRole);
            column->setData(block, BlockRole);
            column->setData(node.value("name"), NodeRole);
            column->setData(target, CatalogIdRole);
            column->setData(linked, LinkedRole);
            column->setData(unavailable, UnavailableRole);
            column->setToolTip(unavailable ? reason : target);
            column->setDropEnabled(false);
            column->setDragEnabled(!linked);
        }
        const auto shape = processorShape(target);
        item->setToolTip(node.value("name").toString() + '\n' + (unavailable ? reason : target)
                         + QString("\n%1 × %2").arg(shape.first).arg(shape.second));
        const bool composite = localBlock || (metadata && metadata->kind != "native");
        item->setIcon(unavailable ? Icons::icon("triangle-exclamation-point") :
                                    Icons::processorBlock(composite, shape.first == 2 && shape.second == 2));
        parent->appendRow({item, description});
        if (localBlock)
        {
            _append(item, target, true, path, depth + 1);
        }
    }
}
QStringList LineflowEditorModel::mimeTypes() const
{
    return {"application/x-termaster-lineflow-node", "application/x-termaster-processor"};
}
QMimeData* LineflowEditorModel::mimeData(const QModelIndexList& indexes) const
{
    auto* data = new QMimeData;
    for (const auto& index : indexes)
    {
        if (index.column() == 0 && !index.data(LinkedRole).toBool())
        {
            data->setData(mimeTypes()[0],
                          QJsonDocument::fromVariant(QVariantMap {
                                                         {"token", QString::fromLatin1(_token)},
                                                         {"block",       index.data(BlockRole)},
                                                         { "name",        index.data(NodeRole)}
            })
                              .toJson(QJsonDocument::Compact));
            break;
        }
    }
    return data;
}
bool LineflowEditorModel::canDropMimeData(const QMimeData*   data,
                                          Qt::DropAction     action,
                                          int                row,
                                          int                column,
                                          const QModelIndex& parent) const
{
    if (action == Qt::IgnoreAction)
    {
        return true;
    }
    if (column > 0 || parent.isValid() || row > rowCount() || _selected.isEmpty())
    {
        return false;
    }
    const auto key = _dropKey(data, row);
    if (key && _modelProbe && _modelProbe->key == *key)
    {
        return _modelProbe->accepted;
    }
    QMimeData snapshot;
    if (key)
    {
        snapshot.setData(key->format, key->payload);
    }
    const bool accepted = key ? _canDropMimeData(&snapshot, row) : data && _canDropMimeData(data, row);
    _modelProbe         = key ? std::optional<DropProbe>({*key, accepted}) : std::nullopt;
    return accepted;
}

bool LineflowEditorModel::_canDropMimeData(const QMimeData* data, int row) const
{
    if (data->hasFormat(mimeTypes()[1]))
    {
        const auto  target = QString::fromUtf8(data->data(mimeTypes()[1]));
        auto        draft  = _draft;
        auto        list   = nodes(draft, _selected);
        const auto  name   = _defaultNodeName(_selected, target);
        QVariantMap node {
            {"name", name}
        };
        node.insert(draft.value(target).toMap().value("kind") == "lineflow" ? "reference" : "type", target);
        list.insert(row < 0 ? list.size() : row, node);
        putNodes(draft, _selected, list);
        const auto parsed = ProcessorRegistry::parseDefinitions(draft);
        return parsed && _registry.validatePublication("workspace", *parsed);
    }
    if (!data->hasFormat(mimeTypes()[0]))
    {
        return false;
    }
    const auto map = QJsonDocument::fromJson(data->data(mimeTypes()[0])).toVariant().toMap();
    return map.value("token").toString().toLatin1() == _token && map.value("block") == _selected
        && _nodeIndex(_selected, map.value("name").toString()) >= 0;
}
bool LineflowEditorModel::dropMimeData(const QMimeData*   data,
                                       Qt::DropAction     action,
                                       int                row,
                                       int                column,
                                       const QModelIndex& parent)
{
    if (!canDropMimeData(data, action, row, column, parent))
    {
        emit rejected(tr("This drop would create an invalid reference or operation order."));
        return false;
    }
    if (action == Qt::IgnoreAction)
    {
        return true;
    }
    if (row < 0)
    {
        row = rowCount();
    }
    if (data->hasFormat(mimeTypes()[1]))
    {
        return !insertNode(_selected, row, QString::fromUtf8(data->data(mimeTypes()[1]))).isEmpty();
    }
    const auto map  = QJsonDocument::fromJson(data->data(mimeTypes()[0])).toVariant().toMap();
    const int  from = _nodeIndex(_selected, map.value("name").toString());
    if (from < row)
    {
        --row;
    }
    return moveNode(_selected, from, row);
}
Qt::DropActions LineflowEditorModel::supportedDropActions() const
{
    return Qt::MoveAction | Qt::CopyAction;
}

bool LineflowEditorModel::removeRows(int, int, const QModelIndex&)
{
    // The authoritative move already rebuilt the presentation. QAbstractItemView
    // may request source-row removal after a successful MoveAction drop.
    return false;
}
