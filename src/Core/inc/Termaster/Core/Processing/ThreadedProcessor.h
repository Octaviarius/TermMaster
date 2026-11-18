#pragma once

#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <memory>
#include <vector>

/** Owner-thread facade for a tree constructed and executed on a private Qt worker.
 * Calls and callbacks use the facade thread. Whole-packet admission and output delivery
 * are bounded by the preparation limits. Cancellation acknowledges after worker teardown.
 */
class ThreadedProcessor final : public Processor
{
public:
    static std::shared_ptr<Processor> create(std::shared_ptr<const ProcessorRegistry::Prepared> prepared,
                                             const QVariantMap&                                 overrides = {},
                                             QString*                                           error     = nullptr,
                                             const QString&                                     directory = {});

    ~ThreadedProcessor() override;
    Q_DISABLE_COPY_MOVE(ThreadedProcessor)

    QVariantMap  configuration() const override;
    int          inputCount() const override;
    int          outputCount() const override;
    QString      inputName(int port) const override;
    QString      outputName(int port) const override;
    AcceptResult accept(Chunk input, Callbacks callbacks) override;
    void         cancel(std::function<void()> quiescent) override;

private:
    struct State;
    struct Pending
    {
        quint64   id    = 0;
        qsizetype bytes = 0;
        Callbacks callbacks;
    };

    std::shared_ptr<State>             _state;
    std::vector<Pending>               _pending;
    std::vector<std::function<void()>> _quiescent;
    quint64                            _nextId        = 0;
    qsizetype                          _pendingBytes  = 0;
    qsizetype                          _pendingChunks = 0;
    bool                               _stopped       = false;
    bool                               _finished      = false;

    explicit ThreadedProcessor(std::shared_ptr<State> state);
    void _drain();
    void _finish();
    void _fail(QString error);
};
