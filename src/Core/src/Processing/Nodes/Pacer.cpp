#include "Pacer.h"

#include <QPointer>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace
{
constexpr qint64 secondNs = 1000000000;

qint64 boundedNs(long double value)
{
    return qint64(std::min(value, static_cast<long double>(std::numeric_limits<qint64>::max() / 2)));
}

int hexDigit(QChar value)
{
    const auto code = value.unicode();
    if (code >= '0' && code <= '9')
    {
        return code - '0';
    }
    if (code >= 'a' && code <= 'f')
    {
        return code - 'a' + 10;
    }
    if (code >= 'A' && code <= 'F')
    {
        return code - 'A' + 10;
    }
    return -1;
}

std::optional<QByteArray> decodeEnding(const QString& text, QString* error)
{
    const auto reject = [&](const char* message) -> std::optional<QByteArray> {
        if (error)
        {
            *error = QString::fromLatin1(message);
        }
        return {};
    };
    if (text.isEmpty() || text.size() > 1024)
    {
        return reject("Pacer ending must decode to 1 to 256 bytes");
    }

    QByteArray result;
    for (qsizetype index = 0; index < text.size();)
    {
        if (text[index] != '\\')
        {
            const auto start = index;
            while (index < text.size() && text[index] != '\\')
            {
                ++index;
            }
            result.append(text.mid(start, index - start).toUtf8());
        }
        else
        {
            if (++index == text.size())
            {
                return reject("Pacer ending has an incomplete escape");
            }
            const auto escaped = text[index++];
            if (escaped == 'r')
            {
                result.append('\r');
            }
            else if (escaped == 'n')
            {
                result.append('\n');
            }
            else if (escaped == '0')
            {
                result.append('\0');
            }
            else if (escaped == '\\')
            {
                result.append('\\');
            }
            else if (escaped == 'x')
            {
                if (index + 1 >= text.size())
                {
                    return reject("Pacer hexadecimal escape requires two digits");
                }
                const auto high = hexDigit(text[index]);
                const auto low  = hexDigit(text[index + 1]);
                if (high < 0 || low < 0)
                {
                    return reject("Pacer ending has an invalid hexadecimal escape");
                }
                result.append(char((high << 4) | low));
                index += 2;
            }
            else
            {
                return reject("Pacer ending contains an unsupported escape");
            }
        }
        if (result.size() > 256)
        {
            return reject("Pacer ending must decode to 1 to 256 bytes");
        }
    }
    return result;
}
} // namespace

Pacer::Pacer(QVariantMap configuration, Limits limits, QByteArray ending) :
    _configuration(std::move(configuration)), _limits(limits), _ending(std::move(ending)),
    _byteDelayNs(_configuration.value("byteDelayMs").toLongLong() * 1000000),
    _lineDelayNs(_configuration.value("lineDelayMs").toLongLong() * 1000000),
    _rate(_configuration.value("bytesPerSecond").toLongLong()),
    _burst(std::min(qsizetype(_configuration.value("burstBytes").toInt()), _limits.maxBytes)), _timer(this)
{
    _prefix.resize(size_t(_ending.size()));
    qsizetype matched = 0;
    for (qsizetype index = 1; index < _ending.size(); ++index)
    {
        while (matched > 0 && _ending[index] != _ending[matched])
        {
            matched = _prefix[size_t(matched - 1)];
        }
        if (_ending[index] == _ending[matched])
        {
            ++matched;
        }
        _prefix[size_t(index)] = matched;
    }

    _clock.start();
    _timer.setSingleShot(true);
    _timer.setTimerType(Qt::PreciseTimer);
    connect(&_timer, &QTimer::timeout, this, [this] { _pump(); });
}

QVariantMap Pacer::configuration() const
{
    return _configuration;
}

Processor::AcceptResult Pacer::accept(Chunk input, Callbacks callbacks)
{
    if (_stopped || input.port != 0)
    {
        return AcceptResult::Rejected;
    }
    if (_busy)
    {
        return AcceptResult::WouldBlock;
    }
    if (input.bytes.size() > _limits.maxBytes)
    {
        return AcceptResult::Rejected;
    }

    _busy       = true;
    _callbacks  = std::move(callbacks);
    // An input may use fromRawData: detach by copying the raw bytes before returning.
    input.bytes = QByteArray(input.bytes.constData(), input.bytes.size());
    _input      = std::move(input);
    _offset     = 0;
    _planned    = false;
    _pump();
    return AcceptResult::Accepted;
}

void Pacer::cancel(std::function<void()> quiescent)
{
    _stopped = true;
    _timer.stop();
    _callbacks = {};
    _input     = {};
    _busy      = false;
    _planned   = false;
    _matched   = 0;
    _offset    = 0;
    if (quiescent)
    {
        quiescent();
    }
}

Pacer::Fragment Pacer::_nextFragment(const QByteArray& bytes, qsizetype offset, qsizetype matched) const
{
    Fragment fragment;
    fragment.matched     = matched;
    const auto remaining = bytes.size() - offset;
    const auto maximum   = _byteDelayNs > 0 ? std::min(qsizetype(1), remaining) :
                           _rate > 0        ? std::min(_burst, remaining) :
                                              remaining;
    if (_lineDelayNs == 0)
    {
        fragment.length  = maximum;
        fragment.matched = matched;
        return fragment;
    }

    for (; fragment.length < maximum;)
    {
        const auto byte = bytes[offset + fragment.length++];
        while (fragment.matched > 0 && byte != _ending[fragment.matched])
        {
            fragment.matched = _prefix[size_t(fragment.matched - 1)];
        }
        if (byte == _ending[fragment.matched])
        {
            ++fragment.matched;
        }
        if (fragment.matched == _ending.size())
        {
            ++fragment.endings;
            fragment.matched = 0;
            break;
        }
    }
    return fragment;
}

void Pacer::_pump()
{
    for (int event = 0; event < _limits.eventBudget; ++event)
    {
        if (_stopped || !_busy)
        {
            return;
        }
        const auto now = _clock.nsecsElapsed();
        if (!_planned)
        {
            _fragment        = _nextFragment(_input.bytes, _offset, _matched);
            _readyNs         = _timingReadyNs;
            _plannedFinishNs = _rateFinishNs;
            if (_rate > 0 && _fragment.length > 0)
            {
                // Virtual finish time implements a bounded token bucket without idle credits.
                const auto duration    = std::ceil(static_cast<long double>(_fragment.length) * secondNs / _rate);
                _plannedFinishNs       = boundedNs(std::max(_rateFinishNs, now) + duration);
                const auto burstWindow = boundedNs(std::ceil(static_cast<long double>(_burst) * secondNs / _rate));
                _readyNs               = std::max(_readyNs, _plannedFinishNs - burstWindow);
            }
            _planned = true;
        }
        if (_readyNs > now)
        {
            const auto waitMs = std::min(qint64(std::numeric_limits<int>::max()), (_readyNs - now + 999999) / 1000000);
            _timer.start(int(waitMs));
            return;
        }

        Chunk output;
        output.bytes                    = QByteArray(_input.bytes.constData() + _offset, _fragment.length);
        output.port                     = _input.port;
        _offset                        += _fragment.length;
        _matched                        = _fragment.matched;
        _rateFinishNs                   = _plannedFinishNs;
        const auto linePause            = boundedNs(static_cast<long double>(_lineDelayNs) * _fragment.endings);
        _timingReadyNs                  = boundedNs(static_cast<long double>(now) + std::max(_byteDelayNs, linePause));
        _planned                        = false;
        const bool      finished        = _offset == _input.bytes.size();
        auto            outputCallback  = _callbacks.output;
        QPointer<Pacer> guard(this);
        if (outputCallback)
        {
            outputCallback(std::move(output));
            if (!guard || guard->_stopped)
            {
                return;
            }
        }
        if (finished)
        {
            _complete();
            return;
        }
    }
    _timer.start(0);
}

void Pacer::_complete(QString error)
{
    auto completed = std::move(_callbacks.completed);
    _callbacks     = {};
    _input         = {};
    _busy          = false;
    _planned       = false;
    if (completed)
    {
        completed(std::move(error));
    }
}

QByteArray Pacer::schema()
{
    return R"(version: 1
type: termaster.settings-schema
description: Asynchronous byte and packet pacing settings
content:
  type: object
  x-groups:
    - {id: timing, title: Timing, order: 0}
  properties:
    byteDelayMs:
      type: integer
      minimum: 0
      maximum: 60000
      default: 0
      title: Delay between bytes (ms)
      description: Splits input packets into one-byte output packets.
      x-widget: integer
      x-group: timing
      x-order: 0
    lineDelayMs:
      type: integer
      minimum: 0
      maximum: 60000
      default: 0
      title: Delay after line ending (ms)
      x-widget: integer
      x-group: timing
      x-order: 1
    ending:
      type: string
      default: '\n'
      maxLength: 1024
      title: Line ending byte pattern
      description: 'Escapes: \r, \n, \0, \xHH and \\; other text uses UTF-8.'
      x-widget: stringLineEdit
      x-group: timing
      x-order: 2
    bytesPerSecond:
      type: integer
      minimum: 0
      maximum: 1000000000
      default: 0
      title: Maximum bytes per second
      description: Zero disables the rate limit; output packets are bounded by the burst.
      x-widget: integer
      x-group: timing
      x-order: 3
    burstBytes:
      type: integer
      minimum: 1
      maximum: 65536
      default: 64
      title: Maximum rate burst (bytes)
      description: Clamped to the runtime byte limit; larger inputs are split into output packets.
      x-widget: integer
      x-group: timing
      x-order: 4
)";
}

std::unique_ptr<Processor> Pacer::create(const QVariantMap& configuration, const Limits& limits, QString* error)
{
    if (error)
    {
        error->clear();
    }
    const auto reject = [&](const char* message) -> std::unique_ptr<Processor> {
        if (error)
        {
            *error = QString::fromLatin1(message);
        }
        return {};
    };
    if (limits.maxBytes <= 0 || limits.maxChunks <= 0 || limits.maxDepth <= 0 || limits.maxInstances <= 0
        || limits.eventBudget <= 0)
    {
        return reject("Pacer runtime limits must be positive");
    }

    QVariantMap       resolved = configuration;
    const QVariantMap defaults {
        {   "byteDelayMs",     0},
        {   "lineDelayMs",     0},
        {        "ending", "\\n"},
        {"bytesPerSecond",     0},
        {    "burstBytes",    64}
    };
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
    {
        if (!resolved.contains(it.key()))
        {
            resolved.insert(it.key(), it.value());
        }
    }
    const auto integer = [&](const char* key, double minimum, double maximum) {
        const auto value = resolved.value(key);
        const auto type  = value.typeId();
        if (type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong
            && type != QMetaType::ULongLong && type != QMetaType::Double)
        {
            return false;
        }
        const auto number = value.toDouble();
        return std::isfinite(number) && number >= minimum && number <= maximum && number == std::floor(number);
    };
    if (!integer("byteDelayMs", 0, 60000) || !integer("lineDelayMs", 0, 60000)
        || !integer("bytesPerSecond", 0, 1000000000) || !integer("burstBytes", 1, 65536))
    {
        return reject("Pacer timing and burst settings must be integers within their allowed bounds");
    }
    if (resolved.value("ending").typeId() != QMetaType::QString)
    {
        return reject("Pacer ending must be text");
    }
    auto ending = decodeEnding(resolved.value("ending").toString(), error);
    if (!ending)
    {
        return {};
    }
    return std::unique_ptr<Processor>(new Pacer(std::move(resolved), limits, std::move(*ending)));
}
