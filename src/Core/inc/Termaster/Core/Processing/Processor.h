#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <functional>

/** Header-only native processing ABI. All calls and callbacks use the owning QObject thread.
 * Instances are independent stream state. A caller must retain the instance until cancel's
 * acknowledgement; discarding callbacks or advancing a generation does not stop worker work.
 * Worker callbacks must post to this QObject's thread with a context-bound connection.
 * cancel acknowledgement means no worker or future callback can use the instance.
 */
class Processor : public QObject
{
public:
    enum class AcceptResult
    {
        Accepted,
        WouldBlock,
        Rejected
    };

    enum class Boundary
    {
        StreamChunk,
        Packet
    };

    struct Chunk
    {
        QByteArray bytes;
        Boundary   boundary = Boundary::StreamChunk;
        int        port     = 0;
    };

    struct Limits
    {
        qsizetype maxBytes     = 1024 * 1024;
        qsizetype maxChunks    = 1024;
        int       maxDepth     = 16;
        int       maxInstances = 256;
        int       eventBudget  = 64;
        qsizetype maxSteps     = 1024 * 1024;
    };

    struct Callbacks
    {
        /** Zero or more outputs before completed, including an explicit empty packet.
         * Payload must own heap storage: do not return QByteArrayLiteral/fromRawData.
         */
        std::function<void(Chunk)> output;

        /** Exactly once for Accepted, including buffered input producing zero output.
         * Error text must own heap storage rather than a native library string literal.
         */
        std::function<void(QString)> completed;
    };

    explicit Processor(QObject* parent = nullptr) : QObject(parent)
    {
    }

    ~Processor() override = default;
    Q_DISABLE_COPY_MOVE(Processor)

    virtual QVariantMap configuration() const = 0;
    /** Port counts are immutable for this instance. General native shapes use 1 to 32 ports. */
    virtual int inputCount() const
    {
        return 1;
    }
    virtual int outputCount() const
    {
        return 1;
    }

    virtual QString inputName(int port) const
    {
        return port >= 0 && port < inputCount() ? QStringLiteral("Input%1").arg(port + 1) : QString {};
    }
    virtual QString outputName(int port) const
    {
        return port >= 0 && port < outputCount() ? QStringLiteral("Output%1").arg(port + 1) : QString {};
    }

    /** Lifetime-owned output/error channels for timer or protocol traffic unrelated to an
     * accepted invocation. All handlers use this object's thread. Clear before cancellation;
     * cancellation must stop future publication before acknowledging quiescence.
     */
    virtual void setOutputHandler(std::function<void(Chunk)> handler)
    {
        _outputHandler = std::move(handler);
    }
    virtual void setErrorHandler(std::function<void(QString)> handler)
    {
        _errorHandler = std::move(handler);
    }

    /** Owned input. WouldBlock/Rejected consume nothing and must not invoke callbacks.
     * Accepted owns one invocation on input.port until completed. Different ports may run
     * concurrently; a provider may return WouldBlock when that port cannot advance.
     * Implementations may complete inline. Output port is independent of input port.
     * Output expansion and persistent buffering must respect the supplied factory limits.
     */
    virtual AcceptResult accept(Chunk input, Callbacks callbacks) = 0;

    /** Stops acceptance and suppresses pending output/completion callbacks immediately.
     * Idempotently cancels owned workers and acknowledges each call only after quiescence.
     * Pending invocation completion is suppressed after acknowledgement. Resetting stream
     * state is achieved by creating a new instance from the prepared configuration.
     */
    virtual void cancel(std::function<void()> quiescent) = 0;

protected:
    void publishOutput(Chunk output)
    {
        auto handler = _outputHandler;
        if (handler)
        {
            handler(std::move(output));
        }
    }
    void publishError(QString error)
    {
        auto handler = _errorHandler;
        if (handler)
        {
            handler(std::move(error));
        }
    }

private:
    std::function<void(Chunk)>   _outputHandler;
    std::function<void(QString)> _errorHandler;
};
