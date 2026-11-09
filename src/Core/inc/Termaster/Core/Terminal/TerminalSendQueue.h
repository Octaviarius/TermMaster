#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QTimer>
#include <Termaster/Core/Terminal/SerialDevice.h>

/** GUI-thread output queue. The borrowed device must outlive this object.
 * The owner reports connection transitions through connectionChanged().
 * Each submission retains its transport message boundary.
 */
class TerminalSendQueue : public QObject
{
    Q_OBJECT
public:
    struct Options
    {
        int  byteDelayMs        = 0;
        int  lineDelayMs        = 0;
        bool retainOnDisconnect = false;
        bool lineSubmission     = false;
    };
    static constexpr qsizetype MaximumPendingBytes = 4 * 1024 * 1024;

    explicit TerminalSendQueue(SerialDevice& device, QObject* parent = nullptr);
    bool      enqueue(const QByteArray& bytes, QString* error = nullptr);
    bool      enqueue(const QByteArray& bytes, const Options& options, QString* error = nullptr);
    qsizetype pendingBytes() const;
    qsizetype pendingSubmissions() const;
    void      setRetainDisconnected(bool enabled);
    void      connectionChanged();
    void      flush();
    void      reset();

    Q_SIGNAL void failed(const QString& error);
    Q_SIGNAL void changed();

private:
    struct Submission
    {
        QByteArray bytes;
        Options    options;
        bool       started = false;
    };
    SerialDevice&      _device;
    QTimer             _retry;
    QElapsedTimer      _clock;
    QQueue<Submission> _pending;
    qsizetype          _pendingBytes       = 0;
    quint64            _generation         = 0;
    qint64             _readyAt            = 0;
    bool               _flushing           = false;
    bool               _retainDisconnected = false;
};
