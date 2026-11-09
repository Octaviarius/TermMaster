#include <QPointer>
#include <Termaster/Core/Terminal/TerminalSendQueue.h>

TerminalSendQueue::TerminalSendQueue(SerialDevice& device, QObject* parent) : QObject(parent), _device(device)
{
    _retry.setSingleShot(true);
    _retry.setInterval(10);
    connect(&_retry, &QTimer::timeout, this, &TerminalSendQueue::flush);
}

bool TerminalSendQueue::enqueue(const QByteArray& bytes, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (_device.state() != SerialDevice::State::Open)
    {
        reset();
        if (error)
        {
            *error = QStringLiteral("Terminal is disconnected.");
        }
        return false;
    }
    if (bytes.size() > MaximumPendingBytes - _pendingBytes || _pending.size() >= 4096)
    {
        if (error)
        {
            *error = QStringLiteral("Terminal output queue is full (4 MiB / 4096 submissions limit).");
        }
        return false;
    }

    _pending.enqueue(bytes);
    _pendingBytes += bytes.size();
    // Defer writes so acceptance and failure reporting cannot re-enter submission UI.
    if (!_pending.isEmpty() && !_retry.isActive())
    {
        _retry.start(0);
    }
    return true;
}

qsizetype TerminalSendQueue::pendingBytes() const
{
    return _pendingBytes;
}

void TerminalSendQueue::flush()
{
    if (_flushing)
    {
        return;
    }
    if (_device.state() != SerialDevice::State::Open)
    {
        reset();
        return;
    }

    // One bounded write per event-loop turn; preserve separate enqueue boundaries.
    if (_pending.isEmpty())
    {
        return;
    }
    _flushing                                    = true;
    const quint64                     generation = _generation;
    const QPointer<TerminalSendQueue> self(this);
    const QByteArray                  bytes = _pending.head();
    QString                           error;
    const qint64                      accepted = _device.write(bytes, &error);
    if (!self)
    {
        return;
    }
    _flushing = false;
    // Callbacks may reset and replace submissions while write is running.
    if (_generation != generation || _pending.isEmpty())
    {
        return;
    }
    if (accepted < 0 || accepted > bytes.size())
    {
        reset();
        emit failed(error.isEmpty() ? QStringLiteral("Terminal output write failed.") : error);
        return;
    }
    if (_device.state() != SerialDevice::State::Open)
    {
        reset();
        return;
    }
    if (accepted > 0 || bytes.isEmpty())
    {
        _pendingBytes -= accepted;
        if (accepted == bytes.size())
        {
            _pending.dequeue();
        }
        else
        {
            _pending.head().remove(0, accepted);
        }
    }
    if (!_pending.isEmpty())
    {
        _retry.start(accepted == 0 ? 10 : 0);
    }
}

void TerminalSendQueue::reset()
{
    ++_generation;
    _retry.stop();
    _pending.clear();
    _pendingBytes = 0;
}
