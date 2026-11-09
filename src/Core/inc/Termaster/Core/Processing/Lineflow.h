#pragma once

#include <QStringList>
#include <Termaster/Core/Processing/Processor.h>
#include <array>
#include <deque>
#include <memory>
#include <vector>
class ProcessorRegistry;

/** Prepared native pipeline and one independently stateful runtime. Owning-thread API.
 * Each stage has at most one accepted invocation; its output may advance while it is pending.
 * maxChunks bounds resident queued work. Total output bytes per child/root invocation remain
 * bounded by maxBytes; max(maxBytes,maxChunks) bounds emissions, including empty chunks.
 */
class Lineflow final : public Processor
{
public:
    class Prepared
    {
    public:
        QVariantMap               configuration() const;
        int                       inputCount() const;
        int                       outputCount() const;
        std::unique_ptr<Lineflow> instantiate(ProcessorRegistry& registry, QString* error = nullptr) const;
        std::unique_ptr<Lineflow> instantiate(ProcessorRegistry& registry,
                                              const QVariantMap& overrides,
                                              QString*           error = nullptr) const;

    private:
        friend class Lineflow;
        struct Node
        {
            QString     path;
            QString     type;
            QVariantMap configuration;
            int         lane = 0;
        };

        QVariantMap       _configuration;
        std::vector<Node> _nodes;
        Limits            _limits;
        int               _channels = 1;
    };

    static std::shared_ptr<const Prepared> prepare(const QVariantMap& blocks,
                                                   const QString&     id,
                                                   ProcessorRegistry& registry,
                                                   Limits             limits,
                                                   QString*           error = nullptr);
    ~Lineflow() override;

    QVariantMap  configuration() const override;
    int          inputCount() const override;
    int          outputCount() const override;
    AcceptResult accept(Chunk input, Callbacks callbacks) override;
    void         cancel(std::function<void()> quiescent) override;

private:
    friend class ProcessorRegistry;
    static std::unique_ptr<Lineflow> compose(std::vector<std::shared_ptr<Processor>> nodes,
                                             QVariantMap                             configuration,
                                             Limits                                  limits,
                                             QStringList                             nodeNames = {},
                                             int                                     channels  = 1,
                                             std::vector<int>                        lanes     = {});

    struct Budget
    {
        qsizetype remaining;
    };
    struct Work
    {
        size_t                  stage;
        Chunk                   chunk;
        int                     root = -1;
        std::shared_ptr<Budget> budget;
    };
    struct Invocation
    {
        qsizetype               bytes      = 0;
        qsizetype               chunks     = 0;
        quint64                 invocation = 0;
        bool                    pending    = false;
        int                     root       = -1;
        std::shared_ptr<Budget> budget;
    };

    struct Stage
    {
        std::array<Invocation, 2> inputs;
    };

    struct Root
    {
        Callbacks callbacks;
        qsizetype outputBytes  = 0;
        qsizetype outputChunks = 0;
        bool      active       = false;
    };

    QVariantMap                             _configuration;
    Limits                                  _limits;
    std::vector<std::shared_ptr<Processor>> _nodes;
    QStringList                             _nodeNames;
    std::deque<Work>                        _queue;
    std::vector<Stage>                      _stages;
    std::vector<Root>                       _roots;
    std::vector<int>                        _lanes;
    int                                     _channels = 1;
    std::vector<std::function<void()>>      _cancelCallbacks;
    qsizetype                               _queuedBytes = 0;
    quint64                                 _generation  = 0;
    size_t                                  _cancelling  = 0;
    bool                                    _pumping     = false;
    bool                                    _scheduled   = false;
    bool                                    _stopped     = false;
    bool                                    _quiescent   = false;
    QString                                 _failure;

    explicit Lineflow(const Prepared& prepared);
    void    _schedule();
    void    _pump();
    void    _finish(int root);
    size_t  _nextStage(int from, int direction) const;
    void    _enqueueOutput(size_t stage, Chunk chunk, int root, int inputPort, std::shared_ptr<Budget> budget);
    void    _connectOutputs();
    void    _fail(QString error);
    QString _nodeError(size_t stage, const QString& error) const;
    void    _cancelNodes();
    void    _cancelled();
};
