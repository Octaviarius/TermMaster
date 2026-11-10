#include "Base64Decoder.h"

#include "Base64.h"

Base64Decoder::Base64Decoder(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _url(configuration.value("alphabet").toString() == "url"),
    _ignoreWhitespace(configuration.value("ignoreWhitespace").toBool()),
    _requirePadding(configuration.value("requirePadding").toBool())
{
}
QByteArray Base64Decoder::schema()
{
    return Base64::descriptor().schema(Codec::Mode::Inverse);
}

std::unique_ptr<Processor> Base64Decoder::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error))
    {
        return {};
    }
    auto configuration = input;
    if (!_stringOption(configuration, "alphabet", "standard", {"standard", "url"}, error)
        || !_boolOption(configuration, "ignoreWhitespace", true, error)
        || !_boolOption(configuration, "requirePadding", true, error))
    {
        return {};
    }
    return std::unique_ptr<Processor>(new Base64Decoder(std::move(configuration), limits));
}
void Base64Decoder::_process(const Chunk& input, QByteArray& output, QString& error)
{
    for (unsigned char byte : input.bytes)
    {
        if (_ignoreWhitespace && _whitespace(byte))
        {
            continue;
        }
        if (_ended)
        {
            if (byte == '=' && _paddingNeeded == 1)
            {
                _paddingNeeded = 0;
                if (!_decoded(output, 2, error))
                {
                    return;
                }
                continue;
            }
            _fail(&error, QStringLiteral("Base64 data follows padding"));
            return;
        }
        if (byte == '=')
        {
            if (_count == 2 && (_unit[1] & 15) == 0)
            {
                _ended         = true;
                _paddingNeeded = 1;
            }
            else if (_count == 3 && (_unit[2] & 3) == 0)
            {
                _ended = true;
                if (!_decoded(output, 3, error))
                {
                    return;
                }
            }
            else
            {
                _fail(&error, QStringLiteral("Base64 padding or unused bits are invalid"));
                return;
            }
            continue;
        }
        int value = -1;
        if (byte >= 'A' && byte <= 'Z')
        {
            value = byte - 'A';
        }
        else if (byte >= 'a' && byte <= 'z')
        {
            value = byte - 'a' + 26;
        }
        else if (byte >= '0' && byte <= '9')
        {
            value = byte - '0' + 52;
        }
        else if (byte == (_url ? '-' : '+'))
        {
            value = 62;
        }
        else if (byte == (_url ? '_' : '/'))
        {
            value = 63;
        }
        if (value < 0)
        {
            _fail(&error, QStringLiteral("Base64 input contains an invalid alphabet byte"));
            return;
        }
        _unit[_count++] = static_cast<unsigned char>(value);
        if (_count == 4)
        {
            if (!_decoded(output, 4, error))
            {
                return;
            }
        }
        else if (_count > _maximumBytes())
        {
            _fail(&error, QStringLiteral("Base64 pending input exceeds byte limit"));
            return;
        }
    }
    {
        if (_paddingNeeded != 0)
        {
            _fail(&error, QStringLiteral("Base64 packet ends with incomplete padding"));
        }
        else if (_count != 0)
        {
            if (_requirePadding || _count == 1)
            {
                _fail(&error, QStringLiteral("Base64 packet ends with an incomplete quartet"));
            }
            else if ((_count == 2 && (_unit[1] & 15) != 0) || (_count == 3 && (_unit[2] & 3) != 0))
            {
                _fail(&error, QStringLiteral("Base64 unused bits are nonzero"));
            }
            else
            {
                _decoded(output, _count, error);
            }
        }
    }
}
void Base64Decoder::_reset()
{
    _unit.fill(0);
    _count         = 0;
    _paddingNeeded = 0;
    _ended         = false;
}
bool Base64Decoder::_decoded(QByteArray& output, int count, QString& error)
{
    if (!_append(output, char((_unit[0] << 2) | (_unit[1] >> 4)), error))
    {
        return false;
    }
    if (count > 2 && !_append(output, char((_unit[1] << 4) | (_unit[2] >> 2)), error))
    {
        return false;
    }
    if (count > 3 && !_append(output, char((_unit[2] << 6) | _unit[3]), error))
    {
        return false;
    }
    _count = 0;
    return true;
}
