#include "TerminalLineRecorder.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QScopeGuard>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/Processing/LineflowEndpoint.h>
#include <algorithm>

struct TerminalLineRecorder::State
{
    QMutex    mutex;
    qsizetype bytes   = 0;
    qsizetype packets = 0;
    QString   failure;
};

class TerminalLineRecorder::Worker final : public QObject
{
public:
    Worker(TerminalLineRecorder& owner, std::shared_ptr<State> state) : _owner(owner), _state(std::move(state))
    {
    }

    void initialize(QVariantMap configuration)
    {
        configuration.insert("recordingMode", "packets");
        configuration.insert("format", "binary");
        configuration.insert("direction", "rx");
        QString error;
        _endpoint = LineflowEndpoint::create(configuration, 2, {}, &error);
        if (!_endpoint)
        {
            _fail(error);
        }
    }

    void write(QByteArray bytes)
    {
        const auto size    = bytes.size();
        const auto release = qScopeGuard([this, size] {
            const QMutexLocker lock(&_state->mutex);
            _state->bytes -= size;
            --_state->packets;
        });
        if (_failed || !_endpoint)
        {
            return;
        }
        QString    error;
        bool       completed = false;
        const auto result    = _endpoint->accept({std::move(bytes), 0}, {{}, [&](QString message) {
                                                                          completed = true;
                                                                          error     = std::move(message);
                                                                         }});
        // This worker uses only the synchronous built-in file endpoint, never plugin factories.
        if (result != Processor::AcceptResult::Accepted || !completed || !error.isEmpty())
        {
            _fail(error.isEmpty() ? QStringLiteral("Rendered line writer rejected a packet") : error);
        }
    }

    void finish()
    {
        if (_endpoint)
        {
            QString error;
            if (!LineflowEndpoint::flush(*_endpoint, &error))
            {
                _fail(error);
            }
            _endpoint->cancel({});
            _endpoint.reset();
        }
    }

private:
    TerminalLineRecorder&      _owner;
    std::shared_ptr<State>     _state;
    std::unique_ptr<Processor> _endpoint;
    bool                       _failed = false;

    void _fail(const QString& message)
    {
        _failed    = true;
        bool first = false;
        {
            const QMutexLocker lock(&_state->mutex);
            first = _state->failure.isEmpty();
            if (first)
            {
                _state->failure = message;
            }
        }
        if (first)
        {
            auto* owner = &_owner;
            QMetaObject::invokeMethod(owner, [owner, message] { emit owner->error(message); }, Qt::QueuedConnection);
        }
    }
};

TerminalLineRecorder::TerminalLineRecorder(TerminalDisplay& display, QVariantMap configuration, QObject* parent) :
    QObject(parent), _display(&display), _configuration(std::move(configuration)), _state(std::make_shared<State>()),
    _worker(new Worker(*this, _state)), _resetRevision(display.screen().resetRevision())
{
    _thread.setObjectName("TerminalLineRecorder");
    _worker->moveToThread(&_thread);
    connect(&_thread, &QThread::finished, _worker, &QObject::deleteLater);
    _thread.start();
    const auto        file = _configuration.value("file").toString();
    const QVariantMap fileConfiguration {
        {"enabled", true},
        {"file", QString(file.constData(), file.size())},
        {"append", _configuration.value("append", true).toBool()},
        {"maxFileBytes", _configuration.value("maxFileBytes", 16777216).toLongLong()},
        {"backups", _configuration.value("backups", 3).toInt()},
        {"flushEvery", _configuration.value("flushEvery", 1).toInt()}
    };
    QMetaObject::invokeMethod(
        _worker,
        [worker = _worker, fileConfiguration] { worker->initialize(fileConfiguration); },
        Qt::QueuedConnection);
    display.setCommittedLineHandler([this](const QString& text, bool softWrapped) {
        _synchronizeReset();
        _append(text, softWrapped);
    });
}

TerminalLineRecorder::~TerminalLineRecorder()
{
    QString error;
    if (!_stopped && !stop(&error))
    {
        qWarning().noquote() << "Rendered line recording stopped:" << error;
    }
}

const QVariantMap& TerminalLineRecorder::configuration() const
{
    return _configuration;
}

bool TerminalLineRecorder::stop(QString* error)
{
    if (!_stopped)
    {
        if (_display)
        {
            _display->setCommittedLineHandler({});
            _synchronizeReset();
            if (_configuration.value("flushScreen", true).toBool())
            {
                _flushScreen();
            }
        }
        if (!_line.isEmpty() && _configuration.value("flushScreen", true).toBool())
        {
            _finishLine();
        }
        _line.clear();
        _stopped = true;
        QMetaObject::invokeMethod(_worker, [worker = _worker] { worker->finish(); }, Qt::BlockingQueuedConnection);
        _thread.quit();
        _thread.wait();
        _worker = nullptr;
    }
    const QMutexLocker lock(&_state->mutex);
    if (error)
    {
        *error = _state->failure;
    }
    return _state->failure.isEmpty();
}

void TerminalLineRecorder::_append(const QString& text, bool softWrapped)
{
    {
        const QMutexLocker lock(&_state->mutex);
        if (!_state->failure.isEmpty())
        {
            return;
        }
    }
    const auto bytes = text.toUtf8();
    if (bytes.size() > maximumBytes - 2 - _line.size())
    {
        _line.clear();
        _fail(QStringLiteral("Rendered logical line exceeds the 1 MiB recording limit"));
        return;
    }
    _line += bytes;
    if (!softWrapped)
    {
        _finishLine();
    }
}

void TerminalLineRecorder::_finishLine()
{
    if (_configuration.value("trimTrailingSpaces", true).toBool())
    {
        while (_line.endsWith(' ') || _line.endsWith('\t'))
        {
            _line.chop(1);
        }
    }
    _line += _configuration.value("lineEnding", "lf") == "crlf" ? "\r\n" : "\n";
    _enqueue(std::move(_line));
    _line.clear();
}

void TerminalLineRecorder::_enqueue(QByteArray bytes)
{
    bool full = false;
    {
        const QMutexLocker lock(&_state->mutex);
        if (!_state->failure.isEmpty())
        {
            return;
        }
        full = bytes.size() > maximumBytes - _state->bytes || _state->packets >= maximumPackets;
        if (!full)
        {
            _state->bytes += bytes.size();
            ++_state->packets;
        }
    }
    if (full)
    {
        _fail(QStringLiteral("Rendered line recording queue exceeds 1 MiB or 1024 packets"));
        return;
    }
    QMetaObject::invokeMethod(
        _worker,
        [worker = _worker, bytes = std::move(bytes)]() mutable { worker->write(std::move(bytes)); },
        Qt::QueuedConnection);
}

void TerminalLineRecorder::_flushScreen()
{
    const auto& screen = _display->screen();
    if (screen.alternateScreen() && !_line.isEmpty())
    {
        _finishLine();
    }
    const auto& rows  = screen.lines();
    const int   first = screen.historyLineCount();
    int         last  = screen.cursorLine() - 1;
    for (int row = rows.size() - 1; row >= first; --row)
    {
        const auto text = TerminalScreen::lineText(rows[row]);
        if (std::any_of(text.cbegin(), text.cend(), [](QChar character) { return character != QLatin1Char(' '); }))
        {
            last = std::max(last, row);
            break;
        }
    }
    for (int row = first; row <= last; ++row)
    {
        _append(TerminalScreen::lineText(rows[row]), rows[row].softWrapped);
    }
}

void TerminalLineRecorder::_synchronizeReset()
{
    if (_display && _resetRevision != _display->screen().resetRevision())
    {
        // A reset discarded the mutable continuation of any unfinished logical line.
        _line.clear();
        _resetRevision = _display->screen().resetRevision();
    }
}

void TerminalLineRecorder::_fail(const QString& message)
{
    bool first = false;
    {
        const QMutexLocker lock(&_state->mutex);
        first = _state->failure.isEmpty();
        if (first)
        {
            _state->failure = message;
        }
    }
    if (first)
    {
        QMetaObject::invokeMethod(this, [this, message] { emit error(message); }, Qt::QueuedConnection);
    }
}
