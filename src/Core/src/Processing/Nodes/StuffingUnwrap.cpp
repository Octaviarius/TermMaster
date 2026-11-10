#include "StuffingUnwrap.h"

#include <QPointer>

StuffingUnwrap::StuffingUnwrap(QVariantMap configuration, Limits limits) :
    _configuration(std::move(configuration)), _limits(limits), _settings(_configuration),
    _framing(_configuration.value("framing").toBool()), _awaitOpening(_settings.boundaries() == 2)
{
}

QByteArray StuffingUnwrap::schema()
{
    return StuffingSettings::schema() + R"SCHEMA(    framing:
      type: boolean
      title: Framing
      description: Buffer stream chunks until each delimiter and decode complete frames; cancellation discards an incomplete frame. None and suffix consume the delimiter; both requires paired opening and closing delimiters.
      default: true
      x-group: stuffing
      x-order: 5
)SCHEMA";
}

std::unique_ptr<Processor> StuffingUnwrap::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (limits.maxBytes <= 0 || limits.maxChunks <= 0 || limits.maxDepth <= 0 || limits.maxInstances <= 0
        || limits.eventBudget <= 0 || limits.maxSteps <= 0)
    {
        _fail(error, QStringLiteral("Processing node limits must be positive"));
        return {};
    }

    auto configuration = input;
    if (!StuffingSettings::validate(configuration, error))
    {
        return {};
    }
    if (!configuration.contains("framing"))
    {
        configuration.insert("framing", true);
    }
    if (configuration.value("framing").typeId() != QMetaType::Bool)
    {
        _fail(error, QStringLiteral("Stuffing framing must be boolean"));
        return {};
    }

    return std::unique_ptr<Processor>(new StuffingUnwrap(std::move(configuration), limits));
}

QVariantMap StuffingUnwrap::configuration() const
{
    return _configuration;
}

Processor::AcceptResult StuffingUnwrap::accept(Chunk input, Callbacks callbacks)
{
    if (_stopped || input.port != 0 || input.bytes.size() > _limits.maxBytes)
    {
        return AcceptResult::Rejected;
    }
    if (_busy)
    {
        return AcceptResult::WouldBlock;
    }
    _busy = true;

    QList<Chunk> outputs;
    qsizetype    outputBytes = 0;
    qsizetype    remaining   = _limits.maxSteps;
    QString      error;
    if (_charge(input.bytes.size(), remaining, error))
    {
        if (!_framing)
        {
            const auto boundaries = _settings.boundaries();
            if (input.bytes.size() < boundaries
                || (boundaries > 0 && static_cast<unsigned char>(input.bytes.back()) != _settings.delimiter())
                || (boundaries == 2 && static_cast<unsigned char>(input.bytes.front()) != _settings.delimiter()))
            {
                _fail(&error, QStringLiteral("Stuffed packet is missing its configured delimiter boundary"));
            }
            else
            {
                const qsizetype start = boundaries == 2 ? 1 : 0;
                const auto      body  = QByteArrayView(input.bytes).sliced(start, input.bytes.size() - boundaries);
                _decode(body, outputs, outputBytes, remaining, error);
            }
        }
        else
        {
            for (const unsigned char byte : input.bytes)
            {
                if (_awaitOpening)
                {
                    if (byte != _settings.delimiter())
                    {
                        _fail(&error, QStringLiteral("Stuffed stream is missing an opening delimiter"));
                        break;
                    }
                    _awaitOpening = false;
                    continue;
                }
                if (byte == _settings.delimiter())
                {
                    auto body = std::move(_buffer);
                    _buffer.clear();
                    if (!_charge(body.size(), remaining, error)
                        || !_decode(body, outputs, outputBytes, remaining, error))
                    {
                        break;
                    }
                    _awaitOpening = _settings.boundaries() == 2;
                }
                else
                {
                    if (_buffer.size() >= _limits.maxBytes - outputBytes)
                    {
                        _fail(&error, QStringLiteral("Stuffed stream accumulated frame exceeds byte limit"));
                        break;
                    }
                    _buffer.append(char(byte));
                }
            }
        }
    }
    if (!error.isEmpty())
    {
        _stopped = true;
        _buffer.clear();
        _awaitOpening = _settings.boundaries() == 2;
    }

    // State and the complete invocation's output are prepared before arbitrary callbacks.
    QPointer<StuffingUnwrap> guard(this);
    if (error.isEmpty() && callbacks.output)
    {
        for (auto& output : outputs)
        {
            callbacks.output(std::move(output));
            if (!guard || guard->_stopped)
            {
                return AcceptResult::Accepted;
            }
        }
    }
    _busy = false;
    if (callbacks.completed)
    {
        callbacks.completed(QString(error.constData(), error.size()));
    }
    return AcceptResult::Accepted;
}

void StuffingUnwrap::cancel(std::function<void()> quiescent)
{
    _stopped = true;
    _busy    = false;
    _buffer.clear();
    _awaitOpening = _settings.boundaries() == 2;
    if (quiescent)
    {
        quiescent();
    }
}

bool StuffingUnwrap::_fail(QString* error, const QString& message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

bool StuffingUnwrap::_charge(qsizetype count, qsizetype& remaining, QString& error)
{
    if (count > remaining)
    {
        return _fail(&error, QStringLiteral("Stuffing processing exceeds step limit"));
    }
    remaining -= count;
    return true;
}

bool StuffingUnwrap::_decode(QByteArrayView body,
                             QList<Chunk>&  outputs,
                             qsizetype&     outputBytes,
                             qsizetype&     remaining,
                             QString&       error) const
{
    qsizetype decodedBytes = 0;
    for (qsizetype index = 0; index < body.size(); ++index)
    {
        const auto byte = static_cast<unsigned char>(body[index]);
        if (byte == _settings.delimiter())
        {
            return _fail(&error, QStringLiteral("Stuffed packet contains a raw delimiter in its body"));
        }
        if (byte == _settings.escape())
        {
            if (++index == body.size())
            {
                return _fail(&error, QStringLiteral("Stuffed packet ends with a dangling escape"));
            }
            const auto code = static_cast<unsigned char>(body[index]);
            if (code != _settings.delimiterCode() && code != _settings.escapeCode())
            {
                return _fail(&error, QStringLiteral("Stuffed packet contains an unknown escape code"));
            }
        }
        ++decodedBytes;
    }
    if (outputs.size() >= _limits.maxChunks || decodedBytes > _limits.maxBytes - outputBytes)
    {
        return _fail(&error, QStringLiteral("Stuffing output exceeds runtime queue limits"));
    }
    if (!_charge(decodedBytes, remaining, error))
    {
        return false;
    }

    QByteArray output;
    output.reserve(decodedBytes);
    for (qsizetype index = 0; index < body.size(); ++index)
    {
        const auto byte = static_cast<unsigned char>(body[index]);
        if (byte == _settings.escape())
        {
            const auto code = static_cast<unsigned char>(body[++index]);
            output.append(char(code == _settings.delimiterCode() ? _settings.delimiter() : _settings.escape()));
        }
        else
        {
            output.append(char(byte));
        }
    }
    outputBytes += decodedBytes;
    outputs.append({std::move(output), 0});
    return true;
}
