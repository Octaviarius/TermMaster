#include "SerialByteDevice.h"

#include <QLoggingCategory>
#include <QMetaType>
#include <QScopedValueRollback>
#include <QSerialPort>
#include <limits>

namespace
{
Q_LOGGING_CATEGORY(serialLog, "termaster.transport.serial")
Q_LOGGING_CATEGORY(serialLogTrace, "termaster.transport.serial.trace", QtInfoMsg)

constexpr qint64 maximumPending = 1024 * 1024;
bool             fail(QString* error, const QString& text)
{
    if (error)
    {
        *error = text;
    }
    return false;
}
bool integer(const QVariant& value)
{
    const auto type = value.metaType().id();
    return type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
        || type == QMetaType::ULongLong;
}
bool configureSerial(QIODevice& device, const QVariantMap& map, QString* error)
{
    auto& port = static_cast<QSerialPort&>(device);
    port.setPortName(map.value("portName").toString());
    const auto                               parity = map.value("parity", "None").toString();
    const auto                               flow   = map.value("flowControl", "None").toString();
    const auto                               stop   = map.value("stopBits", "1").toString();
    const QMap<QString, QSerialPort::Parity> parities {
        { "None",    QSerialPort::NoParity},
        { "Even",  QSerialPort::EvenParity},
        {  "Odd",   QSerialPort::OddParity},
        {"Space", QSerialPort::SpaceParity},
        { "Mark",  QSerialPort::MarkParity}
    };
    const bool configured = port.setBaudRate(map.value("baudRate", 115200).toInt())
                         && port.setDataBits(static_cast<QSerialPort::DataBits>(map.value("dataBits", 8).toInt()))
                         && port.setStopBits(stop == "1"   ? QSerialPort::OneStop :
                                             stop == "1.5" ? QSerialPort::OneAndHalfStop :
                                                             QSerialPort::TwoStop)
                         && port.setParity(parities.value(parity))
                         && port.setFlowControl(flow == "None"    ? QSerialPort::NoFlowControl :
                                                flow == "RTS/CTS" ? QSerialPort::HardwareControl :
                                                                    QSerialPort::SoftwareControl);
    if (!configured)
    {
        return fail(error, port.errorString());
    }
    return true;
}
} // namespace
SerialByteDevice::SerialByteDevice(std::unique_ptr<QIODevice> transport, Configure configure) :
    _transport(std::move(transport)), _configure(std::move(configure))
{
    if (!_transport)
    {
        auto port = std::make_unique<QSerialPort>();
        port->setReadBufferSize(256 * 1024);
        port->setWriteBufferSize(64 * 1024);
        connect(port.get(), &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
            if (error != QSerialPort::NoError && (_state == State::Open || _state == State::Opening))
            {
                transportError(_transport->errorString());
            }
        });
        _transport = std::move(port);
        _configure = configureSerial;
    }
    _retry.setInterval(10);
    connect(&_retry, &QTimer::timeout, this, [this] { _pump(); });
    connect(_transport.get(), &QIODevice::readyRead, this, [this] {
        while (_state == State::Open && _transport->bytesAvailable() > 0)
        {
            const auto bytes = _transport->read(64 * 1024);
            if (bytes.isEmpty())
            {
                break;
            }
            const auto callback = _callbacks.received;
            if (callback)
            {
                callback(bytes);
            }
        }
    });
    connect(_transport.get(), &QIODevice::bytesWritten, this, [this](qint64 count) {
        const auto callback = _callbacks.written;
        if (callback)
        {
            callback(count);
        }
        _pump();
    });
}
SerialByteDevice::~SerialByteDevice()
{
    _callbacks = {};
    close();
}
QObject* SerialByteDevice::object()
{
    return this;
}
SerialDevice::State SerialByteDevice::state() const
{
    return _state;
}
void SerialByteDevice::setCallbacks(Callbacks callbacks)
{
    _callbacks = std::move(callbacks);
}
bool SerialByteDevice::validate(const QVariantMap& map, QString* error)
{
    if (map.value("portName").metaType().id() != QMetaType::QString
        || map.value("portName").toString().trimmed().isEmpty())
    {
        return fail(error, "Port name is required.");
    }
    for (const auto& key : {QString("baudRate"), QString("dataBits")})
    {
        const auto value = map.value(key, key == "baudRate" ? 115200 : 8);
        if (!integer(value) || value.toLongLong() <= 0
            || value.toULongLong() > static_cast<qulonglong>(std::numeric_limits<int>::max()))
        {
            return fail(error, key + " must be a positive integer.");
        }
    }
    const auto bits = map.value("dataBits", 8).toInt();
    if (bits < 5 || bits > 8)
    {
        return fail(error, "Data bits must be between 5 and 8.");
    }
    const QMap<QString, QStringList> choices {
        {   "stopBits",                        {"1", "1.5", "2"}},
        {     "parity", {"None", "Even", "Odd", "Space", "Mark"}},
        {"flowControl",          {"None", "RTS/CTS", "XON/XOFF"}}
    };
    for (auto it = choices.begin(); it != choices.end(); ++it)
    {
        const auto value = map.value(it.key(), it.value().first());
        if (value.metaType().id() != QMetaType::QString || !it.value().contains(value.toString()))
        {
            return fail(error, "Unsupported " + it.key() + ".");
        }
    }
    for (const auto& key : {QString("dtr"), QString("rts")})
    {
        if (map.contains(key) && map.value(key).metaType().id() != QMetaType::Bool)
        {
            return fail(error, key + " must be boolean.");
        }
    }
    return true;
}
bool SerialByteDevice::open(const QVariantMap& configuration, QString* error)
{
    qCDebug(serialLogTrace) << "Open requested";
    if (error)
    {
        error->clear();
    }
    _lastError.clear();
    close();
    if (!validate(configuration, error))
    {
        _setState(State::Failed);
        return false;
    }
    _setState(State::Opening);
    const bool configured = !_configure || _configure(*_transport, configuration, error);
    bool       opened     = false;
    if (configured && _state == State::Opening)
    {
        opened = _transport->open(QIODevice::ReadWrite);
    }
    if (!configured || !opened || _state != State::Opening)
    {
        if (error && error->isEmpty())
        {
            *error = !_lastError.isEmpty() ? _lastError : _transport->errorString();
        }
        _transport->close();
        _setState(State::Failed);
        return false;
    }
    if (auto* port = qobject_cast<QSerialPort*>(_transport.get()))
    {
        bool ok = true;
        if (configuration.contains("dtr"))
        {
            ok = port->setDataTerminalReady(configuration.value("dtr").toBool());
        }
        if (ok && configuration.contains("rts") && configuration.value("flowControl", "None") != "RTS/CTS")
        {
            ok = port->setRequestToSend(configuration.value("rts").toBool());
        }
        if (!ok)
        {
            const auto text = port->errorString();
            port->close();
            _setState(State::Failed);
            return fail(error, text);
        }
    }
    if (_state != State::Opening)
    {
        _transport->close();
        if (error)
        {
            *error = _lastError;
        }
        return false;
    }
    _setState(State::Open);
    return true;
}
void SerialByteDevice::close()
{
    if (_state == State::Closed)
    {
        return;
    }
    _setState(State::Closing);
    _retry.stop();
    _pending.clear();
    _transport->close();
    _setState(State::Closed);
}
qint64 SerialByteDevice::write(const QByteArray& data, QString* error)
{
    qCDebug(serialLogTrace) << "Write requested bytes" << data.size();
    if (error)
    {
        error->clear();
    }
    if (_state != State::Open)
    {
        fail(error, "Serial port is not open.");
        return -1;
    }
    const auto accepted = qMin<qint64>(data.size(), qMax<qint64>(0, maximumPending - pendingBytes()));
    if (accepted == 0)
    {
        qCDebug(serialLogTrace) << "Output backpressure";
        return 0;
    }
    _pending.append(data.constData(), accepted);
    _pump();
    if (_state != State::Open)
    {
        fail(error, _lastError);
        return -1;
    }
    return accepted;
}
qint64 SerialByteDevice::pendingBytes() const
{
    return _pending.size() + qMax(qint64(0), _transport->bytesToWrite());
}
void SerialByteDevice::transportError(const QString& error)
{
    _lastError = error;
    _retry.stop();
    _pending.clear();
    _setState(State::Failed);
    _transport->close();
    const auto callback = _callbacks.error;
    if (callback)
    {
        callback(error);
    }
}
void SerialByteDevice::_setState(State state)
{
    qCDebug(serialLogTrace) << "State transition requested" << int(_state) << int(state);
    if (_state == state)
    {
        return;
    }
    if (state == State::Failed)
    {
        qCWarning(serialLog) << "Transport entered failed state";
    }
    else if (state == State::Open || state == State::Closed)
    {
        qCInfo(serialLog) << "Transport state" << int(state);
    }
    _state              = state;
    const auto callback = _callbacks.stateChanged;
    if (callback)
    {
        callback(state);
    }
}
void SerialByteDevice::_pump()
{
    if (_pumping || _state != State::Open)
    {
        return;
    }
    QScopedValueRollback guard(_pumping, true);
    while (!_pending.isEmpty() && _state == State::Open)
    {
        const auto count = _transport->write(_pending);
        if (_state != State::Open)
        {
            return;
        }
        if (count < 0)
        {
            transportError(_transport->errorString());
            return;
        }
        if (count == 0)
        {
            _retry.start();
            return;
        }
        _pending.remove(0, count);
    }
    _retry.stop();
}
