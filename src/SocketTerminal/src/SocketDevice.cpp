#include "SocketDevice.h"

#include <QHostAddress>
#include <QLoggingCategory>
#include <QMetaType>
#include <QScopedValueRollback>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

namespace
{
Q_LOGGING_CATEGORY(socketLog, "termaster.transport.socket")
Q_LOGGING_CATEGORY(socketLogTrace, "termaster.transport.socket.trace", QtInfoMsg)

constexpr qint64 maximumPending = 1024 * 1024;
bool             fail(QString* error, const QString& message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}
bool number(const QVariant& value, qint64 minimum, qint64 maximum)
{
    const auto type = value.metaType().id();
    return (type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
            || type == QMetaType::ULongLong || type == QMetaType::Short || type == QMetaType::UShort)
        && value.toLongLong() >= minimum && value.toULongLong() <= qulonglong(maximum);
}
} // namespace

SocketDevice::~SocketDevice()
{
    _callbacks = {};
    close();
}
QObject* SocketDevice::object()
{
    return this;
}
SerialDevice::State SocketDevice::state() const
{
    return _state;
}
void SocketDevice::setCallbacks(Callbacks callbacks)
{
    _callbacks = std::move(callbacks);
}

bool SocketDevice::validate(const QVariantMap& map, QString* error)
{
    if (error)
    {
        error->clear();
    }
    const auto protocol = map.value("protocol", "TCP");
    if (protocol.metaType().id() != QMetaType::QString || (protocol != "TCP" && protocol != "UDP"))
    {
        return fail(error, "Protocol must be TCP or UDP.");
    }
    const auto host = map.value("host");
    if (host.metaType().id() != QMetaType::QString || host.toString().isEmpty() || host.toString().size() > 253
        || host.toString() != host.toString().trimmed())
    {
        return fail(error, "Host must be a DNS name or numeric IP address.");
    }
    QHostAddress address;
    if (!address.setAddress(host.toString()))
    {
        auto name = host.toString();
        if (name.endsWith('.'))
        {
            name.chop(1);
        }
        const auto labels = name.split('.');
        for (const auto& label : labels)
        {
            if (label.isEmpty() || label.size() > 63 || label.startsWith('-') || label.endsWith('-'))
            {
                return fail(error, "Host must be a DNS name or numeric IP address.");
            }
            for (const auto c : label)
            {
                if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '-')
                {
                    return fail(error, "Host must be a DNS name or numeric IP address.");
                }
            }
        }
    }
    if (!number(map.value("port"), 1, 65535))
    {
        return fail(error, "Port must be between 1 and 65535.");
    }
    if (!number(map.value("bindPort", 0), 0, 65535))
    {
        return fail(error, "Bind port must be between 0 and 65535.");
    }
    if (!number(map.value("connectTimeoutMs", 10000), 100, 300000))
    {
        return fail(error, "Connect timeout must be between 100 and 300000 milliseconds.");
    }
    const auto bind = map.value("bindAddress", "");
    if (bind.metaType().id() != QMetaType::QString || bind.toString().size() > 128
        || (!bind.toString().isEmpty() && !address.setAddress(bind.toString())))
    {
        return fail(error, "Bind address must be empty or a numeric IP address.");
    }
    for (const auto& key : {QString("keepAlive"), QString("noDelay")})
    {
        if (map.contains(key) && map.value(key).metaType().id() != QMetaType::Bool)
        {
            return fail(error, key + " must be boolean.");
        }
    }
    return true;
}

bool SocketDevice::open(const QVariantMap& configuration, QString* error)
{
    qCDebug(socketLogTrace) << "Open requested";
    const auto previousGeneration = _generation;
    close();
    if (_generation != previousGeneration + 1)
    {
        return fail(error, "Socket connection was replaced during closure.");
    }
    const auto generation = ++_generation;
    if (!validate(configuration, error))
    {
        _setState(State::Failed);
        return false;
    }
    _udp         = configuration.value("protocol", "TCP") == "UDP";
    _socket      = _udp ? static_cast<QAbstractSocket*>(new QUdpSocket(this)) : new QTcpSocket(this);
    auto* socket = _socket.data();
    socket->setReadBufferSize(256 * 1024);
    connect(socket, &QAbstractSocket::connected, this, [this, generation, configuration] {
        if (generation != _generation || _state != State::Opening)
        {
            return;
        }
        if (!_udp)
        {
            _socket->setSocketOption(QAbstractSocket::KeepAliveOption, configuration.value("keepAlive", true));
            _socket->setSocketOption(QAbstractSocket::LowDelayOption, configuration.value("noDelay", true));
        }
        _setState(State::Open);
    });
    connect(socket, &QIODevice::readyRead, this, [this, generation] { _read(generation); });
    connect(socket, &QAbstractSocket::disconnected, this, [this, generation] { _read(generation); });
    connect(socket, &QAbstractSocket::errorOccurred, this, [this, generation](QAbstractSocket::SocketError error) {
        if (generation != _generation)
        {
            return;
        }
        if (error == QAbstractSocket::RemoteHostClosedError && _state == State::Open)
        {
            return;
        }
        _fail(_socket->errorString(), generation);
    });
    connect(socket, &QIODevice::bytesWritten, this, [this, generation](qint64 count) {
        if (generation != _generation || _state != State::Open)
        {
            return;
        }
        const auto callback = _callbacks.written;
        if (callback)
        {
            callback(count);
        }
        _pump(generation);
    });
    const auto bindAddress = configuration.value("bindAddress").toString();
    const auto bindPort    = configuration.value("bindPort", 0).toUInt();
    if ((!bindAddress.isEmpty() || bindPort != 0)
        && !socket->bind(bindAddress.isEmpty() ? QHostAddress(QHostAddress::Any) : QHostAddress(bindAddress),
                         quint16(bindPort)))
    {
        const auto message = socket->errorString();
        _fail(message, generation);
        return fail(error, message);
    }
    _setState(State::Opening);
    if (generation != _generation || _state != State::Opening)
    {
        return false;
    }
    QTimer::singleShot(configuration.value("connectTimeoutMs", 10000).toInt(), socket, [this, generation] {
        if (generation == _generation && _state == State::Opening)
        {
            _fail("Socket connection timed out.", generation);
        }
    });
    socket->connectToHost(configuration.value("host").toString(), quint16(configuration.value("port").toUInt()));
    return generation == _generation && (_state == State::Opening || _state == State::Open);
}

void SocketDevice::close()
{
    const auto generation = ++_generation;
    _release();
    if (_state == State::Closed)
    {
        return;
    }
    _setState(State::Closing);
    if (generation == _generation)
    {
        _setState(State::Closed);
    }
}
qint64 SocketDevice::write(const QByteArray& data, QString* error)
{
    qCDebug(socketLogTrace) << "Write requested bytes" << data.size();
    if (error)
    {
        error->clear();
    }
    if (_state != State::Open)
    {
        fail(error, "Socket is not open.");
        return -1;
    }
    if (_udp && data.size() > 65507)
    {
        fail(error, "UDP datagram exceeds 65507 bytes.");
        return -1;
    }
    const auto capacity = qMax<qint64>(0, maximumPending - pendingBytes());
    if (_pending.size() >= 1024)
    {
        qCDebug(socketLogTrace) << "Output chunk limit reached";
        if (_udp && data.isEmpty())
        {
            // Zero is successful acceptance for an empty datagram, so report rejection explicitly.
            fail(error, "UDP output queue is full; empty datagram was not accepted.");
            return -1;
        }
        return 0;
    }
    if (_udp && data.size() > capacity)
    {
        qCDebug(socketLogTrace) << "Output byte limit reached";
        return 0;
    }
    const auto accepted = qMin<qint64>(data.size(), capacity);
    if (!_udp && accepted == 0)
    {
        return 0;
    }
    if (!_udp && data.isEmpty())
    {
        return 0;
    }
    _pending.append(data.first(accepted));
    _queuedBytes          += accepted;
    const auto generation  = _generation;
    _pump(generation);
    if (generation != _generation || _state != State::Open)
    {
        fail(error, "Socket closed while accepting output.");
        return -1;
    }
    return accepted;
}
qint64 SocketDevice::pendingBytes() const
{
    return _queuedBytes + (_socket ? qMax(qint64(0), _socket->bytesToWrite()) : 0);
}
void SocketDevice::_setState(State state)
{
    qCDebug(socketLogTrace) << "State transition requested" << int(_state) << int(state);
    if (_state == state)
    {
        return;
    }
    if (state == State::Failed)
    {
        qCWarning(socketLog) << "Transport entered failed state";
    }
    else if (state == State::Open || state == State::Closed)
    {
        qCInfo(socketLog) << "Transport state" << int(state);
    }
    _state              = state;
    const auto callback = _callbacks.stateChanged;
    if (callback)
    {
        callback(state);
    }
}
void SocketDevice::_release()
{
    if (_socket)
    {
        auto* socket = _socket.data();
        _socket      = nullptr;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    _pending.clear();
    _queuedBytes    = 0;
    _readScheduled  = false;
    _writeScheduled = false;
}
void SocketDevice::_fail(const QString& error, quint64 generation)
{
    if (generation != _generation || (_state == State::Failed && !_socket))
    {
        return;
    }
    _release();
    _setState(State::Failed);
    if (generation != _generation)
    {
        return;
    }
    const auto callback = _callbacks.error;
    if (callback)
    {
        callback(error);
    }
}
void SocketDevice::_read(quint64 generation)
{
    if (generation != _generation || _state != State::Open || !_socket || _readScheduled)
    {
        return;
    }
    qint64 total   = 0;
    int    packets = 0;
    while (generation == _generation && _state == State::Open && _socket && total < 256 * 1024 && packets++ < 64)
    {
        QByteArray bytes;
        if (_udp)
        {
            auto* socket = static_cast<QUdpSocket*>(_socket.data());
            if (!socket->hasPendingDatagrams())
            {
                break;
            }
            const auto size = socket->pendingDatagramSize();
            if (size < 0 || size > 65535)
            {
                _fail("Invalid UDP datagram size.", generation);
                return;
            }
            bytes.resize(size);
            if (socket->readDatagram(bytes.data(), bytes.size()) != size)
            {
                _fail(socket->errorString(), generation);
                return;
            }
        }
        else
        {
            if (_socket->bytesAvailable() == 0)
            {
                break;
            }
            bytes = _socket->read(64 * 1024);
            if (bytes.isEmpty())
            {
                break;
            }
        }
        total               += bytes.size();
        const auto callback  = _callbacks.received;
        if (callback)
        {
            callback(bytes);
        }
    }
    if (generation != _generation || _state != State::Open || !_socket)
    {
        return;
    }
    if ((_udp && static_cast<QUdpSocket*>(_socket.data())->hasPendingDatagrams())
        || (!_udp && _socket->bytesAvailable() > 0))
    {
        _readScheduled = true;
        QTimer::singleShot(0, _socket, [this, generation] {
            if (generation != _generation)
            {
                return;
            }
            _readScheduled = false;
            _read(generation);
        });
    }
    else if (_socket->state() == QAbstractSocket::UnconnectedState)
    {
        _release();
        _setState(State::Closed);
    }
}
void SocketDevice::_pump(quint64 generation)
{
    if (generation != _generation || _state != State::Open || !_socket || _writeScheduled)
    {
        return;
    }
    if (_pumping)
    {
        // A callback may close/reopen this endpoint and enqueue a new generation
        // while the old socket is still returning from its synchronous write.
        _writeScheduled = true;
        QTimer::singleShot(0, _socket, [this, generation] {
            if (generation != _generation)
            {
                return;
            }
            _writeScheduled = false;
            _pump(generation);
        });
        return;
    }
    QScopedValueRollback guard(_pumping, true);
    int                  count = 0;
    while (!_pending.isEmpty() && count++ < 64)
    {
        const auto       size    = _pending.first().size();
        const QByteArray bytes   = _pending.first();
        const auto       written = _socket->write(bytes.constData(), bytes.size());
        if (generation != _generation || !_socket || _state != State::Open)
        {
            return;
        }
        if (written < 0 || (_udp && written != size))
        {
            _fail(_socket->errorString(), generation);
            return;
        }
        if (written == 0 && size != 0)
        {
            break;
        }
        _queuedBytes -= written;
        if (written == size)
        {
            _pending.removeFirst();
        }
        else
        {
            _pending.first().remove(0, written);
        }
    }
    if (!_pending.isEmpty() && !_writeScheduled)
    {
        _writeScheduled = true;
        QTimer::singleShot(10, _socket, [this, generation] {
            if (generation != _generation)
            {
                return;
            }
            _writeScheduled = false;
            _pump(generation);
        });
    }
}
