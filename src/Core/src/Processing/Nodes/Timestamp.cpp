#include "Timestamp.h"

#include <QDateTime>
#include <QTimeZone>
#include <chrono>
#include <limits>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <time.h>
#endif

Timestamp::Timestamp(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _format(configuration.value("format").toString()),
    _utc(configuration.value("timeZone").toString() == "utc")
{
}

QByteArray Timestamp::schema()
{
    return R"(version: 1
type: termaster.settings-schema
description: Per-packet timestamp settings
content:
  type: object
  x-widget: timestampPreview
  properties:
    format:
      type: string
      title: Datetime format
      description: "Qt date/time format for {datetime}: yyyy-MM-dd HH:mm:ss.zzz. Single-quoted words are literal. Unused variables are not evaluated."
      maxLength: 512
      default: 'yyyy-MM-dd HH:mm:ss.zzz'
      x-group: timestamp
      x-order: 1
    timeZone:
      type: string
      title: Time zone
      description: 'Local or UTC for {datetime}. Unix and boot-relative clocks are unaffected.'
      enum: [local, utc]
      default: local
      x-group: timestamp
      x-order: 2
    template:
      type: string
      title: Output template
      description: 'Fields: {datetime}, {unix:.6f}, {unix_ms}, {unix_us}, {unix_ns}, {steady:.6f}, {steady_ms}, {steady_us}, {steady_ns}, {index:03}, {packet}. steady measures boot-relative time including sleep. Braces: {{ }}. Byte escapes: \e \r \n \t \0 \\ \xHH. ANSI example: \e[90m[{datetime}]\e[0m {packet}.'
      maxLength: 4096
      default: '[{datetime}] {packet}'
      x-group: timestamp
      x-order: 3
  x-groups:
    - id: timestamp
      title: Timestamp
)";
}

std::unique_ptr<Processor> Timestamp::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error))
    {
        return {};
    }

    auto configuration = input;
    if (!_stringOption(configuration, "timeZone", "local", {"local", "utc"}, error))
    {
        return {};
    }
    if (!configuration.contains("format"))
    {
        configuration.insert("format", QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    }
    const auto format = configuration.value("format");
    if (format.typeId() != QMetaType::QString || format.toString().size() > 512)
    {
        _fail(error, QStringLiteral("Datetime format must be a string of at most 512 characters"));
        return {};
    }
    if (!configuration.contains("template"))
    {
        configuration.insert("template", QStringLiteral("[{datetime}] {packet}"));
    }
    const auto text = configuration.value("template");
    if (text.typeId() != QMetaType::QString || text.toString().size() > 4096)
    {
        _fail(error, QStringLiteral("Timestamp template must be a string of at most 4096 characters"));
        return {};
    }

    auto result       = std::unique_ptr<Timestamp>(new Timestamp(configuration, limits));
    result->_compiled = CompiledFormat::compile(text.toString(), result->_schema, error);
    if (!result->_compiled)
    {
        return {};
    }
    result->_values.emplace(result->_schema.createValues());
    return result;
}

qint64 Timestamp::_bootNanoseconds(QString& error)
{
#ifdef Q_OS_WIN
    ULONGLONG ticks = 0;
    QueryInterruptTimePrecise(&ticks);
    if (ticks <= quint64(std::numeric_limits<qint64>::max() / 100))
    {
        return qint64(ticks * 100);
    }
#elif defined(Q_OS_LINUX)
    timespec value {};
    if (clock_gettime(CLOCK_BOOTTIME, &value) == 0 && value.tv_sec >= 0
        && value.tv_sec <= (std::numeric_limits<qint64>::max() - value.tv_nsec) / 1000000000)
    {
        return qint64(value.tv_sec) * 1000000000 + value.tv_nsec;
    }
#endif
    error = QStringLiteral("Boot-relative clock is unavailable or outside its supported range");
    return -1;
}

void Timestamp::_process(const Chunk& input, QByteArray& output, QString& error)
{
    if (_packetIndex == std::numeric_limits<quint64>::max())
    {
        error = QStringLiteral("Timestamp packet index exhausted");
        return;
    }
    _values->set(_index, ++_packetIndex);
    if (_compiled->uses(_packet))
    {
        _values->set(_packet, QByteArrayView(input.bytes));
    }

    using namespace std::chrono;
    if (_compiled->uses(_datetime) || _compiled->uses(_unix) || _compiled->uses(_unixMs) || _compiled->uses(_unixUs)
        || _compiled->uses(_unixNs))
    {
        const auto wall = system_clock::now().time_since_epoch();
        if (_compiled->uses(_datetime))
        {
            auto date = QDateTime::fromMSecsSinceEpoch(duration_cast<milliseconds>(wall).count(), QTimeZone::UTC);
            if (!_utc)
            {
                date = date.toLocalTime();
            }
            _values->set(_datetime, date.toString(_format));
        }
        if (_compiled->uses(_unix))
        {
            _values->set(_unix, duration<double>(wall).count());
        }
        if (_compiled->uses(_unixMs))
        {
            _values->set(_unixMs, qint64(duration_cast<milliseconds>(wall).count()));
        }
        if (_compiled->uses(_unixUs))
        {
            _values->set(_unixUs, qint64(duration_cast<microseconds>(wall).count()));
        }
        if (_compiled->uses(_unixNs))
        {
            _values->set(_unixNs, qint64(duration_cast<nanoseconds>(wall).count()));
        }
    }
    if (_compiled->uses(_steady) || _compiled->uses(_steadyMs) || _compiled->uses(_steadyUs)
        || _compiled->uses(_steadyNs))
    {
        const auto ns = _bootNanoseconds(error);
        if (!error.isEmpty())
        {
            _values->clear();
            return;
        }
        if (_compiled->uses(_steady))
        {
            _values->set(_steady, double(ns) / 1000000000.0);
        }
        if (_compiled->uses(_steadyMs))
        {
            _values->set(_steadyMs, ns / 1000000);
        }
        if (_compiled->uses(_steadyUs))
        {
            _values->set(_steadyUs, ns / 1000);
        }
        if (_compiled->uses(_steadyNs))
        {
            _values->set(_steadyNs, ns);
        }
    }

    _compiled->render(*_values, output, _maximumBytes(), &error);
    _values->clear();
}

void Timestamp::_reset()
{
    if (_values)
    {
        _values->clear();
    }
}
