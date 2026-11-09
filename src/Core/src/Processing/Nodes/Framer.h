#pragma once

#include <Termaster/Core/Processing/Processor.h>
#include <memory>
#include <vector>

/** Stateful delimiter/size byte framer. Calls and callbacks use the owning thread. */
class Framer final : public Processor
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap&       configuration,
                                             const Processor::Limits& limits,
                                             QString*                 error = nullptr);
    Q_DISABLE_COPY_MOVE(Framer)

    QVariantMap  configuration() const override;
    AcceptResult accept(Chunk input, Callbacks callbacks) override;
    void         cancel(std::function<void()> quiescent) override;

private:
    enum class Mode
    {
        Delimiter,
        Size,
        Either
    };
    enum class Tail
    {
        Preserve,
        Emit,
        Discard,
        Error
    };

    QVariantMap            _configuration;
    Limits                 _limits;
    Mode                   _mode;
    Tail                   _tail;
    QByteArray             _ending;
    qsizetype              _frameSize;
    qsizetype              _maximum;
    bool                   _includeEnding;
    bool                   _emitEmpty;
    std::vector<qsizetype> _prefix;
    QByteArray             _buffer;
    qsizetype              _matched = 0;
    bool                   _busy    = false;
    bool                   _stopped = false;

    Framer(QVariantMap configuration,
           Limits      limits,
           Mode        mode,
           Tail        tail,
           QByteArray  ending,
           qsizetype   frameSize,
           qsizetype   maximum);
};
