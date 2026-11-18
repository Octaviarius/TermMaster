#pragma once

#include <Termaster/Core/Processing/Processor.h>
#include <deque>
#include <memory>
#include <vector>

/** One unchanged main output plus an independently queued, discarded-output branch.
 * All state and callbacks belong to the graph thread. A branch error or queue
 * overflow fails the Tap; cancellation joins the owned branch before acknowledgement.
 */
class Tap final : public Processor
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);
    static std::unique_ptr<Processor> compose(std::shared_ptr<Processor> branch,
                                              QVariantMap                configuration,
                                              Limits                     limits,
                                              QString*                   error = nullptr);

    QVariantMap  configuration() const override;
    AcceptResult accept(Chunk input, Callbacks callbacks) override;
    void         cancel(std::function<void()> quiescent) override;

private:
    QVariantMap                        _configuration;
    Limits                             _limits;
    std::shared_ptr<Processor>         _branch;
    std::deque<Chunk>                  _queue;
    std::vector<std::function<void()>> _cancelCallbacks;
    qsizetype                          _pendingBytes = 0;
    qsizetype                          _activeBytes  = 0;
    qsizetype                          _outputBytes  = 0;
    qsizetype                          _outputChunks = 0;
    quint64                            _generation   = 0;
    bool                               _busy         = false;
    bool                               _accepting    = false;
    bool                               _scheduled    = false;
    bool                               _stopped      = false;
    bool                               _cancelling   = false;
    bool                               _quiescent    = false;
    QString                            _failure;

    Tap(std::shared_ptr<Processor> branch, QVariantMap configuration, Limits limits);
    void _schedule(int delay = 0);
    void _pump();
    void _fail(QString error);
    void _cancelBranch();
    void _cancelled();
};
