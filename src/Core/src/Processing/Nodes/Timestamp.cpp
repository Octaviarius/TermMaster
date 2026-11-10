#include "Timestamp.h"

#include <QDateTime>

Timestamp::Timestamp(QVariantMap configuration, Limits limits, QList<Part> parts) :
    SyncProcessorNode(configuration, limits), _format(configuration.value("format").toString()),
    _parts(std::move(parts)), _utc(configuration.value("timeZone").toString() == "utc")
{
}

QByteArray Timestamp::schema()
{
    return R"(version: 1
type: termaster.settings-schema
description: Per-packet timestamp settings
content:
  type: object
  x-widget: timestampPreview
  properties:
    format:
      type: string
      title: Time format
      description: "Qt date/time format: yyyy-MM-dd HH:mm:ss.zzz. Enclose literal words in single quotes. Timestamp text is UTF-8; packet bytes remain unchanged."
      maxLength: 512
      default: 'yyyy-MM-dd HH:mm:ss.zzz'
      x-group: timestamp
      x-order: 1
    timeZone:
      type: string
      title: Time zone
      enum: [local, utc]
      default: local
      x-group: timestamp
      x-order: 2
    template:
      type: string
      title: Output template
      description: 'Use ${timestamp} and ${packet} in any order; $$ is a literal dollar. Byte escapes: \r \n \t \0 \e \\ \xHH. ANSI colors may surround any part, for example \e[90m${timestamp}\e[0m ${packet}.'
      maxLength: 4096
      default: '[${timestamp}] ${packet}'
      x-group: timestamp
      x-order: 3
  x-groups:
    - id: timestamp
      title: Timestamp
)";
}

std::unique_ptr<Processor> Timestamp::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error))
    {
        return {};
    }

    auto configuration = input;
    if (!_stringOption(configuration, "timeZone", "local", {"local", "utc"}, error))
    {
        return {};
    }
    if (!configuration.contains("format"))
    {
        configuration.insert("format", QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    }
    const auto format = configuration.value("format");
    if (format.typeId() != QMetaType::QString || format.toString().size() > 512)
    {
        _fail(error, QStringLiteral("Timestamp format must be a string of at most 512 characters"));
        return {};
    }

    if (!configuration.contains("template"))
    {
        configuration.insert("template", QStringLiteral("[${timestamp}] ${packet}"));
    }
    const auto text = configuration.value("template");
    if (text.typeId() != QMetaType::QString || text.toString().size() > 4096)
    {
        _fail(error, QStringLiteral("Timestamp template must be a string of at most 4096 characters"));
        return {};
    }
    QList<Part> parts;
    if (!_parseTemplate(text.toString(), parts, error))
    {
        return {};
    }

    return std::unique_ptr<Processor>(new Timestamp(std::move(configuration), limits, std::move(parts)));
}

void Timestamp::_process(const Chunk& input, QByteArray& output, QString& error)
{
    const auto now   = _utc ? QDateTime::currentDateTimeUtc() : QDateTime::currentDateTime();
    const auto stamp = now.toString(_format).toUtf8();

    for (const auto& part : _parts)
    {
        if (part.kind == Part::Kind::Packet)
        {
            if (input.bytes.size() > _maximumBytes() - output.size())
            {
                _fail(&error, QStringLiteral("Processing node output exceeds byte limit"));
                return;
            }
            // Avoid sharing borrowed fromRawData storage when output is empty.
            output.append(input.bytes.constData(), input.bytes.size());
            continue;
        }

        const auto& bytes = part.kind == Part::Kind::Timestamp ? stamp : part.literal;
        if (!_append(output, bytes, error))
        {
            return;
        }
    }
}

void Timestamp::_reset()
{
}

bool Timestamp::_parseTemplate(const QString& text, QList<Part>& parts, QString* error)
{
    QString    literal;
    const auto flush = [&] {
        if (!literal.isEmpty())
        {
            parts.append({Part::Kind::Literal, literal.toUtf8()});
            literal.clear();
        }
    };
    const auto hex = [](QChar c) {
        const auto ch = c.unicode();
        if (ch >= '0' && ch <= '9')
        {
            return int(ch - '0');
        }
        if (ch >= 'a' && ch <= 'f')
        {
            return int(ch - 'a' + 10);
        }
        if (ch >= 'A' && ch <= 'F')
        {
            return int(ch - 'A' + 10);
        }
        return -1;
    };

    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const auto ch = text.at(i);
        if (ch == '$')
        {
            if (i + 1 < text.size() && text.at(i + 1) == '$')
            {
                literal.append('$');
                ++i;
                continue;
            }
            if (i + 1 >= text.size() || text.at(i + 1) != '{')
            {
                return _fail(error, QStringLiteral("Timestamp template dollar must use $$ or ${timestamp}/${packet}"));
            }
            const auto end = text.indexOf('}', i + 2);
            if (end < 0)
            {
                return _fail(error, QStringLiteral("Timestamp template has an incomplete placeholder"));
            }
            const auto name = text.mid(i + 2, end - i - 2);
            if (name != "timestamp" && name != "packet")
            {
                return _fail(error, QStringLiteral("Unknown Timestamp template placeholder: ") + name);
            }
            flush();
            parts.append({name == "packet" ? Part::Kind::Packet : Part::Kind::Timestamp, {}});
            i = end;
        }
        else if (ch == '\\')
        {
            if (++i == text.size())
            {
                return _fail(error, QStringLiteral("Timestamp template ends with an incomplete escape"));
            }
            const auto escaped = text.at(i);
            if (escaped == 'r')
            {
                literal.append('\r');
            }
            else if (escaped == 'n')
            {
                literal.append('\n');
            }
            else if (escaped == 't')
            {
                literal.append('\t');
            }
            else if (escaped == '0')
            {
                literal.append(QChar(0));
            }
            else if (escaped == 'e')
            {
                literal.append(QChar(27));
            }
            else if (escaped == '\\')
            {
                literal.append('\\');
            }
            else if (escaped == 'x')
            {
                if (i + 2 >= text.size() || hex(text.at(i + 1)) < 0 || hex(text.at(i + 2)) < 0)
                {
                    return _fail(error, QStringLiteral("Timestamp hexadecimal escape requires two digits"));
                }
                flush();
                parts.append(
                    {Part::Kind::Literal, QByteArray(1, char((hex(text.at(i + 1)) << 4) | hex(text.at(i + 2))))});
                i += 2;
            }
            else
            {
                return _fail(error, QStringLiteral("Unsupported Timestamp template escape"));
            }
        }
        else
        {
            literal.append(ch);
        }
    }
    flush();
    return true;
}
