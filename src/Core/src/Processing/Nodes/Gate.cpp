#include "Gate.h"

#include <QPointer>

Gate::Gate(QVariantMap configuration, Limits limits, bool enabled) :
    _configuration(std::move(configuration)), _limits(limits), _enabled(enabled)
{
    _configuration.insert("enabled", enabled);
}

QByteArray Gate::schema()
{
    return R"(version: 1
type: termaster.settings-schema
description: Traffic gate settings
content:
  type: object
  properties:
    enabled:
      type: boolean
      title: Enabled
      description: Pass bytes unchanged when enabled; discard input when disabled.
      default: true
      x-group: traffic
  x-groups:
    - id: traffic
      title: Traffic
)";
}

std::unique_ptr<Processor> Gate::create(const QVariantMap& configuration, const Limits& limits, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (limits.maxBytes <= 0 || limits.maxChunks <= 0 || limits.maxDepth <= 0 || limits.maxInstances <= 0
        || limits.eventBudget <= 0
        || (configuration.contains("enabled") && configuration.value("enabled").typeId() != QMetaType::Bool))
    {
        if (error)
        {
            *error = QString::fromLatin1("Gate requires valid limits and a boolean enabled setting");
        }
        return {};
    }
    return std::unique_ptr<Processor>(new Gate(configuration, limits, configuration.value("enabled", true).toBool()));
}

QVariantMap Gate::configuration() const
{
    return _configuration;
}

Processor::AcceptResult Gate::accept(Chunk input, Callbacks callbacks)
{
    if (_stopped || input.bytes.size() > _limits.maxBytes)
    {
        return AcceptResult::Rejected;
    }
    if (_busy)
    {
        return AcceptResult::WouldBlock;
    }
    _busy = true;

    QPointer<Gate> guard(this);
    if (_enabled && callbacks.output)
    {
        input.bytes = QByteArray(input.bytes.constData(), input.bytes.size());
        callbacks.output(std::move(input));
        if (!guard || _stopped)
        {
            return AcceptResult::Accepted;
        }
    }
    _busy = false;
    if (callbacks.completed)
    {
        callbacks.completed({});
    }
    return AcceptResult::Accepted;
}

void Gate::cancel(std::function<void()> quiescent)
{
    _stopped = true;
    _busy    = false;
    if (quiescent)
    {
        quiescent();
    }
}
