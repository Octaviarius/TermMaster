#include "Framer.h"

#include <QList>
#include <QPointer>
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace
{
int hexDigit(QChar value)
{
    const auto byte = value.unicode();
    if (byte >= '0' && byte <= '9')
    {
        return byte - '0';
    }
    if (byte >= 'a' && byte <= 'f')
    {
        return byte - 'a' + 10;
    }
    if (byte >= 'A' && byte <= 'F')
    {
        return byte - 'A' + 10;
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
        return std::nullopt;
    };
    // No valid 256-byte pattern needs more than 1024 escaped UTF-16 units.
    if (text.isEmpty() || text.size() > 1024)
    {
        return reject("Framer ending must decode to 1 to 256 bytes");
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
                return reject("Framer ending has an incomplete escape");
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
                    return reject("Framer hexadecimal escape requires two digits");
                }
                const auto high = hexDigit(text[index]);
                const auto low  = hexDigit(text[index + 1]);
                if (high < 0 || low < 0)
                {
                    return reject("Framer ending has an invalid hexadecimal escape");
                }
                result.append(char((high << 4) | low));
                index += 2;
            }
            else
            {
                return reject("Framer ending contains an unsupported escape");
            }
        }
        if (result.size() > 256)
        {
            return reject("Framer ending must decode to 1 to 256 bytes");
        }
    }
    if (result.isEmpty())
    {
        return reject("Framer ending must decode to 1 to 256 bytes");
    }
    return result;
}

} // namespace

Framer::Framer(QVariantMap configuration,
               Limits      limits,
               Mode        mode,
               Tail        tail,
               QByteArray  ending,
               qsizetype   frameSize,
               qsizetype   maximum) :
    _configuration(std::move(configuration)), _limits(limits), _mode(mode), _tail(tail), _ending(std::move(ending)),
    _frameSize(frameSize), _maximum(maximum), _includeEnding(_configuration.value("includeEnding").toBool()),
    _emitEmpty(_configuration.value("emitEmpty").toBool())
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
}

QVariantMap Framer::configuration() const
{
    return _configuration;
}

Processor::AcceptResult Framer::accept(Chunk input, Callbacks callbacks)
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
    _busy = true;

    QList<Chunk> outputs;
    qsizetype    outputBytes = 0;
    QString      error;
    const auto   fail  = [&](const char* message) { error = QString::fromLatin1(message); };
    const auto   frame = [&](bool delimiter) {
        auto bytes = std::move(_buffer);
        _buffer.clear();
        _matched = 0;
        if (delimiter && !_includeEnding)
        {
            bytes.chop(_ending.size());
        }
        if (bytes.isEmpty() && !_emitEmpty)
        {
            return;
        }
        if (outputs.size() >= _limits.maxChunks || bytes.size() > _limits.maxBytes - outputBytes)
        {
            fail("Framer output exceeds runtime queue limits");
            return;
        }
        outputBytes += bytes.size();
        outputs.append({std::move(bytes), input.port});
    };
    for (const char byte : input.bytes)
    {
        // Count buffered raw bytes, including an incomplete delimiter, before allocation.
        if (_buffer.size() >= _maximum || _buffer.size() >= _limits.maxBytes - outputBytes)
        {
            fail("Framer accumulated frame exceeds its byte limit");
            break;
        }
        _buffer.append(byte);
        bool delimiter = false;
        if (_mode != Mode::Size)
        {
            while (_matched > 0 && byte != _ending[_matched])
            {
                _matched = _prefix[size_t(_matched - 1)];
            }
            if (byte == _ending[_matched])
            {
                ++_matched;
            }
            delimiter = _matched == _ending.size();
        }
        const bool sized = _mode != Mode::Delimiter && _buffer.size() == _frameSize;
        if (delimiter || sized)
        {
            // A complete delimiter wins a tie with the raw-size rule.
            frame(delimiter);
            if (!error.isEmpty())
            {
                break;
            }
        }
    }
    if (error.isEmpty())
    {
        if (!_buffer.isEmpty())
        {
            if (_tail == Tail::Emit)
            {
                frame(false);
            }
            else if (_tail == Tail::Discard)
            {
                _buffer.clear();
                _matched = 0;
            }
            else if (_tail == Tail::Error)
            {
                fail("Framer packet ends with an incomplete frame");
            }
        }
        else if (_tail == Tail::Emit && outputs.isEmpty() && _emitEmpty)
        {
            frame(false);
        }
    }
    if (!error.isEmpty())
    {
        _stopped = true;
        _buffer.clear();
        _matched = 0;
    }

    // Commit all state before arbitrary callbacks; one invocation either emits all or none.
    QPointer<Framer> guard(this);
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
        callbacks.completed(std::move(error));
    }
    return AcceptResult::Accepted;
}

void Framer::cancel(std::function<void()> quiescent)
{
    _stopped = true;
    _busy    = false;
    _buffer.clear();
    _matched = 0;
    if (quiescent)
    {
        quiescent();
    }
}

QByteArray Framer::schema()
{
    return R"(version: 1
type: termaster.settings-schema
description: Stateful byte framing settings
content:
  type: object
  x-groups:
    - {id: framing, title: Framing, order: 0}
    - {id: boundaries, title: Packet boundaries, order: 1}
    - {id: limits, title: Limits, order: 2}
  properties:
    mode:
      type: string
      enum: [delimiter, size, either]
      default: delimiter
      title: Framing mode
      x-group: framing
      x-order: 0
    frameSize:
      type: integer
      minimum: 1
      maximum: 1048576
      default: 64
      title: Frame size in bytes
      x-group: framing
      x-order: 1
    ending:
      type: string
      default: '\r'
      maxLength: 1024
      title: Ending byte pattern
      description: 'Escapes: \r, \n, \0, \xHH and \\; other text uses UTF-8.'
      x-group: framing
      x-order: 2
    includeEnding:
      type: boolean
      default: false
      title: Include matched ending
      x-group: framing
      x-order: 3
    emitEmpty:
      type: boolean
      default: true
      title: Emit empty frames
      x-group: framing
      x-order: 4
    packetTail:
      type: string
      enum: [preserve, emit, discard, error]
      default: preserve
      title: Incomplete packet tail
      x-group: boundaries
      x-order: 0
    maxFrameSize:
      type: integer
      minimum: 1
      maximum: 1048576
      default: 1048576
      title: Maximum accumulated bytes
      x-group: limits
      x-order: 0
)";
}

std::unique_ptr<Processor> Framer::create(const QVariantMap&       configuration,
                                          const Processor::Limits& limits,
                                          QString*                 error)
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
    if (limits.maxBytes <= 0 || limits.maxChunks <= 0)
    {
        return reject("Framer runtime limits must be positive");
    }
    QVariantMap       resolved = configuration;
    const QVariantMap defaults {
        {         "mode", "delimiter"},
        {    "frameSize",          64},
        {       "ending",       "\\r"},
        {"includeEnding",       false},
        {    "emitEmpty",        true},
        {   "packetTail",  "preserve"},
        { "maxFrameSize",     1048576}
    };
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
    {
        if (!resolved.contains(it.key()))
        {
            resolved.insert(it.key(), it.value());
        }
    }
    const auto modeValue = resolved.value("mode");
    const auto tailValue = resolved.value("packetTail");
    if (modeValue.typeId() != QMetaType::QString || tailValue.typeId() != QMetaType::QString)
    {
        return reject("Framer mode and packet tail policy must be strings");
    }
    Framer::Mode mode;
    if (modeValue == "delimiter")
    {
        mode = Framer::Mode::Delimiter;
    }
    else if (modeValue == "size")
    {
        mode = Framer::Mode::Size;
    }
    else if (modeValue == "either")
    {
        mode = Framer::Mode::Either;
    }
    else
    {
        return reject("Unknown framer mode");
    }
    Framer::Tail tail;
    if (tailValue == "preserve")
    {
        tail = Framer::Tail::Preserve;
    }
    else if (tailValue == "emit")
    {
        tail = Framer::Tail::Emit;
    }
    else if (tailValue == "discard")
    {
        tail = Framer::Tail::Discard;
    }
    else if (tailValue == "error")
    {
        tail = Framer::Tail::Error;
    }
    else
    {
        return reject("Unknown framer packet tail policy");
    }
    for (const auto& key : {"includeEnding", "emitEmpty"})
    {
        if (resolved.value(key).typeId() != QMetaType::Bool)
        {
            return reject("Framer flags must be boolean values");
        }
    }
    const auto integer = [&](const char* key) -> std::optional<qsizetype> {
        const auto value = resolved.value(key);
        const auto type  = value.typeId();
        if (type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong
            && type != QMetaType::ULongLong && type != QMetaType::Double)
        {
            return std::nullopt;
        }
        const auto number = value.toDouble();
        if (!std::isfinite(number) || number < 1 || number > 1048576 || number != std::floor(number))
        {
            return std::nullopt;
        }
        return qsizetype(number);
    };
    const auto maximum = integer("maxFrameSize");
    if (!maximum)
    {
        return reject("Framer maximum frame size must be an integer from 1 to 1048576");
    }
    const auto effective = std::min(*maximum, limits.maxBytes);
    qsizetype  size      = 64;
    if (mode != Framer::Mode::Delimiter)
    {
        const auto configured = integer("frameSize");
        if (!configured)
        {
            return reject("Framer frame size must be an integer from 1 to 1048576");
        }
        size = *configured;
        if (mode == Framer::Mode::Size && size > effective)
        {
            return reject("Framer frame size exceeds the effective accumulated byte limit");
        }
    }
    QByteArray ending;
    if (mode != Framer::Mode::Size)
    {
        if (resolved.value("ending").typeId() != QMetaType::QString)
        {
            return reject("Framer ending must be text");
        }
        const auto decoded = decodeEnding(resolved.value("ending").toString(), error);
        if (!decoded)
        {
            return {};
        }
        ending = *decoded;
    }
    return std::unique_ptr<Processor>(
        new Framer(std::move(resolved), limits, mode, tail, std::move(ending), size, effective));
}
