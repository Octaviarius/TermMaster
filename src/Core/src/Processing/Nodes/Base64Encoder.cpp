#include "Base64Encoder.h"

#include <cmath>

Base64Encoder::Base64Encoder(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits),
    _lineEnding(configuration.value("lineEnding").toString() == "crlf" ? QByteArray("\r\n") : QByteArray("\n")),
    _lineLength(configuration.value("lineLength").toInt()), _url(configuration.value("alphabet").toString() == "url"),
    _padding(configuration.value("padding").toBool())
{
}
QByteArray Base64Encoder::schema()
{
    return _schema(
        "    alphabet: {type: string, title: Alphabet, enum: [standard, url], default: standard}\n    padding: {type: boolean, title: Emit padding, default: true}\n    lineLength: {type: integer, title: Line length, minimum: 0, maximum: 4096, multipleOf: 4, default: 0}\n    lineEnding: {type: string, title: Line ending, enum: [lf, crlf], default: lf}\n");
}
std::unique_ptr<Processor> Base64Encoder::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error))
    {
        return {};
    }
    auto configuration = input;
    if (!_stringOption(configuration, "alphabet", "standard", {"standard", "url"}, error)
        || !_boolOption(configuration, "padding", true, error)
        || !_stringOption(configuration, "lineEnding", "lf", {"lf", "crlf"}, error))
    {
        return {};
    }
    if (!configuration.contains("lineLength"))
    {
        configuration.insert("lineLength", 0);
    }
    const auto   value   = configuration.value("lineLength");
    const auto   type    = value.typeId();
    const bool   numeric = type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
                        || type == QMetaType::ULongLong || type == QMetaType::Double;
    const double length  = numeric ? value.toDouble() : -1;
    if (!numeric || !std::isfinite(length) || length < 0 || length > 4096 || std::fmod(length, 4) != 0)
    {
        _fail(error, QStringLiteral("Base64 line length must be a multiple of four from 0 to 4096"));
        return {};
    }
    configuration.insert("lineLength", int(length));
    return std::unique_ptr<Processor>(new Base64Encoder(std::move(configuration), limits));
}
void Base64Encoder::_process(const Chunk& input, QByteArray& output, QString& error)
{
    for (unsigned char byte : input.bytes)
    {
        _unit[_count++] = byte;
        if (_count == 3)
        {
            if (!_quantum(output, 3, error))
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
    if (input.boundary == Boundary::Packet && _count != 0)
    {
        _quantum(output, _count, error);
    }
}
void Base64Encoder::_reset()
{
    _unit.fill(0);
    _count  = 0;
    _column = 0;
}
bool Base64Encoder::_encoded(QByteArray& output, char byte, QString& error)
{
    if (_lineLength && _column == _lineLength)
    {
        if (!_append(output, _lineEnding, error))
        {
            return false;
        }
        _column = 0;
    }
    if (!_append(output, byte, error))
    {
        return false;
    }
    if (_lineLength)
    {
        ++_column;
    }
    return true;
}
bool Base64Encoder::_quantum(QByteArray& output, int count, QString& error)
{
    const char*        alphabet = _url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_" :
                                         "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned int value    = (unsigned(_unit[0]) << 16) | (count > 1 ? unsigned(_unit[1]) << 8 : 0)
                                | (count > 2 ? unsigned(_unit[2]) : 0);
    if (!_encoded(output, alphabet[(value >> 18) & 63], error)
        || !_encoded(output, alphabet[(value >> 12) & 63], error))
    {
        return false;
    }
    if (count > 1)
    {
        if (!_encoded(output, alphabet[(value >> 6) & 63], error))
        {
            return false;
        }
    }
    else if (_padding && !_encoded(output, '=', error))
    {
        return false;
    }
    if (count > 2)
    {
        if (!_encoded(output, alphabet[value & 63], error))
        {
            return false;
        }
    }
    else if (_padding && !_encoded(output, '=', error))
    {
        return false;
    }
    _count = 0;
    return true;
}
