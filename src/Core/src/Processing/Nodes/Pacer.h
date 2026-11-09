#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <Termaster/Core/Processing/Processor.h>
#include <memory>
#include <vector>

/** Owner-thread byte pacer. Byte spacing, line pauses and rate limits are concurrent
 * constraints; the longest applicable wait wins. Stream fragments preserve bytes in order.
 * Packet input stays atomic: byte spacing is unsupported; rate scheduling precharges any
 * bytes exceeding the burst, and all matched line pauses are applied after the packet.
 * Timing and split-ending state survive invocations. Packet boundaries reset ending state.
 * One owned input and one output fragment are resident; maxChunks does not limit the total
 * number of fragments emitted over time. Input and burst sizes respect maxBytes.
 */
class Pacer final : public Processor
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);
    Q_DISABLE_COPY_MOVE(Pacer)

    QVariantMap  configuration() const override;
    AcceptResult accept(Chunk input, Callbacks callbacks) override;
    void         cancel(std::function<void()> quiescent) override;

private:
    struct Fragment
    {
        qsizetype length  = 0;
        qsizetype matched = 0;
        qsizetype endings = 0;
    };

    QVariantMap            _configuration;
    Limits                 _limits;
    QByteArray             _ending;
    std::vector<qsizetype> _prefix;
    qint64                 _byteDelayNs;
    qint64                 _lineDelayNs;
    qint64                 _rate;
    qsizetype              _burst;
    QElapsedTimer          _clock;
    QTimer                 _timer;
    Chunk                  _input;
    Callbacks              _callbacks;
    qsizetype              _offset  = 0;
    qsizetype              _matched = 0;
    Fragment               _fragment;
    qint64                 _timingReadyNs   = 0;
    qint64                 _rateFinishNs    = 0;
    qint64                 _plannedFinishNs = 0;
    qint64                 _readyNs         = 0;
    bool                   _planned         = false;
    bool                   _busy            = false;
    bool                   _stopped         = false;

    Pacer(QVariantMap configuration, Limits limits, QByteArray ending);
    Fragment _nextFragment(const QByteArray& bytes, qsizetype offset, qsizetype matched, Boundary boundary) const;
    void     _pump();
    void     _complete(QString error = {});
};
