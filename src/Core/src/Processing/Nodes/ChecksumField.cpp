#include "ChecksumField.h"

#include <cmath>
#include <limits>

namespace
{
bool fail(QString* error, const QString& message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

bool stringOption(QVariantMap&       configuration,
                  const QString&     key,
                  const QString&     fallback,
                  const QStringList& choices,
                  QString*           error)
{
    if (!configuration.contains(key))
    {
        configuration.insert(key, fallback);
    }
    const auto value = configuration.value(key);
    return (value.typeId() == QMetaType::QString && choices.contains(value.toString()))
        || fail(error, "Invalid checksum option: " + key);
}

bool integerOption(QVariantMap&   configuration,
                   const QString& key,
                   quint32        fallback,
                   quint32        minimum,
                   quint32        maximum,
                   QString*       error)
{
    if (!configuration.contains(key))
    {
        configuration.insert(key, qulonglong(fallback));
    }
    const auto   option  = configuration.value(key);
    const auto   type    = option.typeId();
    const bool   numeric = type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
                        || type == QMetaType::ULongLong || type == QMetaType::Double || type == QMetaType::Float;
    const double value   = numeric ? option.toDouble() : 0;
    if (!numeric || !std::isfinite(value) || std::trunc(value) != value || value < minimum || value > maximum)
    {
        return fail(error,
                    QStringLiteral("Checksum option %1 must be an integer from %2 to %3")
                        .arg(key)
                        .arg(minimum)
                        .arg(maximum));
    }

    configuration.insert(key, qulonglong(value));
    return true;
}
} // namespace

ChecksumField::ChecksumField(const QVariantMap& configuration) :
    _algorithm(configuration.value("algorithm") == "sum8" ? Algorithm::Sum :
               configuration.value("algorithm") == "xor8" ? Algorithm::Xor :
                                                            Algorithm::Crc),
    _parameters(_parametersFor(configuration)),
    _mask(_parameters.width == 32 ? std::numeric_limits<quint32>::max() : (quint32(1) << _parameters.width) - 1),
    _prefix(configuration.value("position") == "prefix"),
    _littleEndian(configuration.value("endianness", "little") == "little")
{
    if (_algorithm != Algorithm::Crc)
    {
        return;
    }

    const auto polynomial = _parameters.refin ? _reflect(_parameters.polynomial, _parameters.width) :
                                                _parameters.polynomial;
    const auto topBit     = quint32(1) << (_parameters.width - 1);
    for (quint32 index = 0; index < _table.size(); ++index)
    {
        auto value = _parameters.refin ? index : index << (_parameters.width - 8);
        for (int bit = 0; bit < 8; ++bit)
        {
            value = _parameters.refin ? ((value >> 1) ^ ((value & 1) ? polynomial : 0)) :
                                        ((value << 1) ^ ((value & topBit) ? polynomial : 0));
        }
        _table[index] = value & _mask;
    }
}

bool ChecksumField::validate(QVariantMap& configuration, QString* error)
{
    if (!stringOption(configuration,
                      "algorithm",
                      "crc16-modbus",
                      {"sum8",
                       "xor8",
                       "crc8-smbus",
                       "crc16-modbus",
                       "crc16-ibm3740",
                       "crc32-iso-hdlc",
                       "crc32-iscsi",
                       "crc-custom"},
                      error)
        || !stringOption(configuration, "position", "suffix", {"suffix", "prefix"}, error))
    {
        return false;
    }

    const auto algorithm = configuration.value("algorithm").toString();
    if (algorithm == "sum8" || algorithm == "xor8" || algorithm == "crc8-smbus")
    {
        configuration.remove("endianness");
    }
    else if (!stringOption(configuration, "endianness", "little", {"little", "big"}, error))
    {
        return false;
    }
    if (algorithm != "crc-custom")
    {
        for (const auto* key : {"width", "polynomial", "init", "refin", "refout", "xorout"})
        {
            configuration.remove(key);
        }
        return true;
    }

    if (!integerOption(configuration, "width", 16, 8, 32, error))
    {
        return false;
    }
    const auto width = configuration.value("width").toInt();
    if (width != 8 && width != 16 && width != 32)
    {
        return fail(error, QStringLiteral("Custom CRC width must be 8, 16 or 32 bits"));
    }
    const auto mask = width == 32 ? std::numeric_limits<quint32>::max() : (quint32(1) << width) - 1;
    if (!integerOption(configuration, "polynomial", 0x8005, 1, mask, error)
        || !integerOption(configuration, "init", 0xffff, 0, mask, error)
        || !integerOption(configuration, "xorout", 0, 0, mask, error))
    {
        return false;
    }
    for (const auto* key : {"refin", "refout"})
    {
        if (!configuration.contains(key))
        {
            configuration.insert(key, true);
        }
        if (configuration.value(key).typeId() != QMetaType::Bool)
        {
            return fail(error, "Checksum option must be boolean: " + QString(key));
        }
    }
    return true;
}

int ChecksumField::width() const
{
    return _parameters.width / 8;
}

bool ChecksumField::prefix() const
{
    return _prefix;
}

QByteArray ChecksumField::encode(QByteArrayView payload) const
{
    auto       value = _checksum(payload);
    QByteArray field(width(), Qt::Uninitialized);
    for (int index = 0; index < field.size(); ++index)
    {
        field[_littleEndian ? index : field.size() - 1 - index]   = char(value & 0xff);
        value                                                   >>= 8;
    }
    return field;
}

ChecksumField::Parameters ChecksumField::_parametersFor(const QVariantMap& configuration)
{
    const auto algorithm = configuration.value("algorithm").toString();
    if (algorithm == "sum8" || algorithm == "xor8" || algorithm == "crc8-smbus")
    {
        return {8, 0x07, 0, false, false, 0};
    }
    if (algorithm == "crc16-ibm3740")
    {
        return {16, 0x1021, 0xffff, false, false, 0};
    }
    if (algorithm == "crc32-iso-hdlc")
    {
        return {32, 0x04c11db7, 0xffffffff, true, true, 0xffffffff};
    }
    if (algorithm == "crc32-iscsi")
    {
        return {32, 0x1edc6f41, 0xffffffff, true, true, 0xffffffff};
    }
    if (algorithm == "crc-custom")
    {
        return {configuration.value("width").toInt(),
                configuration.value("polynomial").toUInt(),
                configuration.value("init").toUInt(),
                configuration.value("refin").toBool(),
                configuration.value("refout").toBool(),
                configuration.value("xorout").toUInt()};
    }
    return {};
}

quint32 ChecksumField::_reflect(quint32 value, int width)
{
    quint32 reflected = 0;
    for (int bit = 0; bit < width; ++bit)
    {
        reflected   = (reflected << 1) | (value & 1);
        value     >>= 1;
    }
    return reflected;
}

quint32 ChecksumField::_checksum(QByteArrayView payload) const
{
    quint32 value = _algorithm == Algorithm::Crc ? _parameters.init : 0;
    if (_algorithm == Algorithm::Crc && _parameters.refin)
    {
        value = _reflect(value, _parameters.width);
    }
    if (_algorithm == Algorithm::Sum)
    {
        for (const auto byte : payload)
        {
            value = (value + static_cast<unsigned char>(byte)) & 0xff;
        }
    }
    else if (_algorithm == Algorithm::Xor)
    {
        for (const auto byte : payload)
        {
            value ^= static_cast<unsigned char>(byte);
        }
    }
    else if (_parameters.refin)
    {
        for (const auto byte : payload)
        {
            value = (value >> 8) ^ _table[(value ^ static_cast<unsigned char>(byte)) & 0xff];
        }
    }
    else
    {
        for (const auto byte : payload)
        {
            value = ((value << 8)
                     ^ _table[((value >> (_parameters.width - 8)) ^ static_cast<unsigned char>(byte)) & 0xff])
                  & _mask;
        }
    }
    if (_algorithm == Algorithm::Crc && _parameters.refout != _parameters.refin)
    {
        value = _reflect(value, _parameters.width);
    }
    return (value ^ (_algorithm == Algorithm::Crc ? _parameters.xorout : 0)) & _mask;
}
