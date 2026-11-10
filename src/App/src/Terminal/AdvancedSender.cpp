#include "AdvancedSender.h"

#include "ProcessingRouter.h"

#include <QPointer>

AdvancedSender::AdvancedSender(TerminalSendQueue& queue, SerialDevice& device, QObject* parent) :
    QObject(parent), _queue(queue), _device(device)
{
    _work.setSingleShot(true);
    _period.setSingleShot(true);
    connect(&_work, &QTimer::timeout, this, &AdvancedSender::_pump);
    connect(&_period, &QTimer::timeout, this, [this] {
        _periodicDue = true;
        _work.start(0);
    });
    connect(&_queue, &TerminalSendQueue::changed, this, [this] { _work.start(0); });
    connect(&_queue, &TerminalSendQueue::failed, this, [this] {
        _cancelProducers();
        _publish();
    });
}

void AdvancedSender::setProcessingRouter(ProcessingRouter* router)
{
    if (_router == router)
    {
        return;
    }
    if (_router)
    {
        disconnect(_router, nullptr, this, nullptr);
    }
    _router = router;
    if (_router)
    {
        connect(_router, &ProcessingRouter::capacityAvailable, this, [this](ProcessingRouter::Direction direction) {
            if (direction == ProcessingRouter::Direction::Tx)
            {
                _work.start(0);
                _publish();
            }
        });
    }
}

bool AdvancedSender::_enqueue(const QByteArray& bytes, const TerminalSendQueue::Options& options, QString* error)
{
    if (!_router)
    {
        return _queue.enqueue(bytes, options, error);
    }
    if (_device.state() != SerialDevice::State::Open)
    {
        if (error)
        {
            *error = tr("Processing requires an open terminal.");
        }
        return false;
    }
    return _router->submit(ProcessingRouter::Direction::Tx, {bytes}, options, error)
        == Processor::AcceptResult::Accepted;
}

qsizetype AdvancedSender::_pending() const
{
    return _queue.pendingSubmissions() + (_router ? _router->pendingSubmissions(ProcessingRouter::Direction::Tx) : 0);
}

void AdvancedSender::configure(const QVariantMap& input)
{
    _options.retainOnDisconnect = input.value("disconnectedPolicy", "reject").toString() == "retain";
    _queue.setRetainDisconnected(_options.retainOnDisconnect);
}

bool AdvancedSender::submit(const QByteArray& bytes, QString* error)
{
    return _enqueue(bytes, _options, error);
}

bool AdvancedSender::startFile(const QString& path, bool lineByLine, const QByteArray& terminator, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (terminator.size() > TerminalSendQueue::MaximumPendingBytes)
    {
        if (error)
        {
            *error = tr("File terminator exceeds the 4 MiB output limit.");
        }
        return false;
    }
    if (_fileActive || _periodicActive || _device.state() != SerialDevice::State::Open)
    {
        if (error)
        {
            *error = tr("File sending requires an open terminal and no active producer.");
        }
        return false;
    }
    _file.setFileName(path);
    if (!_file.open(QIODevice::ReadOnly))
    {
        if (error)
        {
            *error = tr("Cannot open file: %1").arg(_file.errorString());
        }
        return false;
    }
    _lineByLine      = lineByLine;
    _terminator      = terminator;
    _fileActive      = true;
    _producerOptions = _options;

    _skipLf = false;
    _line.clear();
    _chunk.clear();
    _chunkOffset = 0;
    _work.start(0);
    _publish();
    return true;
}

bool AdvancedSender::startPeriodic(const QByteArray& bytes, int intervalMs, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (_fileActive || _periodicActive || _device.state() != SerialDevice::State::Open || intervalMs < 100
        || intervalMs > 86400000 || bytes.size() > TerminalSendQueue::MaximumPendingBytes)
    {
        if (error)
        {
            *error = tr(
                "Periodic sending requires an open terminal, no active producer and an interval of 100 ms to 24 hours.");
        }
        return false;
    }
    _periodic        = bytes;
    _producerOptions = _options;
    _period.setInterval(intervalMs);
    _periodicActive = true;
    _periodicDue    = true;
    _work.start(0);
    _publish();
    return true;
}

void AdvancedSender::connectionChanged()
{
    if (_device.state() != SerialDevice::State::Open)
    {
        _cancelProducers();
    }
    const bool                     disconnected  = _device.state() != SerialDevice::State::Open;
    const qsizetype                previousCount = _queue.pendingSubmissions();
    const qsizetype                previousBytes = _queue.pendingBytes();
    const QPointer<AdvancedSender> self(this);
    _queue.connectionChanged();
    if (!self)
    {
        return;
    }
    const qsizetype discarded      = previousCount - _queue.pendingSubmissions();
    const qsizetype discardedBytes = previousBytes - _queue.pendingBytes();
    _publish();
    if (self && disconnected && discarded > 0)
    {
        // Publish after the backend's synchronous state/error callback stack.
        QTimer::singleShot(0, this, [this, discarded, discardedBytes] {
            emit failed(
                tr("Disconnected: discarded %1 pending submissions (%2 bytes).").arg(discarded).arg(discardedBytes));
        });
    }
}

void AdvancedSender::stop()
{
    _cancelProducers();
    const QPointer<AdvancedSender> self(this);
    emit                           stopped();
    if (!self)
    {
        return;
    }
    _queue.reset();
    if (self)
    {
        _publish();
    }
}

void AdvancedSender::stopProducers()
{
    _cancelProducers();
    _publish();
}

bool AdvancedSender::active() const
{
    return _fileActive || _periodicActive || _pending() > 0;
}

void AdvancedSender::_pump()
{
    if (_device.state() != SerialDevice::State::Open || _pending() != 0)
    {
        _publish();
        return;
    }
    if (_periodicActive)
    {
        if (_periodicDue)
        {
            QString error;
            auto    options            = _producerOptions;
            options.retainOnDisconnect = false;
            const QPointer<AdvancedSender> self(this);
            const quint64                  generation = _generation;
            const bool                     accepted   = _enqueue(_periodic, options, &error);
            if (!self || _generation != generation)
            {
                return;
            }
            if (!accepted)
            {
                _cancelProducers();
                _publish();
                if (self)
                {
                    emit failed(error);
                }
                return;
            }
            _periodicDue = false;
        }
        else if (!_period.isActive())
        {
            _period.start();
        }
        return;
    }
    if (!_fileActive)
    {
        _publish();
        return;
    }

    QByteArray output;
    bool       haveOutput = false;
    if (!_lineByLine)
    {
        output     = _file.read(_device.atomicWrites() ? 65507 : 65536);
        haveOutput = !output.isEmpty();
    }
    else
    {
        // Limit both filesystem work and parsing per GUI turn; a logical line is bounded separately.
        if (_chunkOffset == _chunk.size())
        {
            _chunk       = _file.read(65536);
            _chunkOffset = 0;
        }
        while (_chunkOffset < _chunk.size())
        {
            const char byte = _chunk.at(_chunkOffset++);
            if (_skipLf)
            {
                _skipLf = false;
                if (byte == '\n')
                {
                    continue;
                }
            }
            if (byte == '\r' || byte == '\n')
            {
                _skipLf = byte == '\r';
                output  = std::move(_line);
                _line.clear();
                output.append(_terminator);
                haveOutput = true;
                break;
            }
            _line.append(byte);
            if (_line.size() > TerminalSendQueue::MaximumPendingBytes - _terminator.size())
            {
                _fileFailure(tr("File line exceeds the 4 MiB output limit."));
                return;
            }
        }
        if (!haveOutput && _file.atEnd() && _chunkOffset == _chunk.size() && !_line.isEmpty())
        {
            output = std::move(_line);
            _line.clear();
            output.append(_terminator);
            haveOutput = true;
        }
    }
    if (_file.error() != QFileDevice::NoError)
    {
        _fileFailure(tr("Cannot read file: %1").arg(_file.errorString()));
        return;
    }
    if (haveOutput)
    {
        QString error;
        auto    options            = _producerOptions;
        options.retainOnDisconnect = false;
        const QPointer<AdvancedSender> self(this);
        const quint64                  generation = _generation;
        const bool                     accepted   = _enqueue(output, options, &error);
        if (!self || _generation != generation)
        {
            return;
        }
        if (!accepted)
        {
            _fileFailure(error);
            return;
        }
        _publish();
    }
    else if (_file.atEnd() && _chunkOffset == _chunk.size())
    {
        _file.close();
        _fileActive = false;
        _publish();
    }
    else
    {
        _work.start(0);
    }
}

void AdvancedSender::_publish()
{
    const QString status = _fileActive     ? tr("Sending file В· %1 bytes queued").arg(_queue.pendingBytes()) :
                           _periodicActive ? tr("Periodic sending active") :
                           _queue.pendingSubmissions() ? tr("%1 submissions queued В· %2 bytes")
                                                             .arg(_queue.pendingSubmissions())
                                                             .arg(_queue.pendingBytes()) :
                                                         QString {};
    emit          stateChanged(_fileActive || _periodicActive, status);
}

void AdvancedSender::_cancelProducers()
{
    ++_generation;
    _work.stop();
    _period.stop();
    _file.close();
    _fileActive     = false;
    _periodicActive = false;
    _periodicDue    = false;

    _periodic.clear();
    _line.clear();
    _chunk.clear();
    _chunkOffset = 0;
}

void AdvancedSender::_fileFailure(const QString& message)
{
    _cancelProducers();
    const QPointer<AdvancedSender> self(this);
    _publish();
    if (self)
    {
        emit failed(message);
    }
}
