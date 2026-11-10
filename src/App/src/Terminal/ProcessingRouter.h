#pragma once

#include <QObject>
#include <QQueue>
#include <QTimer>
#include <Termaster/Core/Processing/Processor.h>
#include <Termaster/Core/Terminal/TerminalSendQueue.h>
#include <array>
#include <memory>

/** Owning-thread bounded processing coordinator. Replacement discards queued and
 * buffered state, and starts the replacement only after cancellation quiescence.
 * Overlapping replacements coalesce; all ready callbacks run once the latest
 * replacement becomes active. Each supplied instance must be fresh and independent.
 * Stream submissions are copied into at most 1024-byte invocations, with atomic ingress
 * admission of every fragment. Packet submissions preserve their whole boundary.
 * Output sinks consume a whole chunk or return WouldBlock without consuming it.
 */
class ProcessingRouter final : public QObject
{
    Q_OBJECT
public:
    enum class Direction
    {
        Rx,
        Tx
    };
    /** Independent ingress (including active input) and output queue budgets. */
    struct Limits
    {
        qsizetype maxBytes    = 4 * 1024 * 1024;
        qsizetype maxChunks   = 4096;
        int       eventBudget = 64;
        int       retryMs     = 5;
    };
    enum class State
    {
        Bypass,
        Idle,
        Busy,
        Cancelling,
        Failed
    };
    struct Diagnostics
    {
        State     state          = State::Bypass;
        qsizetype pendingBytes   = 0;
        qsizetype pendingChunks  = 0;
        quint64   acceptedBytes  = 0;
        quint64   forwardedBytes = 0;
        quint64   failures       = 0;
        QString   lastError;
    };
    using Output = std::function<
        Processor::AcceptResult(const Processor::Chunk&, const TerminalSendQueue::Options&, QString*)>;

    explicit ProcessingRouter(QObject* parent = nullptr);
    explicit ProcessingRouter(Limits limits, QObject* parent = nullptr);
    ~ProcessingRouter() override;
    Q_DISABLE_COPY_MOVE(ProcessingRouter)

    void                    setOutput(Direction direction, Output output);
    Processor::AcceptResult submit(Direction                         direction,
                                   Processor::Chunk                  chunk,
                                   const TerminalSendQueue::Options& options = {},
                                   QString*                          error   = nullptr);
    void replace(Direction direction, std::shared_ptr<Processor> processor, std::function<void()> ready = {});
    /** One 2x2 instance owns both directions; ports 0/1 are Rx/Tx. */
    void replaceDuplex(std::shared_ptr<Processor> processor, std::function<void()> ready = {});
    bool duplex() const;
    /** Stop a failed lane (both lanes for duplex) without switching to bypass. */
    void        fail(Direction direction, QString message);
    void        cancel(Direction direction, std::function<void()> ready = {});
    void        wake(Direction direction);
    Diagnostics diagnostics(Direction direction) const;
    qsizetype   pendingBytes(Direction direction) const;
    qsizetype   pendingSubmissions(Direction direction) const;

    Q_SIGNAL void error(Direction direction, const QString& message);
    Q_SIGNAL void capacityAvailable(Direction direction);

private:
    struct Item
    {
        Processor::Chunk           chunk;
        TerminalSendQueue::Options options;
    };
    struct Lane
    {
        std::shared_ptr<Processor>   processor;
        std::shared_ptr<Processor>   replacement;
        QList<std::function<void()>> ready;
        QQueue<Item>                 inputs;
        QQueue<Item>                 outputs;
        Output                       output;
        TerminalSendQueue::Options   activeOptions;
        qsizetype                    inputBytes     = 0;
        qsizetype                    outputBytes    = 0;
        qsizetype                    activeBytes    = 0;
        quint64                      generation     = 0;
        quint64                      acceptedBytes  = 0;
        quint64                      forwardedBytes = 0;
        quint64                      failures       = 0;
        QString                      lastError;
        bool                         active    = false;
        bool                         replacing = false;
        bool                         failed    = false;
    };
    Limits                                    _limits;
    std::array<Lane, 2>                       _lanes;
    QTimer                                    _retry;
    bool                                      _pumping        = false;
    bool                                      _duplex         = false;
    bool                                      _pairReplacing  = false;
    quint64                                   _pairGeneration = 0;
    std::array<std::shared_ptr<Processor>, 2> _pairProcessors;
    QList<std::function<void()>>              _pairReady;
    bool                                      _pairShared = false;

    Lane&       _lane(Direction direction);
    const Lane& _lane(Direction direction) const;
    void        _replaceLane(Direction direction, std::shared_ptr<Processor> processor, std::function<void()> ready);
    void _replacePair(std::array<std::shared_ptr<Processor>, 2> processors, bool shared, std::function<void()> ready);
    void _installHandlers(Direction direction);
    void _output(Direction                         source,
                 quint64                           generation,
                 Processor::Chunk                  chunk,
                 const TerminalSendQueue::Options& options,
                 bool                              requireActive);
    void _pump(Direction direction);
    void _schedule();
    void _clear(Lane& lane);
    static void _retire(std::shared_ptr<Processor> processor, std::function<void()> ready);
};
