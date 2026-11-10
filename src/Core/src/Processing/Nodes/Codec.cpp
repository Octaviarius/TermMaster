#include "Codec.h"

#include <Termaster/Core/SettingsSchema.h>
#include <yaml-cpp/yaml.h>

namespace
{
YAML::Node properties(const QByteArray& fragment)
{
    return fragment.isEmpty() ? YAML::Node(YAML::NodeType::Map) : YAML::Load(fragment.constData());
}
QString prefixed(const QString& prefix, QString key)
{
    if (prefix.isEmpty())
    {
        return key;
    }
    key[0] = key[0].toUpper();
    return prefix + key;
}
void appendProperties(YAML::Node destination, const YAML::Node& fields, const QString& prefix)
{
    for (const auto& field : fields)
    {
        auto descriptor = YAML::Clone(field.second);
        if (!prefix.isEmpty())
        {
            descriptor["x-group"] = prefix.toStdString();
        }
        destination[prefixed(prefix, QString::fromStdString(field.first.as<std::string>())).toStdString()] = descriptor;
    }
}
} // namespace

Codec::Codec(QVariantMap                configuration,
             QString                    wireName,
             QString                    payloadName,
             std::unique_ptr<Processor> inverse,
             std::unique_ptr<Processor> direct) :
    _configuration(std::move(configuration)), _wireName(std::move(wireName)), _payloadName(std::move(payloadName)),
    _codecs {std::move(inverse), std::move(direct)}
{
    QPointer<Codec> guard(this);
    for (int port = 0; port < inputCount(); ++port)
    {
        _codecs[port]->setOutputHandler([guard, port](Chunk output) {
            if (guard && !guard->_stopped && guard->_failure.isEmpty())
            {
                output.port = port;
                guard->publishOutput(std::move(output));
            }
        });
        _codecs[port]->setErrorHandler([guard](QString error) {
            if (guard && !guard->_stopped && guard->_failure.isEmpty())
            {
                guard->_failure = error;
                guard->publishError(std::move(error));
            }
        });
    }
}

QVariantMap Codec::configuration() const
{
    return _configuration;
}
int Codec::inputCount() const
{
    return 2;
}
int Codec::outputCount() const
{
    return 2;
}
QString Codec::inputName(int port) const
{
    if (port < 0 || port >= inputCount())
    {
        return {};
    }
    return (port == 1) ? _payloadName : _wireName;
}
QString Codec::outputName(int port) const
{
    if (port < 0 || port >= outputCount())
    {
        return {};
    }
    return (port == 1) ? _wireName : _payloadName;
}

Processor::AcceptResult Codec::accept(Chunk input, Callbacks callbacks)
{
    if (_stopped || !_failure.isEmpty() || input.port < 0 || input.port >= inputCount())
    {
        return AcceptResult::Rejected;
    }

    const int port = input.port;
    input.port     = 0;
    QPointer<Codec> guard(this);
    auto            output    = std::move(callbacks.output);
    auto            completed = std::move(callbacks.completed);
    return _codecs[port]->accept(std::move(input),
                                 {[guard, port, output = std::move(output)](Chunk chunk) {
                                      if (guard && !guard->_stopped && guard->_failure.isEmpty() && output)
                                      {
                                          chunk.port = port;
                                          output(std::move(chunk));
                                      }
                                  },
                                  [guard, completed = std::move(completed)](QString error) {
                                      if (!guard || guard->_stopped)
                                      {
                                          return;
                                      }
                                      if (!error.isEmpty() && guard->_failure.isEmpty())
                                      {
                                          // Preserve completions already in flight on the peer port.
                                          // Only explicit cancellation suppresses accepted callbacks.
                                          guard->_failure = error;
                                      }
                                      if (completed)
                                      {
                                          completed(guard->_failure.isEmpty() ? std::move(error) : guard->_failure);
                                      }
                                  }});
}

void Codec::cancel(std::function<void()> quiescent)
{
    _stopped = true;
    if (_pendingCancellation == 0)
    {
        if (quiescent)
        {
            quiescent();
        }
        return;
    }
    if (quiescent)
    {
        _quiescent.append(std::move(quiescent));
    }
    if (_pendingCancellation != -1)
    {
        return;
    }

    _pendingCancellation = inputCount();
    QPointer<Codec> guard(this);
    for (int port = 0; port < inputCount(); ++port)
    {
        const auto& codec = _codecs[port];
        codec->setOutputHandler({});
        codec->setErrorHandler({});
        codec->cancel([guard] {
            if (guard)
            {
                guard->_cancelled();
            }
        });
        if (!guard)
        {
            return;
        }
    }
}

void Codec::_cancelled()
{
    if (--_pendingCancellation != 0)
    {
        return;
    }
    auto callbacks = std::move(_quiescent);
    for (auto& callback : callbacks)
    {
        callback();
    }
}

Codec::Definition::Definition(QByteArray commonSchema,
                              QByteArray directProperties,
                              QByteArray inverseProperties,
                              Factory    direct,
                              Factory    inverse,
                              QString    wireName,
                              QString    payloadName,
                              QString    directPrefix,
                              QString    inversePrefix) :
    _commonSchema(std::move(commonSchema)), _directProperties(std::move(directProperties)),
    _inverseProperties(std::move(inverseProperties)), _direct(std::move(direct)), _inverse(std::move(inverse)),
    _wireName(std::move(wireName)), _payloadName(std::move(payloadName)), _directPrefix(std::move(directPrefix)),
    _inversePrefix(std::move(inversePrefix))
{
}

QByteArray Codec::Definition::schema(Mode mode) const
{
    auto document = YAML::Load(_commonSchema.constData());
    auto content  = document["content"];
    if (!content["properties"].IsMap())
    {
        content["properties"] = YAML::Node(YAML::NodeType::Map);
    }
    if (mode != Mode::Inverse)
    {
        appendProperties(content["properties"],
                         properties(_directProperties),
                         mode == Mode::Duplex ? _directPrefix : QString {});
    }
    if (mode != Mode::Direct)
    {
        appendProperties(content["properties"],
                         properties(_inverseProperties),
                         mode == Mode::Duplex ? _inversePrefix : QString {});
    }
    if (mode == Mode::Duplex)
    {
        for (const auto& prefix : {_inversePrefix, _directPrefix})
        {
            if (properties(prefix == _directPrefix ? _directProperties : _inverseProperties).size() == 0)
            {
                continue;
            }
            YAML::Node group;
            group["id"]    = prefix.toStdString();
            auto title     = prefix;
            title[0]       = title[0].toUpper();
            group["title"] = title.toStdString();
            content["x-groups"].push_back(group);
        }
    }
    return QByteArray::fromStdString(YAML::Dump(document));
}

std::unique_ptr<Processor> Codec::Definition::create(Mode                     mode,
                                                     const QVariantMap&       configuration,
                                                     const Processor::Limits& limits,
                                                     QString*                 error) const
{
    if (mode == Mode::Direct)
    {
        return _direct(configuration, limits, error);
    }
    if (mode == Mode::Inverse)
    {
        return _inverse(configuration, limits, error);
    }

    auto normalized  = configuration;
    auto instantiate = [&](const Factory& factory, const QByteArray& fragment, const QString& prefix) {
        const auto fields = properties(fragment);
        auto       values = configuration;
        for (const auto& field : fields)
        {
            const auto key = QString::fromStdString(field.first.as<std::string>());
            values.remove(key);
            const auto scoped = prefixed(prefix, key);
            if (configuration.contains(scoped))
            {
                values.insert(key, configuration.value(scoped));
            }
        }
        auto processor = factory(values, limits, error);
        if (processor)
        {
            const auto resolved = processor->configuration();
            for (auto it = resolved.cbegin(); it != resolved.cend(); ++it)
            {
                // Known directional fields are scoped; common fields and unknown configuration stay intact.
                if (fields[it.key().toStdString()])
                {
                    normalized.insert(prefixed(prefix, it.key()), it.value());
                }
                else if (!configuration.contains(it.key()))
                {
                    normalized.insert(it.key(), it.value());
                }
            }
        }
        return processor;
    };
    auto inverse = instantiate(_inverse, _inverseProperties, _inversePrefix);
    if (!inverse)
    {
        return {};
    }
    auto direct = instantiate(_direct, _directProperties, _directPrefix);
    if (!direct)
    {
        return {};
    }
    normalized = SettingsSchema(schema(Mode::Duplex)).activeValues(normalized);
    return std::make_unique<Codec>(std::move(normalized),
                                   _wireName,
                                   _payloadName,
                                   std::move(inverse),
                                   std::move(direct));
}
