#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QVariantMap>
#include <memory>

class TerminalDisplay;

/** GUI-owned rendered-line collector. The display is borrowed on the GUI thread;
 * only owned UTF-8 packets and built-in endpoint configuration enter its file worker.
 * Stop drains accepted packets and closes the writer before returning.
 */
class TerminalLineRecorder final : public QObject
{
    Q_OBJECT
public:
    TerminalLineRecorder(TerminalDisplay& display, QVariantMap configuration, QObject* parent = nullptr);
    ~TerminalLineRecorder() override;
    Q_DISABLE_COPY_MOVE(TerminalLineRecorder)

    const QVariantMap& configuration() const;
    bool               stop(QString* error = nullptr);
    Q_SIGNAL void      error(const QString& message);

private:
    struct State;
    class Worker;
    static constexpr qsizetype maximumBytes   = 1024 * 1024;
    static constexpr qsizetype maximumPackets = 1024;
    QPointer<TerminalDisplay>  _display;
    QVariantMap                _configuration;
    std::shared_ptr<State>     _state;
    QThread                    _thread;
    Worker*                    _worker;
    QByteArray                 _line;
    quint64                    _resetRevision;
    bool                       _stopped = false;

    void _append(const QString& text, bool softWrapped);
    void _finishLine();
    void _enqueue(QByteArray bytes);
    void _flushScreen();
    void _synchronizeReset();
    void _fail(const QString& message);
};
