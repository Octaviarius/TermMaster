#pragma once
#include <QPointer>
#include <Termaster/Core/Processing/Processor.h>

/** Synchronous processing-node lifecycle. An invocation prepares bounded output
 * before any callback, so an error suppresses its complete output. Derived nodes
 * reset packet state after each invocation unless buffering is explicitly enabled.
 */
class SyncProcessorNode : public Processor
{
public:
    QVariantMap configuration() const final
    {
        return _configuration;
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) final
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
        QByteArray output;
        QString    error;
        _process(input, output, error);
        if (!error.isEmpty())
        {
            _stopped = true;
        }
        if (_stopped || !_retainPacketState())
        {
            _reset();
        }
        QPointer<SyncProcessorNode> guard(this);
        if (error.isEmpty() && (!output.isEmpty() || _emitEmptyPacket()) && callbacks.output)
        {
            callbacks.output({std::move(output), input.port});
            if (!guard || guard->_stopped)
            {
                return AcceptResult::Accepted;
            }
        }
        _busy = false;
        if (callbacks.completed)
        {
            callbacks.completed(QString(error.constData(), error.size()));
        }
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> ready) final
    {
        _stopped = true;
        _busy    = false;
        _reset();
        if (ready)
        {
            ready();
        }
    }

protected:
    SyncProcessorNode(QVariantMap configuration, Limits limits) :
        _configuration(std::move(configuration)), _limits(limits)
    {
    }
    virtual bool _retainPacketState() const
    {
        return false;
    }
    virtual bool _emitEmptyPacket() const
    {
        return true;
    }
    qsizetype _maximumBytes() const
    {
        return _limits.maxBytes;
    }
    bool _append(QByteArray& output, char byte, QString& error) const
    {
        if (output.size() >= _limits.maxBytes)
        {
            return _fail(&error, QStringLiteral("Processing node output exceeds byte limit"));
        }
        output.append(byte);
        return true;
    }
    bool _append(QByteArray& output, const QByteArray& bytes, QString& error) const
    {
        if (bytes.size() > _limits.maxBytes - output.size())
        {
            return _fail(&error, QStringLiteral("Processing node output exceeds byte limit"));
        }
        output.append(bytes);
        return true;
    }
    static bool _fail(QString* error, const QString& message)
    {
        if (error)
        {
            *error = message;
        }
        return false;
    }
    static bool _validLimits(const Limits& limits, QString* error)
    {
        if (error)
        {
            error->clear();
        }
        return (limits.maxBytes > 0 && limits.maxChunks > 0 && limits.maxDepth > 0 && limits.maxInstances > 0
                && limits.eventBudget > 0)
            || _fail(error, QStringLiteral("Processing node limits must be positive"));
    }
    static bool _stringOption(QVariantMap&       configuration,
                              const QString&     key,
                              const QString&     fallback,
                              const QStringList& choices,
                              QString*           error)
    {
        if (!configuration.contains(key))
        {
            configuration.insert(key, fallback);
        }
        const auto value = configuration.value(key);
        return (value.typeId() == QMetaType::QString && choices.contains(value.toString()))
            || _fail(error, "Invalid processing node option: " + key);
    }
    static bool _boolOption(QVariantMap& configuration, const QString& key, bool fallback, QString* error)
    {
        if (!configuration.contains(key))
        {
            configuration.insert(key, fallback);
        }
        return configuration.value(key).typeId() == QMetaType::Bool
            || _fail(error, "Processing node option must be boolean: " + key);
    }
    static bool _whitespace(unsigned char byte)
    {
        return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' || byte == '\v' || byte == '\f';
    }
    static QByteArray _schema(const QByteArray& fields)
    {
        return QByteArray(
                   "version: 1\ntype: termaster.settings-schema\ndescription: Packet byte processing settings\ncontent:\n  type: object\n  properties:\n")
             + fields;
    }

private:
    QVariantMap  _configuration;
    Limits       _limits;
    bool         _busy                                                            = false;
    bool         _stopped                                                         = false;
    virtual void _process(const Chunk& input, QByteArray& output, QString& error) = 0;
    virtual void _reset()                                                         = 0;
};
