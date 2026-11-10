#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <Termaster/Core/Processing/Processor.h>
#include <memory>

/** Owner-thread idle-gap framer. Empty inputs neither create frames nor reset the gap.
 * Frames use the lifetime output channel, independently of input acknowledgements.
 */
class TimedFramer final : public Processor
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);
    Q_DISABLE_COPY_MOVE(TimedFramer)

    QVariantMap  configuration() const override;
    QString      inputName(int port) const override;
    QString      outputName(int port) const override;
    AcceptResult accept(Chunk input, Callbacks callbacks) override;
    void         cancel(std::function<void()> quiescent) override;

private:
    QVariantMap   _configuration;
    Limits        _limits;
    int           _gapMs;
    QElapsedTimer _idle;
    QTimer        _timer;
    QByteArray    _buffer;
    bool          _busy    = false;
    bool          _stopped = false;

    TimedFramer(QVariantMap configuration, Limits limits);
    void _flush();
    void _timeout();
};
