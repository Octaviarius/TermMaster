#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <Termaster/Core/Terminal/SerialDevice.h>

class QAbstractSocket;

/** GUI-thread TCP/UDP byte transport. Each connection owns a fresh socket. */
class SocketDevice final : public QObject, public SerialDevice
{
public:
    SocketDevice() = default;
    ~SocketDevice() override;
    QObject* object() override;
    State    state() const override;
    void     setCallbacks(Callbacks callbacks) override;
    bool     open(const QVariantMap& configuration, QString* error = nullptr) override;
    void     close() override;
    qint64   write(const QByteArray& data, QString* error = nullptr) override;
    qint64   pendingBytes() const override;
    bool     atomicWrites() const override
    {
        return _udp;
    }
    static bool validate(const QVariantMap& configuration, QString* error = nullptr);

private:
    friend class SocketDeviceTests;
    Callbacks                 _callbacks;
    State                     _state = State::Closed;
    QPointer<QAbstractSocket> _socket;
    QList<QByteArray>         _pending;
    qint64                    _queuedBytes    = 0;
    bool                      _udp            = false;
    bool                      _pumping        = false;
    quint64                   _generation     = 0;
    bool                      _readScheduled  = false;
    bool                      _writeScheduled = false;
    void                      _setState(State state);
    void                      _fail(const QString& error, quint64 generation);
    void                      _read(quint64 generation);
    void                      _pump(quint64 generation);
    void                      _release();
};
