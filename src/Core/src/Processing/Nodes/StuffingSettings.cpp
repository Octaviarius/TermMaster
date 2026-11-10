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
