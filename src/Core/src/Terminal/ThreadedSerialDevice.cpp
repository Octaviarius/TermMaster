#include "../Threading/ThreadJoinGuard.h"

#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QQueue>
#include <QThread>
#include <QTimer>
#include <Termaster/Core/Terminal/ThreadedSerialDevice.h>
#include <exception>
#include <stdexcept>

namespace
{
Q_LOGGING_CATEGORY(transportWorkerLog, "termaster.transport.worker")
Q_LOGGING_CATEGORY(transportWorkerTrace, "termaster.transport.worker.trace", QtInfoMsg)
} // namespace

struct ThreadedSerialDevice::Private
{
    enum class Kind
    {
        Received,
        Written,
        State,
        Error,
        Exited
    };
    struct Event
    {
        Kind       kind = Kind::State;
        QByteArray bytes;
        QString    error;
        qint64     count         = 0;
        State      state         = State::Closed;
        bool       atomicWrites  = false;
        bool       autoReconnect = true;
        quint64    generation    = 0;
    };

    ThreadedSerialDevice*         _owner;
    Factory                       _factory;
    ThreadJoinGuard               _nativeJoin;
    QThread                       _thread;
    QObject*                      _worker = new QObject;
    QTimer*                       _retry  = nullptr;
    std::unique_ptr<SerialDevice> _device;
    QMutex                        _mutex;
    QQueue<QByteArray>            _output;
    QQueue<Event>                 _events;
    QVariantMap                   _configuration;
    qint64                        _queuedBytes          = 0;
    qint64                        _backendPendingBytes  = 0;
    qint64                        _inFlightBytes        = 0;
    qint64                        _inFlightWritten      = 0;
    quint64                       _outputEpoch          = 0;
    bool                          _writing              = false;
    qint64                        _receivedBytes        = 0;
    quint64                       _generation           = 0;
    quint64                       _workerGeneration     = 0;
    bool                          _requestedOpen        = false;
    bool                          _commandPending       = false;
    bool                          _wakeScheduled        = false;
    bool                          _dispatchScheduled    = false;
    bool                          _stopping             = false;
    bool                          _accepting            = false;
    bool                          _failed               = false;
    bool                          _initialAtomicWrites  = false;
    bool                          _initialAutoReconnect = true;
    bool                          _geometryPending      = false;
    int                           _columns              = 0;
    int                           _rows                 = 0;

    Private(ThreadedSerialDevice* owner, Factory factory) : _owner(owner), _factory(std::move(factory))
    {
        _worker->moveToThread(&_thread);
        QObject::connect(&_thread, &QThread::finished, _worker, &QObject::deleteLater);
        _thread.setObjectName(QStringLiteral("TerminalTransport"));
        _thread.start();
        qCDebug(transportWorkerTrace) << "Transport worker started";
        std::exception_ptr failure;
        QMetaObject::invokeMethod(
            _worker,
            [this, &failure] {
                _retry = new QTimer(_worker);
                _retry->setSingleShot(true);
                QObject::connect(_retry, &QTimer::timeout, _worker, [this] { _process(); });
                try
                {
                    QString error;
                    if (!_nativeJoin.captureCurrentThread(&error))
                    {
                        throw std::runtime_error(error.toStdString());
                    }
                    _device = _factory ? _factory() : nullptr;
                    if (!_device)
                    {
                        throw std::runtime_error("Transport factory returned no device.");
                    }
                    auto* object = _device->object();
                    if (!object || object->thread() != QThread::currentThread())
                    {
                        throw std::runtime_error("Transport device must expose an object on its worker thread.");
                    }
                    _initialAtomicWrites  = _device->atomicWrites();
                    _initialAutoReconnect = _device->autoReconnect();
                }
                catch (...)
                {
                    _device.reset();
                    _factory = {};
                    failure  = std::current_exception();
                }
            },
            Qt::BlockingQueuedConnection);
        if (failure)
        {
            _joinThread();
            std::rethrow_exception(failure);
        }
    }

    ~Private()
    {
        {
            QMutexLocker lock(&_mutex);
            _stopping = true;
            ++_generation;
            _events.clear();
            _output.clear();
        }
        // Worker callbacks only enqueue owner notifications; they never wait for the owner.
        QMetaObject::invokeMethod(
            _worker,
            [this] {
                _retry->stop();
                if (_device)
                {
                    _device->setCallbacks({});
                    _device->close();
                    _device.reset();
                }
                _factory = {};
            },
            Qt::BlockingQueuedConnection);
        _joinThread();
        qCDebug(transportWorkerTrace) << "Transport worker joined";
    }

    void _joinThread()
    {
        _thread.quit();
        if (!_thread.wait())
        {
            qFatal("Transport worker Qt join failed.");
        }
        QString error;
        if (!_nativeJoin.waitAndClose(&error))
        {
            // Returning would let the caller unload provider code still used by native TLS.
            qFatal("Transport worker native join failed: %s", qPrintable(error));
        }
    }

    qint64 _pendingBytesLocked() const
    {
        return _queuedBytes + _backendPendingBytes + _inFlightBytes - _inFlightWritten;
    }

    void _clearOutputLocked()
    {
        ++_outputEpoch;
        _output.clear();
        _queuedBytes         = 0;
        _backendPendingBytes = 0;
        _inFlightBytes       = 0;
        _inFlightWritten     = 0;
        _writing             = false;
    }

    void _wakeLocked()
    {
        if (_wakeScheduled || _stopping)
        {
            return;
        }
        _wakeScheduled = true;
        QMetaObject::invokeMethod(_worker, [this] { _process(); }, Qt::QueuedConnection);
    }

    bool _current(quint64 generation)
    {
        QMutexLocker lock(&_mutex);
        return !_stopping && generation == _generation;
    }

    void _dispatchLocked()
    {
        if (!_dispatchScheduled && !_stopping)
        {
            _dispatchScheduled = true;
            QMetaObject::invokeMethod(_owner, [owner = _owner] { owner->_dispatch(); }, Qt::QueuedConnection);
        }
    }

    bool _enqueue(Event event)
    {
        QMutexLocker lock(&_mutex);
        if (_stopping || event.generation != _generation)
        {
            return true;
        }
        if (event.kind == Kind::Written && !_events.isEmpty() && _events.back().kind == Kind::Written)
        {
            _events.back().count += event.count;
            return true;
        }
        if (_events.size() >= MaximumCallbackEvents || event.bytes.size() > MaximumReceivedBytes - _receivedBytes)
        {
            return false;
        }
        if (event.kind == Kind::Received)
        {
            // Copy only after admission; plugin callbacks may expose fromRawData storage.
            event.bytes = QByteArray(event.bytes.constData(), event.bytes.size());
        }
        if (event.kind == Kind::Error)
        {
            event.error = QString(event.error.constData(), qMin<qsizetype>(4096, event.error.size()));
        }
        _receivedBytes += event.bytes.size();
        _events.enqueue(std::move(event));
        _dispatchLocked();
        return true;
    }

    Event _event(Kind kind, quint64 generation)
    {
        Event event;
        event.kind       = kind;
        event.generation = generation;
        return event;
    }

    void _stateChanged(State state, quint64 generation)
    {
        if (!_current(generation) || _failed)
        {
            return;
        }
        auto event          = _event(Kind::State, generation);
        event.state         = state;
        event.atomicWrites  = _device->atomicWrites();
        event.autoReconnect = _device->autoReconnect();
        {
            QMutexLocker lock(&_mutex);
            if (generation == _generation)
            {
                _accepting = state == State::Open;
                if (state == State::Closed || state == State::Failed)
                {
                    _clearOutputLocked();
                }
            }
        }
        if (!_enqueue(std::move(event)))
        {
            _fail(QStringLiteral("Transport callback queue exceeded 4096 events."), generation);
        }
        if (state == State::Open)
        {
            QMutexLocker lock(&_mutex);
            _wakeLocked();
        }
    }

    void _fail(const QString& message, quint64 generation)
    {
        if (!_current(generation) || _failed)
        {
            return;
        }
        qCWarning(transportWorkerLog) << "Transport worker failed:" << message;
        _failed = true;
        _retry->stop();
        if (_device)
        {
            _device->setCallbacks({});
            _device->close();
        }
        QMutexLocker lock(&_mutex);
        if (_stopping || generation != _generation)
        {
            return;
        }
        _clearOutputLocked();
        _accepting  = false;
        // Two reserved failure events preserve the already accepted receive prefix.
        auto error  = _event(Kind::Error, generation);
        error.error = QString(message.constData(), qMin<qsizetype>(4096, message.size()));
        _events.enqueue(std::move(error));
        auto state          = _event(Kind::State, generation);
        state.state         = State::Failed;
        state.atomicWrites  = _device && _device->atomicWrites();
        state.autoReconnect = !_device || _device->autoReconnect();
        _events.enqueue(std::move(state));
        _dispatchLocked();
    }

    void _installCallbacks(quint64 generation)
    {
        Callbacks callbacks;
        callbacks.received = [this, generation](const QByteArray& bytes) {
            if (_failed)
            {
                return;
            }
            auto event  = _event(Kind::Received, generation);
            event.bytes = bytes;
            if (!_enqueue(std::move(event)))
            {
                _fail(QStringLiteral("Transport receive queue exceeded 4 MiB / 4096 events."), generation);
            }
        };
        callbacks.written = [this, generation](qint64 count) {
            if (count < 0 || _failed)
            {
                return;
            }
            {
                QMutexLocker lock(&_mutex);
                if (generation != _generation || _stopping)
                {
                    return;
                }
                const qint64 backendWritten  = qMin(count, _backendPendingBytes);
                _backendPendingBytes        -= backendWritten;
                // A synchronous callback can precede write()'s acceptance result. Reserve its
                // credit against that call, never against newly admitted owner-thread output.
                if (_writing)
                {
                    _inFlightWritten += qMin(count - backendWritten, _inFlightBytes - _inFlightWritten);
                }
                _wakeLocked();
            }
            auto event  = _event(Kind::Written, generation);
            event.count = count;
            if (!_enqueue(std::move(event)))
            {
                _fail(QStringLiteral("Transport callback queue exceeded 4096 events."), generation);
            }
        };
        callbacks.stateChanged = [this, generation](State state) { _stateChanged(state, generation); };
        callbacks.error        = [this, generation](const QString& message) {
            if (_failed)
            {
                return;
            }
            auto event  = _event(Kind::Error, generation);
            event.error = message.left(4096);
            if (!_enqueue(std::move(event)))
            {
                _fail(QStringLiteral("Transport callback queue exceeded 4096 events."), generation);
            }
        };
        callbacks.exited = [this, generation](int code) {
            if (_failed)
            {
                return;
            }
            auto event  = _event(Kind::Exited, generation);
            event.count = code;
            if (!_enqueue(std::move(event)))
            {
                _fail(QStringLiteral("Transport callback queue exceeded 4096 events."), generation);
            }
        };
        _device->setCallbacks(std::move(callbacks));
    }

    void _process()
    {
        QVariantMap configuration;
        bool        command       = false;
        bool        requestedOpen = false;
        bool        geometry      = false;
        int         columns       = 0;
        int         rows          = 0;
        quint64     generation    = 0;
        {
            QMutexLocker lock(&_mutex);
            _wakeScheduled = false;
            if (_stopping)
            {
                return;
            }
            generation       = _generation;
            command          = _commandPending;
            _commandPending  = false;
            requestedOpen    = _requestedOpen;
            configuration    = _configuration;
            geometry         = _geometryPending;
            _geometryPending = false;
            columns          = _columns;
            rows             = _rows;
        }
        if (command)
        {
            qCDebug(transportWorkerTrace) << "Apply connection request" << generation << requestedOpen;
            _retry->stop();
            _workerGeneration = generation;
            _failed           = false;
            if (_device)
            {
                _device->setCallbacks({});
                _device->close();
            }
            if (!_current(generation))
            {
                return;
            }
            if (!requestedOpen)
            {
                return;
            }
            _installCallbacks(generation);
            QString    error;
            const bool accepted = _device->open(configuration, &error);
            if (!_current(generation))
            {
                return;
            }
            if (!accepted)
            {
                _fail(error.isEmpty() ? QStringLiteral("Transport open failed.") : error, generation);
                return;
            }
            _stateChanged(_device->state(), generation);
            // Apply the latest geometry on every open, including a coalesced close/open.
            geometry = true;
        }
        if (!_device || _failed || generation != _workerGeneration || !_current(generation))
        {
            return;
        }
        if (geometry)
        {
            _device->resize(columns, rows);
        }
        if (_device->state() != State::Open)
        {
            return;
        }
        qint64 processedBytes = 0;
        for (int index = 0; index < 64 && processedBytes < 256 * 1024; ++index)
        {
            QByteArray bytes;
            quint64    outputEpoch = 0;
            {
                QMutexLocker lock(&_mutex);
                if (_stopping || generation != _generation || _output.isEmpty())
                {
                    return;
                }
                bytes             = _output.dequeue();
                _queuedBytes     -= bytes.size();
                _inFlightBytes    = bytes.size();
                _inFlightWritten  = 0;
                _writing          = true;
                outputEpoch       = _outputEpoch;
            }
            const bool   emptyPacket = bytes.isEmpty();
            QString      error;
            const qint64 accepted = _device->write(bytes, &error);
            if (!_current(generation) || _failed)
            {
                return;
            }
            if (accepted < 0 || accepted > bytes.size()
                || (_device->atomicWrites() && accepted > 0 && accepted != bytes.size()))
            {
                _fail(error.isEmpty() ? QStringLiteral("Transport write failed.") : error, generation);
                return;
            }
            if (_device->state() != State::Open)
            {
                return;
            }
            {
                QMutexLocker lock(&_mutex);
                if (generation != _generation)
                {
                    return;
                }
                _backendPendingBytes += qMax<qint64>(0, accepted - _inFlightWritten);
                _inFlightBytes        = 0;
                _inFlightWritten      = 0;
                _writing              = false;
                if (outputEpoch == _outputEpoch && accepted < bytes.size())
                {
                    bytes.remove(0, accepted);
                    _queuedBytes += bytes.size();
                    _output.prepend(std::move(bytes));
                }
            }
            processedBytes += accepted;
            if (accepted == 0 && !emptyPacket)
            {
                _retry->start(10);
                return;
            }
        }
        QMutexLocker lock(&_mutex);
        if (!_output.isEmpty())
        {
            _wakeLocked();
        }
    }
};

ThreadedSerialDevice::ThreadedSerialDevice(Factory factory, QObject* parent) :
    QObject(parent), _private(std::make_unique<Private>(this, std::move(factory)))
{
    _atomicWrites  = _private->_initialAtomicWrites;
    _autoReconnect = _private->_initialAutoReconnect;
}

ThreadedSerialDevice::~ThreadedSerialDevice()
{
    _callbacks = {};
    _private.reset();
}

QObject* ThreadedSerialDevice::object()
{
    return this;
}

SerialDevice::State ThreadedSerialDevice::state() const
{
    return _state;
}

void ThreadedSerialDevice::setCallbacks(Callbacks callbacks)
{
    _callbacks = std::move(callbacks);
}

bool ThreadedSerialDevice::open(const QVariantMap& configuration, QString* error)
{
    if (error)
    {
        error->clear();
    }
    {
        QMutexLocker lock(&_private->_mutex);
        ++_private->_generation;
        _private->_accepting      = false;
        _private->_requestedOpen  = true;
        _private->_configuration  = configuration;
        _private->_commandPending = true;
        _private->_clearOutputLocked();
        _private->_events.clear();
        _private->_receivedBytes = 0;
        _private->_wakeLocked();
    }
    _state              = State::Opening;
    const auto callback = _callbacks.stateChanged;
    if (callback)
    {
        callback(State::Opening);
    }
    return true;
}

void ThreadedSerialDevice::close()
{
    {
        QMutexLocker lock(&_private->_mutex);
        ++_private->_generation;
        _private->_accepting     = false;
        _private->_requestedOpen = false;
        _private->_configuration.clear();
        _private->_commandPending = true;
        _private->_clearOutputLocked();
        _private->_events.clear();
        _private->_receivedBytes = 0;
        _private->_wakeLocked();
    }
    if (_state != State::Closed)
    {
        _state              = State::Closed;
        const auto callback = _callbacks.stateChanged;
        if (callback)
        {
            callback(State::Closed);
        }
    }
}

qint64 ThreadedSerialDevice::write(const QByteArray& data, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (_state != State::Open)
    {
        if (error)
        {
            *error = QStringLiteral("Transport is not open.");
        }
        return -1;
    }
    QMutexLocker lock(&_private->_mutex);
    if (!_private->_accepting)
    {
        if (error)
        {
            *error = QStringLiteral("Transport is not open.");
        }
        return -1;
    }
    const qint64 capacity = qMax<qint64>(0, MaximumPendingBytes - _private->_pendingBytesLocked());
    if (_private->_output.size() + (_private->_writing ? 1 : 0) >= MaximumPendingPackets
        || (_atomicWrites && data.size() > capacity))
    {
        if (_atomicWrites && data.isEmpty())
        {
            if (error)
            {
                *error = QStringLiteral("Transport output queue is full; empty packet was not accepted.");
            }
            return -1;
        }
        return 0;
    }
    const qint64 accepted = qMin<qint64>(data.size(), capacity);
    if (!_atomicWrites && accepted == 0)
    {
        return 0;
    }
    _private->_output.enqueue(QByteArray(data.constData(), accepted));
    _private->_queuedBytes += accepted;
    _private->_wakeLocked();
    return accepted;
}

qint64 ThreadedSerialDevice::pendingBytes() const
{
    QMutexLocker lock(&_private->_mutex);
    return _private->_pendingBytesLocked();
}

void ThreadedSerialDevice::discardPendingOutput()
{
    QMutexLocker lock(&_private->_mutex);
    ++_private->_outputEpoch;
    _private->_output.clear();
    _private->_queuedBytes = 0;
    // Keep native and executing-write credits until their real written callbacks arrive.
    // The output epoch prevents an executing write from restoring a cancelled remainder.
    _private->_wakeLocked();
}

bool ThreadedSerialDevice::atomicWrites() const
{
    return _atomicWrites;
}

bool ThreadedSerialDevice::autoReconnect() const
{
    return _autoReconnect;
}

void ThreadedSerialDevice::resize(int columns, int rows)
{
    QMutexLocker lock(&_private->_mutex);
    _private->_columns         = columns;
    _private->_rows            = rows;
    _private->_geometryPending = true;
    _private->_wakeLocked();
}

void ThreadedSerialDevice::_dispatch()
{
    const QPointer<ThreadedSerialDevice> self(this);
    // Limit one owner-thread turn; the queued notification itself never carries byte payloads.
    qint64 bytes = 0;
    for (int index = 0; index < 128 && bytes < 256 * 1024; ++index)
    {
        Private::Event event;
        {
            QMutexLocker lock(&_private->_mutex);
            if (_private->_events.isEmpty())
            {
                _private->_dispatchScheduled = false;
                return;
            }
            event                     = _private->_events.dequeue();
            _private->_receivedBytes -= event.bytes.size();
            if (event.generation != _private->_generation)
            {
                continue;
            }
        }
        bytes                += event.bytes.size();
        const auto callbacks  = _callbacks;
        switch (event.kind)
        {
            case Private::Kind::Received:
                if (callbacks.received)
                {
                    callbacks.received(event.bytes);
                }
                break;
            case Private::Kind::Written:
                if (callbacks.written)
                {
                    callbacks.written(event.count);
                }
                break;
            case Private::Kind::State:
                _atomicWrites  = event.atomicWrites;
                _autoReconnect = event.autoReconnect;
                if (_state != event.state)
                {
                    _state = event.state;
                    if (callbacks.stateChanged)
                    {
                        callbacks.stateChanged(event.state);
                    }
                }
                break;
            case Private::Kind::Error:
                if (callbacks.error)
                {
                    callbacks.error(event.error);
                }
                break;
            case Private::Kind::Exited:
                if (callbacks.exited)
                {
                    callbacks.exited(static_cast<int>(event.count));
                }
                break;
        }
        if (!self)
        {
            return;
        }
    }
    QMutexLocker lock(&_private->_mutex);
    _private->_dispatchScheduled = false;
    if (!_private->_events.isEmpty())
    {
        _private->_dispatchLocked();
    }
}
