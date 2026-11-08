#pragma once

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <functional>

class QObject;

/** Header-only byte-device ABI. Device and callbacks share the GUI thread.
 * The unique device owner clears callbacks and closes it before unloading its module.
 */
class SerialDevice
{
public:
    enum class State
    {
        Closed,
        Opening,
        Open,
        Closing,
        Failed
    };
    struct Callbacks
    {
        std::function<void(const QByteArray&)> received;
        std::function<void(qint64)>            written;
        std::function<void(State)>             stateChanged;
        std::function<void(const QString&)>    error;
    };

    virtual ~SerialDevice()                                                       = default;
    virtual QObject* object()                                                     = 0;
    virtual State    state() const                                                = 0;
    virtual void     setCallbacks(Callbacks callbacks)                            = 0;
    /** Configuration is a flat transport map. Open never implies data transmission. */
    virtual bool open(const QVariantMap& configuration, QString* error = nullptr) = 0;
    virtual void close()                                                          = 0;
    /** Returns bytes accepted into a bounded queue, not proof of delivery; -1 on failure. */
    virtual qint64 write(const QByteArray& data, QString* error = nullptr)        = 0;
    virtual qint64 pendingBytes() const                                           = 0;
};
