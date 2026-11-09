#include <QPointer>
#include <Termaster/Core/Terminal/TerminalSendQueue.h>

TerminalSendQueue::TerminalSendQueue(SerialDevice& device, QObject* parent) : QObject(parent), _device(device)
{
    _clock.start();
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
    if (options.byteDelayMs < 0 || options.byteDelayMs > 60000 || options.lineDelayMs < 0
        || options.lineDelayMs > 60000)
    {
        return reject(QStringLiteral("Output delays must be between 0 and 60000 ms."));
    }
    if (_device.atomicWrites() && options.byteDelayMs != 0)
    {
        return reject(QStringLiteral("Byte pacing is unavailable for datagram terminals."));
    }
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
    _readyAt = 0;
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
    const qint64 wait = _readyAt - _clock.elapsed();
    if (wait > 0)
    {
        _retry.start(static_cast<int>(wait));
        return;
    }

    const Submission& submission = _pending.head();
    if (_device.atomicWrites() && submission.options.byteDelayMs != 0)
    {
        const QPointer<TerminalSendQueue> self(this);
        reset();
        if (self)
        {
            emit failed(QStringLiteral("Byte pacing is unavailable for datagram terminals."));
        }
        return;
    }
    qsizetype count = submission.bytes.size();
    if (!_device.atomicWrites())
    {
        if (submission.options.byteDelayMs > 0)
        {
            count = qMin<qsizetype>(count, 1);
        }
        else if (submission.options.lineDelayMs > 0)
        {
            for (qsizetype index = 0; index < count; ++index)
            {
                if (submission.bytes.at(index) == '\n' || submission.bytes.at(index) == '\r')
                {
                    count = index + 1;
                    if (submission.bytes.at(index) == '\r' && count < submission.bytes.size()
                        && submission.bytes.at(count) == '\n')
                    {
                        ++count;
                    }
                    break;
                }
            }
        }
    }
    const QByteArray bytes                       = submission.bytes.first(count);
    const Options    options                     = submission.options;
    const bool       crBeforeLf                  = count < submission.bytes.size() && count > 0 && bytes.back() == '\r'
                                                && submission.bytes.at(count) == '\n';
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
        int delay      = options.byteDelayMs;
        if (accepted > 0)
        {
            const char last         = bytes.at(accepted - 1);
            const bool followedByLf = accepted < bytes.size() ? bytes.at(accepted) == '\n' : crBeforeLf;
            if (last == '\n' || (last == '\r' && !followedByLf))
            {
                delay += options.lineDelayMs;
            }
        }
        if (options.lineSubmission && accepted == _pending.head().bytes.size())
        {
            delay = qMax(delay, options.byteDelayMs + options.lineDelayMs);
        }
        _readyAt = _clock.elapsed() + delay;
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
        _retry.start(
            accepted == 0 && !bytes.isEmpty() ? 10 : static_cast<int>(qMax<qint64>(0, _readyAt - _clock.elapsed())));
    }
    emit changed();
}

void TerminalSendQueue::reset()
{
    ++_generation;
    _retry.stop();
    _pending.clear();
    _pendingBytes = 0;
    _readyAt      = 0;
    emit changed();
}
