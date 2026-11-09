#include <QRegularExpression>
#include <QSet>
#include <Termaster/Core/Processing/ProcessorBlocks.h>
#include <functional>

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

bool validId(const QVariant& value)
{
    static const QRegularExpression pattern("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\\z");
    return value.typeId() == QMetaType::QString && pattern.match(value.toString()).hasMatch();
}

bool validNodeName(const QVariant& value)
{
    if (value.typeId() != QMetaType::QString)
    {
        return false;
    }
    const auto name = value.toString();
    return !name.isEmpty() && name.size() <= 256 && name == name.trimmed() && !name.contains(QChar::Null)
        && !name.contains(QLatin1Char('/'));
}
} // namespace

bool ProcessorBlocks::validate(const QVariantMap& blocks, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (blocks.size() > 1024)
    {
        return fail(error, "Workspace supports at most 1024 processor blocks");
    }
    QMap<QString, QStringList> references;
    qsizetype                  totalNodes = 0;
    for (auto it = blocks.cbegin(); it != blocks.cend(); ++it)
    {
        if (!validId(it.key()) || it.value().typeId() != QMetaType::QVariantMap)
        {
            return fail(error, "Processor blocks require valid IDs and mapping definitions");
        }
        const auto block = it.value().toMap();
        if (block.value("id").typeId() != QMetaType::QString || block.value("id").toString() != it.key())
        {
            return fail(error, "Processor block ID must match its mapping key: " + it.key());
        }
        const auto name = block.value("name");
        if (name.typeId() != QMetaType::QString || name.toString().trimmed().isEmpty() || name.toString().size() > 256
            || name.toString().contains(QChar::Null))
        {
            return fail(error, "Processor block name must contain 1 to 256 characters and no NUL");
        }
        const auto nodes = block.value("nodes");
        if (nodes.typeId() != QMetaType::QVariantList || nodes.toList().size() > 256)
        {
            return fail(error, "Processor block nodes must be a list of at most 256 nodes");
        }
        totalNodes += nodes.toList().size();
        if (totalNodes > 16384)
        {
            return fail(error, "Workspace processor blocks exceed 16384 nodes");
        }
        QSet<QString> nodeNames;
        for (const auto& value : nodes.toList())
        {
            if (value.typeId() != QMetaType::QVariantMap)
            {
                return fail(error, "Processor block node must be a mapping");
            }
            const auto node = value.toMap();
            if (!validNodeName(node.value("name")) || nodeNames.contains(node.value("name").toString()))
            {
                return fail(error,
                            "Processor nodes require unique names without slashes, NUL or surrounding whitespace");
            }
            nodeNames.insert(node.value("name").toString());
            if (node.contains("type") == node.contains("reference"))
            {
                return fail(error, "Processor block node requires exactly one type or reference");
            }
            const auto key = node.contains("type") ? "type" : "reference";
            if (!validId(node.value(key)))
            {
                return fail(error, "Processor block node has an invalid type or reference ID");
            }
            if (node.contains("configuration") && node.value("configuration").typeId() != QMetaType::QVariantMap)
            {
                return fail(error, "Processor node configuration must be a mapping");
            }
            if (node.contains("reference"))
            {
                if (node.contains("configuration"))
                {
                    return fail(error, "Reference nodes do not accept processor configuration");
                }
                references[it.key()].append(node.value("reference").toString());
            }
        }
    }
    QSet<QString>                            visiting;
    QMap<QString, int>                       heights;
    std::function<bool(const QString&, int)> visit = [&](const QString& id, int depth) {
        if (!blocks.contains(id))
        {
            return fail(error, "Unknown processor block reference: " + id);
        }
        if (visiting.contains(id))
        {
            return fail(error, "Processor block reference cycle at: " + id);
        }
        if (depth > 16)
        {
            return fail(error, "Processor block reference depth exceeds 16");
        }
        if (heights.contains(id))
        {
            return depth + heights.value(id) - 1 <= 16 || fail(error, "Processor block reference depth exceeds 16");
        }
        visiting.insert(id);
        int height = 1;
        for (const auto& reference : references.value(id))
        {
            if (!visit(reference, depth + 1))
            {
                return false;
            }
        }
        for (const auto& reference : references.value(id))
        {
            height = qMax(height, 1 + heights.value(reference));
        }
        visiting.remove(id);
        heights.insert(id, height);
        return true;
    };
    for (auto it = blocks.cbegin(); it != blocks.cend(); ++it)
    {
        if (!visit(it.key(), 1))
        {
            return false;
        }
    }
    return true;
}
