#include "HexDecoder.h"

HexDecoder::HexDecoder(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _ignoreWhitespace(configuration.value("ignoreWhitespace").toBool())
{
}
QByteArray HexDecoder::schema()
{
    return _schema("    ignoreWhitespace: {type: boolean, title: Ignore ASCII whitespace, default: true}\n");
}
std::unique_ptr<Processor> HexDecoder::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error))
    {
        return {};
    }
    auto configuration = input;
    if (!_boolOption(configuration, "ignoreWhitespace", true, error))
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
    if (input.boundary == Boundary::Packet && _nibble >= 0)
    {
        _fail(&error, QStringLiteral("HEX packet ends with an incomplete byte"));
    }
}
void HexDecoder::_reset()
{
    _nibble = -1;
}
