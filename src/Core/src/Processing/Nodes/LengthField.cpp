#include "LengthField.h"

#include <cmath>
#include <limits>

namespace
{
bool integerOption(QVariantMap&   configuration,
                   const QString& key,
                   int            fallback,
                   int            minimum,
                   int            maximum,
                   QString*       error)
{
    if (!configuration.contains(key))
    {
        configuration.insert(key, fallback);
    }

    const auto   option  = configuration.value(key);
    const auto   type    = option.typeId();
    const bool   numeric = type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
                        || type == QMetaType::ULongLong || type == QMetaType::Double || type == QMetaType::Float;
    const double number  = numeric ? option.toDouble() : 0;
    if (!numeric || !std::isfinite(number) || number < minimum || number > maximum || std::trunc(number) != number)
    {
        if (error)
        {
            *error = QStringLiteral("Length field option %1 must be an integer from %2 to %3")
                         .arg(key)
                         .arg(minimum)
                         .arg(maximum);
        }
        return false;
    }

    configuration.insert(key, int(number));
    return true;
}
} // namespace

LengthField::LengthField(const QVariantMap& configuration) :
    _encoding(configuration.value("encoding") == "varint" ? Encoding::Varint : Encoding::Fixed),
    _width(configuration.value("width").toInt()), _maxBytes(configuration.value("maxBytes").toInt()),
    _adjustment(configuration.value("adjustment").toInt()),
    _prefix(_encoding == Encoding::Varint || configuration.value("position") == "prefix"),
    _littleEndian(configuration.value("endianness") == "little"),
    _includeField(configuration.value("includeField").toBool())
{
}

QByteArray LengthField::schema()
{
    return R"(version: 1
type: termaster.settings-schema
description: Whole-packet binary length field settings
content:
  type: object
  properties:
    encoding:
      type: string
      title: Encoding
      default: fixed
      x-group: length
      x-order: 0
    includeField:
      type: boolean
      title: Include field length
      description: Count the length field itself in addition to payload bytes.
      default: false
      x-group: length
      x-order: 4
    adjustment:
      type: integer
      title: Length adjustment
      description: Signed value added after counting payload and optional field bytes.
      minimum: -2147483647
      maximum: 2147483647
      default: 0
      x-group: length
      x-order: 5
  oneOf:
    - title: Fixed
      required: [encoding]
      properties:
        encoding: {type: string, const: fixed}
        position:
          type: string
          title: Position
          description: Place the length field before or after the payload.
          enum: [prefix, suffix]
          default: prefix
          x-group: length
          x-order: 1
        width:
          type: integer
          title: Field width (bytes)
          description: Unsigned binary length field size.
          minimum: 1
          maximum: 8
          default: 2
          x-group: length
          x-order: 2
        endianness:
          type: string
          title: Endianness
          description: Byte order of the unsigned length field.
          enum: [little, big]
          default: little
          x-group: length
          x-order: 3
    - title: Varint
      required: [encoding]
      properties:
        encoding: {type: string, const: varint}
        maxBytes:
          type: integer
          title: Maximum bytes
          description: Maximum canonical unsigned ULEB128 prefix width (uint64, Protobuf encoding).
          minimum: 1
          maximum: 10
          default: 10
          x-group: length
          x-order: 1
  x-groups:
    - id: length
      title: Length field
)";
}

bool LengthField::validate(QVariantMap& configuration, QString* error)
{
    if (!configuration.contains("encoding"))
    {
        configuration.insert("encoding", "fixed");
    }
    const auto encoding = configuration.value("encoding");
    if (encoding.typeId() != QMetaType::QString || (encoding != "fixed" && encoding != "varint"))
    {
        if (error)
        {
            *error = QStringLiteral("Invalid length field encoding");
        }
        return false;
    }
    if (!integerOption(configuration, "adjustment", 0, -2147483647, 2147483647, error))
    {
        return false;
    }
    if (encoding == "varint")
    {
        configuration.remove("position");
        configuration.remove("width");
        configuration.remove("endianness");
        return integerOption(configuration, "maxBytes", 10, 1, 10, error);
    }

    configuration.remove("maxBytes");
    if (!integerOption(configuration, "width", 2, 1, 8, error))
    {
        return false;
    }
    return true;
}

bool LengthField::prefix() const
{
    return _prefix;
}

bool LengthField::value(qsizetype payloadBytes, quint64& result, int& width, QString& error) const
{
    if (_encoding == Encoding::Varint)
    {
        // The smallest self-consistent width is the canonical packet layout. Counting
        // the field may otherwise admit two valid integers at a seven-bit boundary.
        for (width = 1; width <= _maxBytes; ++width)
        {
            if (_value(payloadBytes, width, result) && _varintWidth(result) == width)
            {
                return true;
            }
        }
        error = QStringLiteral("Packet length has no canonical varint within the maximum bytes");
        return false;
    }

    width = _width;
    if (!_value(payloadBytes, width, result))
    {
        error = QStringLiteral("Packet length adjustment exceeds unsigned length range");
        return false;
    }
    if (_width < 8 && result >= (quint64(1) << (_width * 8)))
    {
        error = QStringLiteral("Packet length does not fit the %1-byte field").arg(_width);
        return false;
    }
    return true;
}

bool LengthField::_value(qsizetype payloadBytes, int width, quint64& result) const
{
    result              = quint64(payloadBytes);
    const qint64 offset = (_includeField ? width : 0) + qint64(_adjustment);
    if (payloadBytes < 0 || (offset < 0 && result < quint64(-offset))
        || (offset > 0 && result > std::numeric_limits<quint64>::max() - quint64(offset)))
    {
        return false;
    }
    result = offset < 0 ? result - quint64(-offset) : result + quint64(offset);
    return true;
}

int LengthField::_varintWidth(quint64 value)
{
    int width = 1;
    while (value >= 128)
    {
        value >>= 7;
        ++width;
    }
    return width;
}

QByteArray LengthField::encode(quint64 value) const
{
    if (_encoding == Encoding::Varint)
    {
        QByteArray bytes;
        do
        {
            const auto byte   = static_cast<unsigned char>(value & 0x7f);
            value           >>= 7;
            bytes.append(char(byte | (value ? 0x80 : 0)));
        } while (value);
        return bytes;
    }

    QByteArray bytes(_width, Qt::Uninitialized);
    for (int index = 0; index < _width; ++index)
    {
        bytes[_littleEndian ? index : _width - 1 - index]   = char(value & 0xff);
        value                                             >>= 8;
    }
    return bytes;
}

bool LengthField::decode(const QByteArray& packet,
                         quint64&          result,
                         int&              width,
                         qsizetype         maxSteps,
                         QString&          error) const
{
    result = 0;
    if (_encoding == Encoding::Varint)
    {
        for (width = 1; width <= _maxBytes && width <= packet.size(); ++width)
        {
            if (width > maxSteps)
            {
                error = QStringLiteral("Length field processing exceeds step limit");
                return false;
            }
            const auto byte = static_cast<unsigned char>(packet.at(width - 1));
            if (width == 10 && byte > 1)
            {
                error = QStringLiteral("Varint length exceeds unsigned 64-bit range");
                return false;
            }
            result |= quint64(byte & 0x7f) << ((width - 1) * 7);
            if (!(byte & 0x80))
            {
                if (_varintWidth(result) == width)
                {
                    return true;
                }
                error = QStringLiteral("Varint length is not canonical");
                return false;
            }
        }
        error = QStringLiteral("Varint length is unterminated or exceeds maximum bytes");
        return false;
    }

    width = _width;
    if (maxSteps < width || packet.size() < width)
    {
        error = maxSteps < width ? QStringLiteral("Length field processing exceeds step limit") :
                                   QStringLiteral("Packet is shorter than its length field");
        return false;
    }
    const auto* bytes = packet.constData() + (_prefix ? 0 : packet.size() - width);
    for (int index = 0; index < width; ++index)
    {
        result = (result << 8) | static_cast<unsigned char>(bytes[_littleEndian ? width - 1 - index : index]);
    }
    return true;
}
