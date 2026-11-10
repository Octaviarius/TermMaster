#include "StuffingSettings.h"

#include <cmath>

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

bool byteOption(QVariantMap& configuration, const QString& key, int fallback, QString* error)
{
    if (!configuration.contains(key))
    {
        configuration.insert(key, fallback);
    }
    const auto   option  = configuration.value(key);
    const auto   type    = option.typeId();
    const bool   numeric = type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
                        || type == QMetaType::ULongLong || type == QMetaType::Double || type == QMetaType::Float;
    const double value   = numeric ? option.toDouble() : 0;
    if (!numeric || !std::isfinite(value) || std::trunc(value) != value || value < 0 || value > 255)
    {
        return fail(error, QStringLiteral("Stuffing option %1 must be an integer from 0 to 255").arg(key));
    }

    configuration.insert(key, int(value));
    return true;
}
} // namespace

StuffingSettings::StuffingSettings(const QVariantMap& configuration) :
    _delimiter(configuration.value("delimiter").toUInt()), _escape(configuration.value("escape").toUInt()),
    _delimiterCode(configuration.value("delimiterCode").toUInt()),
    _escapeCode(configuration.value("escapeCode").toUInt()),
    _boundaries(configuration.value("boundaryMode") == "both"   ? 2 :
                configuration.value("boundaryMode") == "suffix" ? 1 :
                                                                  0)
{
}

QByteArray StuffingSettings::schema()
{
    return R"SCHEMA(version: 1
type: termaster.settings-schema
description: Whole-packet fixed-pair byte stuffing settings
content:
  type: object
  x-groups:
    - {id: stuffing, title: Byte stuffing, order: 0}
  properties:
    boundaryMode:
      type: string
      title: Boundary mode
      description: Suffix adds/requires a trailing delimiter; both also adds/requires a leading delimiter. Use none when a Framer has already removed the delimiter.
      enum: [suffix, both, none]
      default: suffix
      x-group: stuffing
      x-order: 0
    delimiter:
      type: integer
      title: Delimiter
      description: Reserved boundary byte, in hexadecimal; must differ from escape and both replacement codes.
      minimum: 0
      maximum: 255
      default: 192
      x-widget: hexInteger
      x-group: stuffing
      x-order: 1
    escape:
      type: integer
      title: Escape
      description: Introduces exactly one replacement code, in hexadecimal.
      minimum: 0
      maximum: 255
      default: 219
      x-widget: hexInteger
      x-group: stuffing
      x-order: 2
    delimiterCode:
      type: integer
      title: Delimiter replacement
      description: Code following escape for a payload delimiter, in hexadecimal; must differ from the escape replacement code.
      minimum: 0
      maximum: 255
      default: 220
      x-widget: hexInteger
      x-group: stuffing
      x-order: 3
    escapeCode:
      type: integer
      title: Escape replacement
      description: Code following escape for a payload escape byte, in hexadecimal; may equal escape.
      minimum: 0
      maximum: 255
      default: 221
      x-widget: hexInteger
      x-group: stuffing
      x-order: 4
)SCHEMA";
}

bool StuffingSettings::validate(QVariantMap& configuration, QString* error)
{
    if (!byteOption(configuration, "delimiter", 192, error) || !byteOption(configuration, "escape", 219, error)
        || !byteOption(configuration, "delimiterCode", 220, error)
        || !byteOption(configuration, "escapeCode", 221, error))
    {
        return false;
    }
    if (!configuration.contains("boundaryMode"))
    {
        configuration.insert("boundaryMode", "suffix");
    }
    const auto mode = configuration.value("boundaryMode");
    if (mode.typeId() != QMetaType::QString || !QStringList {"none", "suffix", "both"}.contains(mode.toString()))
    {
        return fail(error, QStringLiteral("Stuffing boundaryMode must be none, suffix or both"));
    }
    if (configuration.value("delimiter") == configuration.value("escape"))
    {
        return fail(error, QStringLiteral("Stuffing delimiter and escape must differ"));
    }
    if (configuration.value("delimiterCode") == configuration.value("escapeCode"))
    {
        return fail(error, QStringLiteral("Stuffing replacement codes must differ"));
    }
    if (configuration.value("delimiterCode") == configuration.value("delimiter")
        || configuration.value("escapeCode") == configuration.value("delimiter"))
    {
        return fail(error, QStringLiteral("Stuffing replacement codes must differ from delimiter"));
    }
    return true;
}

unsigned char StuffingSettings::delimiter() const
{
    return _delimiter;
}

unsigned char StuffingSettings::escape() const
{
    return _escape;
}

unsigned char StuffingSettings::delimiterCode() const
{
    return _delimiterCode;
}

unsigned char StuffingSettings::escapeCode() const
{
    return _escapeCode;
}

int StuffingSettings::boundaries() const
{
    return _boundaries;
}
