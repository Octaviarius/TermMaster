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
    _width(configuration.value("width").toInt()), _adjustment(configuration.value("adjustment").toInt()),
    _prefix(configuration.value("position") == "prefix"), _littleEndian(configuration.value("endianness") == "little"),
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
      enum: [1, 2, 4, 8]
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
  x-groups:
    - id: length
      title: Length field
)";
}

bool LengthField::validate(QVariantMap& configuration, QString* error)
{
    if (!integerOption(configuration, "width", 2, 1, 8, error)
        || !integerOption(configuration, "adjustment", 0, -2147483647, 2147483647, error))
    {
        return false;
    }
    const auto width = configuration.value("width").toInt();
    if (width != 1 && width != 2 && width != 4 && width != 8)
    {
        if (error)
        {
            *error = QStringLiteral("Length field width must be 1, 2, 4 or 8 bytes");
        }
        return false;
    }
    return true;
}

int LengthField::width() const
{
    return _width;
}

bool LengthField::prefix() const
{
    return _prefix;
}

bool LengthField::value(qsizetype payloadBytes, quint64& result, QString& error) const
{
    result              = quint64(payloadBytes);
    const qint64 offset = (_includeField ? _width : 0) + qint64(_adjustment);
    if ((offset < 0 && result < quint64(-offset))
        || (offset > 0 && result > std::numeric_limits<quint64>::max() - quint64(offset)))
    {
        error = QStringLiteral("Packet length adjustment exceeds unsigned length range");
        return false;
    }
    result = offset < 0 ? result - quint64(-offset) : result + quint64(offset);

    if (_width < 8 && result >= (quint64(1) << (_width * 8)))
    {
        error = QStringLiteral("Packet length does not fit the %1-byte field").arg(_width);
        return false;
    }
    return true;
}

QByteArray LengthField::encode(quint64 value) const
{
    QByteArray bytes(_width, Qt::Uninitialized);
    for (int index = 0; index < _width; ++index)
    {
        bytes[_littleEndian ? index : _width - 1 - index]   = char(value & 0xff);
        value                                             >>= 8;
    }
    return bytes;
}

quint64 LengthField::decode(const char* bytes) const
{
    quint64 value = 0;
    for (int index = 0; index < _width; ++index)
    {
        value = (value << 8) | static_cast<unsigned char>(bytes[_littleEndian ? _width - 1 - index : index]);
    }
    return value;
}
