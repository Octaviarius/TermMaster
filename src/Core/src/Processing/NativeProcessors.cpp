#include <QList>
#include <QPointer>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorCatalog.h>

namespace
{
class NativeProcessor final : public Processor
{
public:
    enum class Kind
    {
        Identity,
        CrFramer,
        HexDecoder
    };

    NativeProcessor(Kind kind, QVariantMap configuration, Limits limits) :
        _configuration(std::move(configuration)), _limits(limits), _kind(kind)
    {
    }

    QVariantMap configuration() const override
    {
        return _configuration;
    }

    AcceptResult accept(Chunk input, Callbacks callbacks) override
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

        QList<Chunk> outputs;
        QString      error;
        qsizetype    bytes     = 0;
        auto         emitChunk = [&](Chunk chunk) {
            if (outputs.size() >= _limits.maxChunks || chunk.bytes.size() > _limits.maxBytes - bytes)
            {
                error = QStringLiteral("Native processor output limit exceeded");
                return false;
            }
            bytes += chunk.bytes.size();
            outputs.push_back(std::move(chunk));
            return true;
        };

        if (_kind == Kind::Identity)
        {
            emitChunk(std::move(input));
        }
        else if (_kind == Kind::CrFramer)
        {
            for (char byte : input.bytes)
            {
                if (byte == '\r')
                {
                    if (!emitChunk({std::move(_buffer), Boundary::Packet}))
                    {
                        break;
                    }
                    _buffer.clear();
                }
                else if (_buffer.size() >= _limits.maxBytes)
                {
                    error = QStringLiteral("CR framer buffer limit exceeded");
                    break;
                }
                else
                {
                    _buffer.append(byte);
                }
            }
        }
        else
        {
            QByteArray decoded;
            for (char byte : input.bytes)
            {
                int value = -1;
                if (byte >= '0' && byte <= '9')
                {
                    value = byte - '0';
                }
                else if (byte >= 'a' && byte <= 'f')
                {
                    value = byte - 'a' + 10;
                }
                else if (byte >= 'A' && byte <= 'F')
                {
                    value = byte - 'A' + 10;
                }
                else
                {
                    error = QStringLiteral("HEX decoder encountered a non-hexadecimal byte");
                    break;
                }
                if (_nibble < 0)
                {
                    _nibble = value;
                }
                else
                {
                    decoded.append(char((_nibble << 4) | value));
                    _nibble = -1;
                }
            }
            if (error.isEmpty() && input.boundary == Boundary::Packet && _nibble >= 0)
            {
                error = QStringLiteral("HEX packet ends with an incomplete byte");
            }
            if (error.isEmpty() && (!decoded.isEmpty() || input.boundary == Boundary::Packet))
            {
                emitChunk({std::move(decoded), input.boundary});
            }
        }
        if (!error.isEmpty())
        {
            _stopped = true;
            _buffer.clear();
            _nibble = -1;
        }
        // State is committed before calling arbitrary caller code.

        QPointer<NativeProcessor> guard(this);
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

    void cancel(std::function<void()> quiescent) override
    {
        _stopped = true;
        _busy    = false;
        _buffer.clear();
        _nibble = -1;
        if (quiescent)
        {
            quiescent();
        }
    }

private:
    QVariantMap _configuration;
    Limits      _limits;
    Kind        _kind;
    QByteArray  _buffer;
    int         _nibble  = -1;
    bool        _stopped = false;
    bool        _busy    = false;
};
} // namespace
void registerNativeProcessors(ProcessorCatalog& catalog, const QString& owner)
{
    const QByteArray schema = R"(version: 1
type: termaster.settings-schema
description: Native byte processor settings
content:
  type: object
  properties: {}
)";
    struct Definition
    {
        const char*           id;
        const char*           title;
        NativeProcessor::Kind kind;
    };
    const Definition definitions[] = {
        {   "native.identity",         "Identity",   NativeProcessor::Kind::Identity},
        {  "native.cr-framer", "CR packet framer",   NativeProcessor::Kind::CrFramer},
        {"native.hex-decoder",      "HEX decoder", NativeProcessor::Kind::HexDecoder}
    };
    QStringList registered;
    try
    {
        for (const auto& definition : definitions)
        {
            ProcessorCatalog::Type type;
            type.id             = QString::fromLatin1(definition.id);
            type.title          = QString::fromLatin1(definition.title);
            type.settingsSchema = schema;
            type.create         = [kind = definition.kind](const QVariantMap&       configuration,
                                                           const Processor::Limits& limits,
                                                           QString*) -> std::unique_ptr<Processor> {
                return std::make_unique<NativeProcessor>(kind, configuration, limits);
            };
            catalog.registerType(owner, std::move(type));
            registered.push_back(QString::fromLatin1(definition.id));
        }
    }
    catch (...)
    {
        for (const auto& id : registered)
        {
            catalog.unregisterType(owner, id);
        }
        throw;
    }
}
