#pragma once
#include <QIODevice>
#include <QObject>
#include <QTimer>
#include <Termaster/Core/Terminal/SerialDevice.h>
#include <memory>

/** GUI-thread device; the injectable transport is used only by synthetic tests. */
class SerialByteDevice final : public QObject, public SerialDevice
{
public:
    using Configure = std::function<bool(QIODevice&, const QVariantMap&, QString*)>;
    explicit SerialByteDevice(std::unique_ptr<QIODevice> transport = {}, Configure configure = {});
    ~SerialByteDevice() override;
    QObject*    object() override;
    State       state() const override;
    void        setCallbacks(Callbacks callbacks) override;
    bool        open(const QVariantMap& configuration, QString* error = nullptr) override;
    void        close() override;
    qint64      write(const QByteArray& data, QString* error = nullptr) override;
    qint64      pendingBytes() const override;
    void        transportError(const QString& error);
    static bool validate(const QVariantMap& configuration, QString* error);

private:
    std::unique_ptr<QIODevice> _transport;
    Configure                  _configure;
    Callbacks                  _callbacks;
    State                      _state = State::Closed;
    QByteArray                 _pending;
    QString                    _lastError;
    QTimer                     _retry;
    bool                       _pumping = false;
    void                       _setState(State state);
    void                       _pump();
};
