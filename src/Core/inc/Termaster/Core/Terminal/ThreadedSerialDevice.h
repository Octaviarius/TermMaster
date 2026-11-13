#pragma once

#include <QObject>
#include <Termaster/Core/Terminal/SerialDevice.h>
#include <memory>

/** Owner-thread facade for a device constructed, used and destroyed on its dedicated worker.
 * All public operations and callbacks belong to the constructing thread. The factory and its
 * provider must remain loaded until destruction returns. Open accepts an asynchronous request;
 * validation and transport failures arrive through the callbacks. Close cancels queued work.
 * Bootstrap rejects a missing device/object or an object outside the worker thread.
 */
class ThreadedSerialDevice final : public QObject, public SerialDevice
{
public:
    using Factory                                    = std::function<std::unique_ptr<SerialDevice>()>;
    static constexpr qint64    MaximumPendingBytes   = 1024 * 1024;
    static constexpr qsizetype MaximumPendingPackets = 1024;
    static constexpr qint64    MaximumReceivedBytes  = 4 * 1024 * 1024;
    static constexpr qsizetype MaximumCallbackEvents = 4096;

    explicit ThreadedSerialDevice(Factory factory, QObject* parent = nullptr);
    ~ThreadedSerialDevice() override;
    Q_DISABLE_COPY_MOVE(ThreadedSerialDevice)

    QObject* object() override;
    State    state() const override;
    void     setCallbacks(Callbacks callbacks) override;
    bool     open(const QVariantMap& configuration, QString* error = nullptr) override;
    void     close() override;
    qint64   write(const QByteArray& data, QString* error = nullptr) override;
    qint64   pendingBytes() const override;
    void     discardPendingOutput() override;
    bool     atomicWrites() const override;
    bool     autoReconnect() const override;
    void     resize(int columns, int rows) override;

private:
    struct Private;
    std::unique_ptr<Private> _private;
    Callbacks                _callbacks;
    State                    _state         = State::Closed;
    bool                     _atomicWrites  = false;
    bool                     _autoReconnect = true;

    void _dispatch();
};
