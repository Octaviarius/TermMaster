#include <Termaster/Core/Terminal/CommandInput.h>

namespace
{
int hexDigit(QChar character)
{
    const ushort value = character.unicode();
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    return -1;
}
} // namespace

std::optional<QByteArray> CommandInput::prepare(const QString& text,
                                                const QString& terminator,
                                                const QString& custom,
                                                QString*       error)
{
    if (error)
    {
        error->clear();
    }
    const auto reject = [error](const QString& reason) -> std::optional<QByteArray> {
        if (error)
        {
            *error = reason;
        }
        return std::nullopt;
    };
    if (text.size() > MaximumBytes || custom.size() > MaximumBytes)
    {
        return reject(QStringLiteral("Command exceeds the 4 MiB byte limit."));
    }

    QByteArray result = text.toUtf8();

    const QString kind = terminator.toLower();
    if (kind == QStringLiteral("lf"))
    {
        result.append('\n');
    }
    else if (kind == QStringLiteral("crlf"))
    {
        result.append("\r\n");
    }
    else if (kind == QStringLiteral("http"))
    {
        result.append("\r\n\r\n");
    }
    else if (kind == QStringLiteral("cr"))
    {
        result.append('\r');
    }
    else if (kind == QStringLiteral("nul"))
    {
        result.append('\0');
    }
    else if (kind == QStringLiteral("custom"))
    {
        QString    literal;
        const auto flushLiteral = [&] {
            result.append(literal.toUtf8());
            literal.clear();
        };
        for (qsizetype index = 0; index < custom.size(); ++index)
        {
            if (custom[index] != QLatin1Char('\\'))
            {
                literal.append(custom[index]);
                continue;
            }
            flushLiteral();
            if (++index >= custom.size())
            {
                return reject(QStringLiteral("Custom terminator ends with an incomplete escape."));
            }
            const QChar escape = custom[index];
            if (escape == QLatin1Char('r'))
            {
                result.append('\r');
            }
            else if (escape == QLatin1Char('n'))
            {
                result.append('\n');
            }
            else if (escape == QLatin1Char('0'))
            {
                result.append('\0');
            }
            else if (escape == QLatin1Char('\\'))
            {
                result.append('\\');
            }
            else if (escape == QLatin1Char('x'))
            {
                if (index + 2 >= custom.size())
                {
                    return reject(QStringLiteral("Custom hexadecimal escape requires two digits."));
                }
                const int high = hexDigit(custom[++index]);
                const int low  = hexDigit(custom[++index]);
                if (high < 0 || low < 0)
                {
                    return reject(QStringLiteral("Custom hexadecimal escape contains invalid digits."));
                }
                result.append(static_cast<char>((high << 4) | low));
            }
            else
            {
                return reject(QStringLiteral("Custom terminator contains an unsupported escape."));
            }
        }
        flushLiteral();
    }
    else if (kind != QStringLiteral("none"))
    {
        return reject(QStringLiteral("Unknown command terminator."));
    }

    if (result.size() > MaximumBytes)
    {
        return reject(QStringLiteral("Command exceeds the 4 MiB byte limit."));
    }
    return result;
}
