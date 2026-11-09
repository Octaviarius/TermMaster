#include <QPointer>
#include <Termaster/Core/Terminal/TerminalSendQueue.h>

TerminalSendQueue::TerminalSendQueue(SerialDevice& device, QObject* parent) : QObject(parent), _device(device)
{
    _retry.setSingleShot(true);
    connect(&_retry, &QTimer::timeout, this, &TerminalSendQueue::flush);
}

bool TerminalSendQueue::enqueue(const QByteArray& bytes, QString* error)
{
    return enqueue(bytes, Options {}, error);
}

bool TerminalSendQueue::enqueue(const QByteArray& bytes, const Options& options, QString* error)
{
    if (error)
    {
        error->clear();
    }
    const auto reject = [error](const QString& message) {
        if (error)
        {
            *error = message;
        }
        return false;
    };
    if (_device.state() != SerialDevice::State::Open && !(_retainDisconnected && options.retainOnDisconnect))
    {
        return reject(QStringLiteral("Terminal is disconnected."));
    }
    if (bytes.size() > MaximumPendingBytes - _pendingBytes || _pending.size() >= 4096)
    {
        return reject(QStringLiteral("Terminal output queue is full (4 MiB / 4096 submissions limit)."));
    }

    _pending.enqueue({bytes, options, false});
    _pendingBytes += bytes.size();
    if (_device.state() == SerialDevice::State::Open && !_retry.isActive())
    {
        _retry.start(0);
    }
    emit changed();
    return true;
}

qsizetype TerminalSendQueue::pendingBytes() const
{
    return _pendingBytes;
}

qsizetype TerminalSendQueue::pendingSubmissions() const
{
    return _pending.size();
}

void TerminalSendQueue::setRetainDisconnected(bool enabled)
{
    _retainDisconnected = enabled;
    if (!enabled)
    {
        for (auto& submission : _pending)
        {
            submission.options.retainOnDisconnect = false;
        }
    }
    connectionChanged();
}

void TerminalSendQueue::connectionChanged()
{
    if (_device.state() == SerialDevice::State::Open)
    {
        if (!_pending.isEmpty() && !_retry.isActive())
        {
            _retry.start(0);
        }
        return;
    }
    ++_generation;
    _retry.stop();
    for (qsizetype index = _pending.size(); index > 0; --index)
    {
        const auto& submission = _pending.at(index - 1);
        // An in-flight write may already have accepted a prefix before reporting closure.
        if (submission.started || (_flushing && index == 1) || !_retainDisconnected
            || !submission.options.retainOnDisconnect)
        {
            _pendingBytes -= submission.bytes.size();
            _pending.removeAt(index - 1);
        }
    }
    emit changed();
}

void TerminalSendQueue::flush()
{
    if (_flushing)
    {
        return;
    }
    if (_device.state() != SerialDevice::State::Open)
    {
        connectionChanged();
        return;
    }
    if (_pending.isEmpty())
    {
        return;
    }
    const QByteArray bytes                       = _pending.head().bytes;
    _flushing                                    = true;
    const quint64                     generation = _generation;
    const QPointer<TerminalSendQueue> self(this);
    QString                           error;
    const qint64                      accepted = _device.write(bytes, &error);
    if (!self)
    {
        return;
    }
    _flushing = false;
    if (_generation != generation || _pending.isEmpty())
    {
        return;
    }
    if (accepted < 0 || accepted > bytes.size() || (_device.atomicWrites() && accepted > 0 && accepted != bytes.size()))
    {
        reset();
        if (self)
        {
            emit failed(error.isEmpty() ? QStringLiteral("Terminal output write failed.") : error);
        }
        return;
    }
    if (accepted > 0)
    {
        _pending.head().started = true;
    }
    if (_device.state() != SerialDevice::State::Open)
    {
        connectionChanged();
        return;
    }
    if (accepted > 0 || bytes.isEmpty())
    {
        _pendingBytes -= accepted;
        if (accepted == _pending.head().bytes.size())
        {
            _pending.dequeue();
        }
        else
        {
            _pending.head().bytes.remove(0, accepted);
        }
    }
    if (!_pending.isEmpty())
    {
        _retry.start(accepted == 0 && !bytes.isEmpty() ? 10 : 0);
    }
    emit changed();
}

void TerminalSendQueue::reset()
{
    ++_generation;
    _retry.stop();
    _pending.clear();
    _pendingBytes = 0;
    emit changed();
}
