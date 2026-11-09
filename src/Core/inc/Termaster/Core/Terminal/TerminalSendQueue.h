#pragma once

#include <QObject>
#include <QQueue>
#include <QTimer>
#include <Termaster/Core/Terminal/SerialDevice.h>

/** GUI-thread output queue. The borrowed device must outlive this object.
 * The owner calls reset on every non-open state transition, including close/reopen.
 * Each submission retains its transport message boundary.
 */
class TerminalSendQueue : public QObject
{
    Q_OBJECT
public:
    static constexpr qsizetype MaximumPendingBytes = 4 * 1024 * 1024;

    explicit TerminalSendQueue(SerialDevice& device, QObject* parent = nullptr);
    bool      enqueue(const QByteArray& bytes, QString* error = nullptr);
    qsizetype pendingBytes() const;
    void      flush();
    void      reset();

    Q_SIGNAL void failed(const QString& error);

private:
    SerialDevice&      _device;
    QTimer             _retry;
    QQueue<QByteArray> _pending;
    qsizetype          _pendingBytes = 0;
    quint64            _generation   = 0;
    bool               _flushing     = false;
};
