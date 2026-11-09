#pragma once

#include <QFile>
#include <QObject>
#include <QTimer>
#include <Termaster/Core/Terminal/TerminalSendQueue.h>

/** GUI-thread producer borrowing the runtime queue and device. No file or periodic state is restored. */
class AdvancedSender final : public QObject
{
    Q_OBJECT
public:
    AdvancedSender(TerminalSendQueue& queue, SerialDevice& device, QObject* parent = nullptr);
    void configure(const QVariantMap& input);
    bool submit(const QByteArray& bytes, QString* error = nullptr);
    bool startFile(const QString& path, bool lineByLine, const QByteArray& terminator, QString* error = nullptr);
    bool startPeriodic(const QByteArray& bytes, int intervalMs, QString* error = nullptr);
    void connectionChanged();
    void stop();
    bool active() const;
    Q_SIGNAL void stateChanged(bool active, const QString& status);
    Q_SIGNAL void failed(const QString& error);

private:
    TerminalSendQueue&         _queue;
    SerialDevice&              _device;
    QTimer                     _work;
    QTimer                     _period;
    QFile                      _file;
    QByteArray                 _line;
    QByteArray                 _terminator;
    QByteArray                 _periodic;
    QByteArray                 _chunk;
    qsizetype                  _chunkOffset = 0;
    TerminalSendQueue::Options _options;
    bool                       _fileActive     = false;
    bool                       _lineByLine     = false;
    bool                       _skipLf         = false;
    bool                       _periodicActive = false;
    bool                       _periodicDue    = false;
    TerminalSendQueue::Options _producerOptions;
    quint64                    _generation = 0;

    void _pump();
    void _publish();
    void _cancelProducers();
    void _fileFailure(const QString& message);
};
