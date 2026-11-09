#pragma once

#include <QObject>
#include <QTimer>
#include <Termaster/Core/Terminal/SerialDevice.h>
#include <memory>

/** GUI-thread PTY endpoint. Native workers only access the bounded shared queues;
 * close drains all workers before the endpoint or its plugin can be destroyed.
 */
class ConsoleDevice final : public QObject, public SerialDevice
{
public:
    explicit ConsoleDevice();
    ~ConsoleDevice() override;
    QObject*    object() override;
    State       state() const override;
    void        setCallbacks(Callbacks callbacks) override;
    bool        open(const QVariantMap& configuration, QString* error = nullptr) override;
    void        close() override;
    qint64      write(const QByteArray& data, QString* error = nullptr) override;
    qint64      pendingBytes() const override;
    void        resize(int columns, int rows) override;
    bool        autoReconnect() const override;
    static bool validate(const QVariantMap& configuration, QString* error = nullptr);

private:
    struct Native;
    std::unique_ptr<Native> _native;
    Callbacks               _callbacks;
    State                   _state = State::Closed;
    QTimer                  _timer;
    int                     _columns = 80;
    int                     _rows    = 24;
    bool                    _pumping = false;
    void                    _setState(State state);
    void                    _pump();
};
