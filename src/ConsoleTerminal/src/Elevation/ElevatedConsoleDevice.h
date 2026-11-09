#pragma once

#include <QLocalServer>
#include <QObject>
#include <QTimer>
#include <Termaster/Core/Terminal/SerialDevice.h>
#include <memory>

class QLocalSocket;

/** GUI-thread authenticated IPC endpoint. The helper owns the elevated process tree. */
class ElevatedConsoleDevice final :
    public QObject,
    public SerialDevice,
    public std::enable_shared_from_this<ElevatedConsoleDevice>
{
public:
    /** Test launchers start the same helper without UAC and return its actual process ID. */
    using Launcher = std::function<quint32(const QString&, const QStringList&, QString*)>;
    explicit ElevatedConsoleDevice(Launcher launcher = {});
    ~ElevatedConsoleDevice() override;
    QObject* object() override;
    State    state() const override;
    void     setCallbacks(Callbacks callbacks) override;
    bool     open(const QVariantMap& configuration, QString* error = nullptr) override;
    void     close() override;
    qint64   write(const QByteArray& data, QString* error = nullptr) override;
    qint64   pendingBytes() const override;
    void     resize(int columns, int rows) override;
    bool     autoReconnect() const override;

private:
    Launcher      _launcher;
    Callbacks     _callbacks;
    QLocalServer  _server;
    QLocalSocket* _socket = nullptr;
    QTimer        _startup;
    QByteArray    _buffer;
    QByteArray    _nonce;
    QVariantMap   _configuration;
    State         _state         = State::Closed;
    void*         _process       = nullptr;
    quint32       _pid           = 0;
    qint64        _pending       = 0;
    int           _columns       = 80;
    int           _rows          = 24;
    int           _attempts      = 0;
    bool          _authenticated = false;
    bool          _reading       = false;
    quint64       _generation    = 0;
    void          _setState(State state);
    void          _accept();
    void          _read();
    void          _fail(const QString& message);
    void          _cleanup();
};
