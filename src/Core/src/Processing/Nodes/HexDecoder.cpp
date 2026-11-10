#include "HexDecoder.h"

#include "Hex.h"

HexDecoder::HexDecoder(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _ignoreWhitespace(configuration.value("ignoreWhitespace").toBool()),
    _accumulate(configuration.value("accumulate").toBool())
{
}
QByteArray HexDecoder::schema()
{
    return Hex::descriptor().schema(Codec::Mode::Inverse);
}

std::unique_ptr<Processor> HexDecoder::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error))
    {
        return {};
    }
    auto configuration = input;
    if (!_boolOption(configuration, "ignoreWhitespace", true, error)
        || !_boolOption(configuration, "accumulate", false, error))
    {
        return {};
    }
    return std::unique_ptr<Processor>(new HexDecoder(std::move(configuration), limits));
}
void HexDecoder::_process(const Chunk& input, QByteArray& output, QString& error)
{
    for (unsigned char byte : input.bytes)
    {
        if (_ignoreWhitespace && _whitespace(byte))
        {
            continue;
        }
        const int value = byte >= '0' && byte <= '9' ? byte - '0' :
                          byte >= 'a' && byte <= 'f' ? byte - 'a' + 10 :
                          byte >= 'A' && byte <= 'F' ? byte - 'A' + 10 :
                                                       -1;
        if (value < 0)
        {
            _fail(&error, QStringLiteral("HEX input contains a non-hexadecimal byte"));
            return;
        }
        if (_nibble < 0)
        {
            _nibble = value;
        }
        else
        {
            if (!_append(output, char((_nibble << 4) | value), error))
            {
                return;
            }
            _nibble = -1;
        }
    }
    if (!_accumulate && _nibble >= 0)
    {
        _fail(&error, QStringLiteral("HEX packet ends with an incomplete byte"));
    }
}
void HexDecoder::_reset()
{
    _nibble = -1;
}

bool HexDecoder::_retainPacketState() const
{
    return _accumulate;
}

bool HexDecoder::_emitEmptyPacket() const
{
    return _nibble < 0;
}
