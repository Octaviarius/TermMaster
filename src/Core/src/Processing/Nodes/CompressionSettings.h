#pragma once

#include <QVariantMap>
#include <cmath>

/** Shared validation for the independent DEFLATE packet nodes. */
class CompressionSettings final
{
public:
    static bool integerOption(QVariantMap&   configuration,
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

        const auto   value   = configuration.value(key);
        const auto   type    = value.typeId();
        const bool   numeric = type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
                            || type == QMetaType::ULongLong || type == QMetaType::Double;
        const double number  = numeric ? value.toDouble() : 0;
        if (!numeric || !std::isfinite(number) || number < minimum || number > maximum || std::trunc(number) != number)
        {
            if (error)
            {
                *error = QStringLiteral("Processing node option %1 must be an integer from %2 to %3")
                             .arg(key)
                             .arg(minimum)
                             .arg(maximum);
            }
            return false;
        }

        configuration.insert(key, int(number));
        return true;
    }

    static int wrappedWindowBits(const QVariantMap& configuration, bool autoDetect = false)
    {
        const auto format = configuration.value("format").toString();
        const auto bits   = configuration.value("windowBits").toInt();
        if (format == "raw")
        {
            return -bits;
        }
        if (autoDetect)
        {
            return bits + 32;
        }
        if (format == "gzip")
        {
            return bits + 16;
        }
        return bits;
    }
};
