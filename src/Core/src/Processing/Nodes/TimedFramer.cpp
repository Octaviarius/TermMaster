#include "TimedFramer.h"

#include <QPointer>
#include <cmath>

TimedFramer::TimedFramer(QVariantMap configuration, Limits limits) :
    _configuration(std::move(configuration)), _limits(limits), _gapMs(_configuration.value("gapMs").toInt()),
    _timer(this)
{
    _timer.setSingleShot(true);
    _timer.setTimerType(Qt::PreciseTimer);
    connect(&_timer, &QTimer::timeout, this, &TimedFramer::_timeout);
}

QVariantMap TimedFramer::configuration() const
{
    return _configuration;
}

QString TimedFramer::inputName(int port) const
{
    return port == 0 ? QStringLiteral("Stream") : QString {};
}

QString TimedFramer::outputName(int port) const
{
    return port == 0 ? QStringLiteral("Frames") : QString {};
}

Processor::AcceptResult TimedFramer::accept(Chunk input, Callbacks callbacks)
{
    if (_stopped || input.port != 0)
    {
        return AcceptResult::Rejected;
    }
    if (_busy)
    {
        return AcceptResult::WouldBlock;
    }

    _busy = true;
    QPointer<TimedFramer> guard(this);
    if (!_buffer.isEmpty() && _idle.elapsed() >= _gapMs)
    {
        _flush();
        if (!guard || _stopped)
        {
            return AcceptResult::Accepted;
        }
    }

    QString error;
    if (input.bytes.size() > _limits.maxBytes - _buffer.size())
    {
        _stopped = true;
        _timer.stop();
        _buffer.clear();
        error = QString::fromLatin1("Timed Framer buffer exceeds the runtime byte limit");
    }
    else if (!input.bytes.isEmpty())
    {
        // append copies even borrowed fromRawData packets before input acknowledgement.
        _buffer.append(input.bytes.constData(), input.bytes.size());
        _idle.start();
        _timer.start(_gapMs);
    }

    _busy = false;
    if (callbacks.completed)
    {
        callbacks.completed(std::move(error));
    }
    return AcceptResult::Accepted;
}

void TimedFramer::cancel(std::function<void()> quiescent)
{
    _stopped = true;
    _timer.stop();
    _buffer.clear();
    if (quiescent)
    {
        quiescent();
    }
}

void TimedFramer::_flush()
{
    _timer.stop();
    auto bytes = std::move(_buffer);
    _buffer    = {};
    if (!bytes.isEmpty())
    {
        publishOutput({std::move(bytes), 0});
    }
}

void TimedFramer::_timeout()
{
    if (_stopped || _busy || _buffer.isEmpty())
    {
        return;
    }
    const auto remaining = _gapMs - _idle.elapsed();
    if (remaining > 0)
    {
        _timer.start(int(remaining));
        return;
    }

    _busy = true;
    QPointer<TimedFramer> guard(this);
    _flush();
    if (guard)
    {
        _busy = false;
    }
}

QByteArray TimedFramer::schema()
{
    return R"(version: 1
type: termaster.settings-schema
description: Idle-gap byte framing settings
content:
  type: object
  x-groups:
    - {id: framing, title: Framing, order: 0}
  properties:
    gapMs:
      type: integer
      minimum: 1
      maximum: 60000
      default: 20
      title: Idle gap (ms)
      description: Emits buffered bytes after silence. Empty packets do not reset the gap.
      x-widget: integer
      x-group: framing
      x-order: 0
)";
}

std::unique_ptr<Processor> TimedFramer::create(const QVariantMap& configuration, const Limits& limits, QString* error)
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
        || limits.eventBudget <= 0 || limits.maxSteps <= 0)
    {
        return reject("Timed Framer runtime limits must be positive");
    }

    auto resolved = configuration;
    if (!resolved.contains("gapMs"))
    {
        resolved.insert("gapMs", 20);
    }
    const auto value  = resolved.value("gapMs");
    const auto type   = value.typeId();
    const auto number = value.toDouble();
    if ((type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong
         && type != QMetaType::ULongLong && type != QMetaType::Double)
        || !std::isfinite(number) || number < 1 || number > 60000 || number != std::floor(number))
    {
        return reject("Timed Framer gapMs must be an integer from 1 to 60000");
    }
    return std::unique_ptr<Processor>(new TimedFramer(std::move(resolved), limits));
}
