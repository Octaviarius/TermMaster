#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <Termaster/Core/Help/HelpSource.h>
#include <Termaster/Core/SettingsSchema.h>
#include <stdexcept>

namespace
{
constexpr qsizetype totalLimit = 16 * 1024 * 1024;
constexpr qsizetype pageLimit  = 256 * 1024;
constexpr qsizetype entryLimit = 1024;

bool fail(QString* error, const QString& message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

bool validId(const QString& id)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,191}$"));
    const auto                      match = expression.match(id);
    return match.hasMatch() && match.capturedLength() == id.size();
}

QString owned(const QString& text)
{
    return QString(text.constData(), text.size());
}

QUrl sourceUrl(const QString& owner, const QString& path)
{
    QUrl url;
    url.setScheme(QStringLiteral("help-asset"));
    url.setPath(QStringLiteral("/") + owner + QStringLiteral("/") + path);
    return url;
}

QUrl pageUrl(QUrl url)
{
    url.setFragment({});
    url.setQuery({});
    return url.adjusted(QUrl::NormalizePathSegments);
}

QString safePath(const QString& root, const QString& relative)
{
    if (relative.isEmpty() || relative.contains(QChar::Null) || relative.contains(QLatin1Char('\\'))
        || relative.contains(QLatin1Char(':')) || QDir::isAbsolutePath(relative))
    {
        return {};
    }

    const auto parts = relative.split(QLatin1Char('/'));
    for (const auto& part : parts)
    {
        if (part.isEmpty() || part == QStringLiteral("..") || part == QStringLiteral("."))
        {
            return {};
        }
    }

    const auto path = root + QLatin1Char('/') + relative;
    if (root.startsWith(QStringLiteral(":")))
    {
        return path;
    }

    const auto canonicalRoot = QFileInfo(root).canonicalFilePath();
    const auto canonicalPath = QFileInfo(path).canonicalFilePath();
    if (canonicalRoot.isEmpty() || canonicalPath.isEmpty()
        || !canonicalPath.startsWith(canonicalRoot + QLatin1Char('/'),
#ifdef Q_OS_WIN
                                     Qt::CaseInsensitive
#else
                                     Qt::CaseSensitive
#endif
                                     ))
        return {};
    return canonicalPath;
}

bool readFile(const QString& path, qsizetype limit, QByteArray& bytes, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return fail(error, QStringLiteral("Cannot read help file %1: %2").arg(path, file.errorString()));
    }
    if (file.size() > limit)
    {
        return fail(error, QStringLiteral("Help file exceeds its size limit: %1").arg(path));
    }

    bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > limit)
    {
        return fail(error, QStringLiteral("Cannot read bounded help file: %1").arg(path));
    }
    return true;
}

QString literal(const QVariant& value)
{
    const auto encoded = QJsonDocument(QJsonArray {QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact);
    auto       text    = QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
    text.replace(QStringLiteral("`"), QStringLiteral("\\`"));
    return QStringLiteral("`") + text + QStringLiteral("`");
}
} // namespace

HelpSource::HelpSource(QObject* parent) : QObject(parent)
{
}

bool HelpSource::loadManifest(const QString& owner, const QString& manifestPath, QString* error)
{
    if (!validId(owner))
    {
        return fail(error, QStringLiteral("Invalid help owner ID"));
    }
    if (manifestPath.contains(QChar::Null)
        || (!manifestPath.startsWith(QStringLiteral(":/")) && !QDir::isAbsolutePath(manifestPath)))
    {
        return fail(error, QStringLiteral("Help manifest path must be absolute"));
    }

    QByteArray bytes;
    if (!readFile(manifestPath, pageLimit, bytes, error))
    {
        return false;
    }
    QJsonParseError parseError;
    const auto      document = QJsonDocument::fromJson(bytes, &parseError);
    const auto      envelope = document.object();
    const auto      content  = envelope.value(QStringLiteral("content")).toObject();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || envelope.value(QStringLiteral("version")) != QJsonValue(1)
        || envelope.value(QStringLiteral("type")) != QJsonValue(QStringLiteral("termaster.help"))
        || !envelope.value(QStringLiteral("description")).isString()
        || !envelope.value(QStringLiteral("content")).isObject() || !content.value(QStringLiteral("topics")).isArray()
        || (content.contains(QStringLiteral("assets")) && !content.value(QStringLiteral("assets")).isArray()))
    {
        return fail(error, QStringLiteral("Invalid version 1 termaster.help manifest"));
    }

    const auto entries = content.value(QStringLiteral("topics")).toArray();
    const auto assets  = content.value(QStringLiteral("assets")).toArray();
    if (entries.size() > entryLimit || assets.size() > entryLimit)
    {
        return fail(error, QStringLiteral("Help manifest contains too many entries"));
    }

    const auto    root = QFileInfo(manifestPath).path();
    Snapshot      snapshot;
    QSet<QString> files;
    qsizetype     total = bytes.size();
    for (const auto& entry : entries)
    {
        const auto object = entry.toObject();
        const auto file   = object.value(QStringLiteral("file")).toString();
        const auto path   = safePath(root, file);
        if (!entry.isObject() || !object.value(QStringLiteral("id")).isString()
            || !object.value(QStringLiteral("title")).isString()
            || (object.contains(QStringLiteral("parent")) && !object.value(QStringLiteral("parent")).isString())
            || path.isEmpty() || files.contains(file))
        {
            return fail(error, QStringLiteral("Invalid or unsafe help topic entry"));
        }
        files.insert(file);

        if (!readFile(path, pageLimit, bytes, error))
        {
            return false;
        }
        total += bytes.size();
        if (total > totalLimit)
        {
            return fail(error, QStringLiteral("Help snapshot exceeds its total size limit"));
        }
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString  markdown = decoder(bytes);
        if (decoder.hasError())
        {
            return fail(error, QStringLiteral("Help Markdown is not valid UTF-8: %1").arg(file));
        }
        snapshot.topics.append({object.value(QStringLiteral("id")).toString(),
                                object.value(QStringLiteral("title")).toString(),
                                object.value(QStringLiteral("parent")).toString(),
                                markdown,
                                owner,
                                sourceUrl(owner, file)});
    }
    static const QSet<QString> extensions {QStringLiteral("png"),
                                           QStringLiteral("jpg"),
                                           QStringLiteral("jpeg"),
                                           QStringLiteral("gif"),
                                           QStringLiteral("bmp"),
                                           QStringLiteral("webp")};
    for (const auto& entry : assets)
    {
        const auto file = entry.toString();
        const auto path = safePath(root, file);
        if (!entry.isString() || path.isEmpty() || files.contains(file)
            || !extensions.contains(QFileInfo(file).suffix().toLower()))
        {
            return fail(error, QStringLiteral("Invalid or unsafe help image entry"));
        }
        files.insert(file);
        if (!readFile(path, totalLimit - total, bytes, error))
        {
            return false;
        }
        total += bytes.size();
        snapshot.assets.insert(sourceUrl(owner, file), QByteArray(bytes.constData(), bytes.size()));
    }
    return _replace(owner, std::move(snapshot), error);
}

bool HelpSource::replaceTopics(const QString& owner, QList<Topic> topics, QString* error)
{
    return _replace(owner, {std::move(topics), {}}, error);
}

void HelpSource::removeOwner(const QString& owner)
{
    if (_owners.remove(owner))
    {
        emit changed();
    }
}

QList<HelpSource::Topic> HelpSource::topics() const
{
    QList<Topic> result;
    for (const auto& snapshot : _owners)
    {
        result.append(snapshot.topics);
    }
    return result;
}

std::optional<HelpSource::Topic> HelpSource::topic(const QString& id) const
{
    for (const auto& snapshot : _owners)
    {
        for (const auto& topic : snapshot.topics)
        {
            if (topic.id == id)
            {
                return topic;
            }
        }
    }
    return std::nullopt;
}

QList<HelpSource::Topic> HelpSource::search(const QString& query) const
{
    const auto   needle = query.left(256).trimmed();
    QList<Topic> result;
    if (needle.isEmpty())
    {
        return result;
    }
    for (const auto& snapshot : _owners)
    {
        for (const auto& topic : snapshot.topics)
        {
            if (topic.title.contains(needle, Qt::CaseInsensitive)
                || topic.markdown.contains(needle, Qt::CaseInsensitive))
            {
                result.append(topic);
            }
        }
    }
    return result;
}

QString HelpSource::topicForUrl(const QUrl& url) const
{
    if (url.scheme() == QStringLiteral("help"))
    {
        const auto id = url.path();
        return topic(id) ? id : QString();
    }
    const auto source = pageUrl(url);
    for (const auto& snapshot : _owners)
    {
        for (const auto& topic : snapshot.topics)
        {
            if (!topic.source.isEmpty() && pageUrl(topic.source) == source)
            {
                return topic.id;
            }
        }
    }
    return {};
}

QByteArray HelpSource::asset(const QUrl& url) const
{
    const auto source = pageUrl(url);
    for (const auto& snapshot : _owners)
    {
        const auto found = snapshot.assets.constFind(source);
        if (found != snapshot.assets.cend())
        {
            return found.value();
        }
    }
    return {};
}

QMap<QUrl, QByteArray> HelpSource::assets() const
{
    QMap<QUrl, QByteArray> result;
    for (const auto& snapshot : _owners)
    {
        result.insert(snapshot.assets);
    }
    return result;
}

QString HelpSource::schemaReference(const QByteArray& descriptor)
{
    try
    {
        const SettingsSchema schema(descriptor);
        return schemaReference(schema);
    }
    catch (const std::exception&)
    {
        return {};
    }
}

QString HelpSource::schemaReference(const SettingsSchema& schema)
{
    QString markdown = QStringLiteral("## Parameters\n\n");
    for (const auto& field : schema.fields())
    {
        markdown += QStringLiteral("### %1 (`%2`)\n\n%3\n\n").arg(field.title, field.key, field.description);
        markdown += QStringLiteral("- Type: %1\n- Settings path: %2\n- Required: %3\n")
                        .arg(field.type,
                             literal(field.settingsPath),
                             field.required ? QStringLiteral("yes") : QStringLiteral("no"));
        if (field.readOnly)
        {
            markdown += QStringLiteral("- Read only: yes\n");
        }
        if (field.hasDefault)
        {
            markdown += QStringLiteral("- Default: %1\n").arg(literal(field.defaultValue));
        }
        if (!field.group.isEmpty())
        {
            QString title = field.group;
            for (const auto& group : schema.groups())
            {
                if (group.id == field.group)
                {
                    title = group.title;
                }
            }
            markdown += QStringLiteral("- Group: %1\n").arg(title);
        }
        if (!field.variants.isEmpty())
        {
            markdown += QStringLiteral("- Variants: %1\n").arg(field.variants.join(QStringLiteral(", ")));
        }
        for (const auto& key : {"enum",
                                "const",
                                "minimum",
                                "maximum",
                                "exclusiveMinimum",
                                "exclusiveMaximum",
                                "multipleOf",
                                "minLength",
                                "maxLength",
                                "pattern",
                                "minItems",
                                "maxItems"})
        {
            if (field.definition.contains(QLatin1String(key)))
            {
                markdown += QStringLiteral("- %1: %2\n")
                                .arg(QLatin1String(key), literal(field.definition.value(QLatin1String(key))));
            }
        }
        markdown += QLatin1Char('\n');
    }
    return markdown;
}

bool HelpSource::_replace(const QString& owner, Snapshot snapshot, QString* error)
{
    if (!validId(owner) || snapshot.topics.size() > entryLimit)
    {
        return fail(error, QStringLiteral("Invalid help owner or topic count"));
    }

    QMap<QString, QString> parents;
    qsizetype              total      = 0;
    qsizetype              count      = 0;
    qsizetype              assetCount = snapshot.assets.size();
    for (auto it = _owners.cbegin(); it != _owners.cend(); ++it)
    {
        if (it.key() != owner)
        {
            assetCount += it.value().assets.size();
            for (const auto& topic : it.value().topics)
            {
                parents.insert(topic.id, topic.parent);
                total += topic.markdown.toUtf8().size() + topic.title.size() * 2;
                ++count;
            }
            for (const auto& bytes : it.value().assets)
            {
                total += bytes.size();
            }
        }
    }
    for (auto& topic : snapshot.topics)
    {
        const auto size = topic.markdown.toUtf8().size();
        if (!validId(topic.id) || (!topic.parent.isEmpty() && !validId(topic.parent)) || topic.title.trimmed().isEmpty()
            || topic.title.size() > 1024 || size > pageLimit || parents.contains(topic.id))
        {
            return fail(error, QStringLiteral("Invalid, duplicate or oversized help topic: %1").arg(topic.id));
        }
        parents.insert(topic.id, topic.parent);
        total += size + topic.title.size() * 2;
        ++count;
        topic.id       = owned(topic.id);
        topic.title    = owned(topic.title);
        topic.parent   = owned(topic.parent);
        topic.markdown = owned(topic.markdown);
        topic.owner    = owned(owner);
        topic.source   = QUrl(owned(topic.source.toString()));
    }
    for (const auto& bytes : snapshot.assets)
    {
        total += bytes.size();
    }
    if (total > totalLimit || count > entryLimit || assetCount > entryLimit
        || (!_owners.contains(owner) && _owners.size() >= entryLimit))
    {
        return fail(error, QStringLiteral("Help catalog exceeds its size or topic count limit"));
    }

    for (auto it = parents.cbegin(); it != parents.cend(); ++it)
    {
        QSet<QString> visited;
        auto          current = it.key();
        while (parents.contains(current))
        {
            if (visited.contains(current))
            {
                return fail(error, QStringLiteral("Help topic parent cycle: %1").arg(it.key()));
            }
            visited.insert(current);
            current = parents.value(current);
        }
    }

    _owners.insert(owned(owner), std::move(snapshot));
    if (error)
    {
        error->clear();
    }
    emit changed();
    return true;
}
