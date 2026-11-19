#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include <Termaster/Core/Processing/Codeflow.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/SettingsSchema.h>
#include <algorithm>
#include <functional>
#include <map>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
Q_LOGGING_CATEGORY(pythonLog, "termaster.processing.python")
Q_LOGGING_CATEGORY(pythonTrace, "termaster.processing.python.trace", QtInfoMsg)
constexpr qsizetype maxHeader = 4 * 1024 * 1024;
constexpr qsizetype maxSource = 2 * 1024 * 1024;

bool fail(QString* error, const QString& text)
{
    if (error)
    {
        *error = text;
    }
    return false;
}

int timeout(const QVariantMap& source, const QString& key, int fallback)
{
    return std::clamp(source.value(key, fallback).toInt(), 20, 30000);
}

QVariantMap sourceForExecution(QVariantMap source, const QString& directory, QString* error)
{
    auto file = source.value("file").toString();
    if (!file.isEmpty())
    {
        if (QDir::isRelativePath(file))
        {
            if (directory.isEmpty())
            {
                fail(error, "A relative script path requires an open workspace");
                return {};
            }
            file = QDir(directory).absoluteFilePath(file);
        }
        const auto expected = source.value("digest").toString();
        if (!source.contains("code") || !expected.isEmpty())
        {
            QFile input(file);
            if (!input.open(QIODevice::ReadOnly) || input.size() > maxSource)
            {
                fail(error, "Cannot read Python source (limit 2 MiB): " + file + ": " + input.errorString());
                return {};
            }
            const auto bytes = input.readAll();
            if (!expected.isEmpty()
                && expected != QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()))
            {
                fail(error, "Python source changed; Check and Apply the new revision: " + file);
                return {};
            }
            source.insert("code", QString::fromUtf8(bytes));
        }
        source.insert("file", QDir::cleanPath(file));
    }
    if (source.value("code").toString().toUtf8().size() > maxSource || source.value("code").toString().isEmpty())
    {
        fail(error, "Python source must contain 1 byte to 2 MiB of UTF-8 text");
        return {};
    }
    return source;
}

class Host final : public QObject, public std::enable_shared_from_this<Host>
{
public:
    using Handler = std::function<void(const QVariantMap&, const QByteArray&)>;
    QProcess               process;
    QMap<QString, Handler> handlers;
    QString                failure;
    QString                python;
    qsizetype              payloadLimit;

    Host(QString executable, qsizetype maxBytes) : python(std::move(executable)), payloadLimit(maxBytes)
    {
        clock.start();
        connect(&process, &QProcess::readyReadStandardOutput, this, [this] { read(); });
        connect(&process, &QProcess::readyReadStandardError, this, [this] {
            const auto bytes = process.readAllStandardError();
            if (diagnosticBytes < 65536)
            {
                const auto bounded  = bytes.left(65536 - diagnosticBytes);
                diagnosticBytes    += bounded.size();
                diagnostics        += QString::fromUtf8(bounded);
                qCInfo(pythonLog).noquote() << QString::fromUtf8(bounded).trimmed();
            }
        });
        connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            if (!closing && failure.isEmpty())
            {
                fault(QString("Python execution process exited (%1)").arg(code));
            }
        });
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, this, [this] {
            for (auto it = deadlines.cbegin(); it != deadlines.cend(); ++it)
            {
                if (it.value() <= clock.elapsed())
                {
                    fault("Python execution deadline exceeded; the diagram process was stopped");
                    return;
                }
            }
        });
    }
    ~Host() override
    {
        closing = true;
        timer.stop();
        process.kill();
        process.waitForFinished(500);
#ifdef Q_OS_WIN
        if (job)
        {
            CloseHandle(job);
        }
#endif
    }
    bool start(QString* error)
    {
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("PYTHONDONTWRITEBYTECODE", "1");
        process.setProcessEnvironment(env);
        process.setProgram(python);
        process.setArguments({"-u", "-I", "-B", Codeflow::hostScript()});
        process.start();
        if (!process.waitForStarted(2000))
        {
            return fail(error, "Cannot start Python: " + process.errorString());
        }
#ifdef Q_OS_WIN
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const auto child    = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process.processId()));
        const bool assigned = job && child
                           && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))
                           && AssignProcessToJobObject(job, child);
        if (child)
        {
            CloseHandle(child);
        }
        if (!assigned)
        {
            fault("Cannot establish Python process-tree ownership");
            return fail(error, failure);
        }
#endif
        timer.start();
        qCInfo(pythonLog) << "Python diagram process started" << process.processId();
        return true;
    }
    quint64 next()
    {
        return ++sequence;
    }
    bool send(QVariantMap header, const QByteArray& bytes = {})
    {
        if (!failure.isEmpty() || process.state() == QProcess::NotRunning)
        {
            return false;
        }
        header.insert("bytes", bytes.size());
        const auto json = QJsonDocument::fromVariant(header).toJson(QJsonDocument::Compact);
        if (json.size() > maxHeader || bytes.size() > payloadLimit)
        {
            fault("Python protocol message exceeds limits");
            return false;
        }
        QByteArray frame(4, Qt::Uninitialized);
        qToBigEndian<quint32>(quint32(json.size()), frame.data());
        frame                      += json;
        frame                      += bytes;
        const qsizetype queueLimit  = std::max<qsizetype>(8 * 1024 * 1024, 4 * payloadLimit + maxHeader);
        if (process.bytesToWrite() + frame.size() > queueLimit)
        {
            fault("Python protocol output queue is full");
            return false;
        }
        if (process.write(frame) != frame.size())
        {
            fault("Cannot queue Python request");
            return false;
        }
        return true;
    }
    QVariantMap blocking(QVariantMap                              header,
                         int                                      milliseconds,
                         QString*                                 error,
                         const std::shared_ptr<std::atomic_bool>& cancelled = {},
                         const QByteArray&                        data      = {})
    {
        const auto request = next();
        header.insert("request", qlonglong(request));
        blockingRequests.insert(request);
        const auto scope = qScopeGuard([&] { blockingRequests.remove(request); });
        if (!send(header, data))
        {
            fail(error, failure);
            return {};
        }
        QElapsedTimer elapsed;
        elapsed.start();
        while (!replies.contains(request) && failure.isEmpty())
        {
            if ((cancelled && *cancelled) || elapsed.elapsed() >= milliseconds)
            {
                fault(cancelled && *cancelled ? "Python operation cancelled" : "Python startup/inspection timed out");
                break;
            }
            process.waitForReadyRead(20);
            read();
            if (process.state() == QProcess::NotRunning && failure.isEmpty())
            {
                fault("Python process exited: " + diagnostics.right(4096));
            }
        }
        if (!failure.isEmpty())
        {
            fail(error, failure);
            return {};
        }
        auto reply = replies.take(request);
        if (reply.value("op").toString() == "error")
        {
            fail(error, reply.value("error").toString());
            return {};
        }
        return reply;
    }
    void deadline(quint64 request, int ms)
    {
        deadlines.insert(request, clock.elapsed() + ms);
    }
    void finished(quint64 request)
    {
        deadlines.remove(request);
    }
    void fault(QString error)
    {
        const auto retained = shared_from_this();
        if (!failure.isEmpty())
        {
            return;
        }
        failure = std::move(error);
        timer.stop();
        closing = true;
        process.kill();
#ifdef Q_OS_WIN
        if (job)
        {
            CloseHandle(job);
            job = nullptr;
        }
#endif
        qCWarning(pythonLog).noquote() << failure;
        const auto callbacks = handlers;
        for (const auto& callback : callbacks)
        {
            callback(
                {
                    {   "op", "fault"},
                    {"error", failure}
            },
                {});
        }
    }
    QString diagnostics;

private:
    QByteArray                 buffer;
    QMap<quint64, QVariantMap> replies;
    QSet<quint64>              blockingRequests;
    QMap<quint64, qint64>      deadlines;
    QElapsedTimer              clock;
    QTimer                     timer;
    quint64                    sequence        = 0;
    qsizetype                  diagnosticBytes = 0;
    bool                       closing         = false;
#ifdef Q_OS_WIN
    HANDLE job = nullptr;
#endif
    void read()
    {
        const auto retained  = shared_from_this();
        buffer              += process.readAllStandardOutput();
        if (buffer.size() > maxHeader + std::max<qsizetype>(payloadLimit * 4, 8 * 1024 * 1024))
        {
            fault("Python input channel exceeded its limit");
            return;
        }
        while (buffer.size() >= 4 && failure.isEmpty())
        {
            const auto size = qFromBigEndian<quint32>(buffer.constData());
            if (size < 2 || size > maxHeader)
            {
                fault("Invalid Python protocol header size");
                return;
            }
            if (buffer.size() < 4 + size)
            {
                return;
            }
            QJsonParseError parse;
            const auto      doc     = QJsonDocument::fromJson(buffer.mid(4, size), &parse);
            const auto      header  = doc.toVariant().toMap();
            const qint64    payload = header.value("bytes", 0).toLongLong();
            if (parse.error != QJsonParseError::NoError || !doc.isObject() || payload < 0 || payload > payloadLimit)
            {
                fault("Invalid Python protocol frame");
                return;
            }
            if (buffer.size() < 4 + size + payload)
            {
                return;
            }
            const auto bytes = buffer.mid(4 + size, payload);
            buffer.remove(0, 4 + size + payload);
            const auto op      = header.value("op").toString();
            const auto request = header.value("request").toULongLong();
            const auto node    = header.value("node").toString();
            if (op == "log")
            {
                if (diagnosticBytes < 65536)
                {
                    const auto text   = header.value("message").toString().left(4096);
                    diagnosticBytes  += text.toUtf8().size() + 1;
                    diagnostics      += text + "\n";
                    const auto level  = header.value("level").toString();
                    if (level == "debug" || level == "trace")
                    {
                        qCDebug(pythonTrace).noquote() << text;
                    }
                    else if (level == "warning" || level == "warn")
                    {
                        qCWarning(pythonLog).noquote() << text;
                    }
                    else if (level == "error" || level == "critical")
                    {
                        qCCritical(pythonLog).noquote() << text;
                    }
                    else
                    {
                        qCInfo(pythonLog).noquote() << text;
                    }
                }
            }
            else if (blockingRequests.contains(request) && (op == "ready" || op == "described" || op == "error"))
            {
                replies.insert(request, header);
            }
            else if (!node.isEmpty() && handlers.contains(node))
            {
                handlers.value(node)(header, QByteArray(bytes.constData(), bytes.size()));
            }
            else if (op == "described" || op == "ready" || op == "error")
            {
                replies.insert(request, header);
            }
            // Responses from a retired instance cannot affect its replacement.
        }
    }
};

thread_local QMap<QString, std::weak_ptr<Host>> graphHosts;

class ScriptProcessor final : public Processor
{
public:
    struct Pending
    {
        Callbacks callbacks;
        int       port   = 0;
        qsizetype bytes  = 0;
        qsizetype chunks = 0;
    };
    struct ChildCall
    {
        QString alias;
        quint64 request = 0;
        Chunk   input;
    };
    ScriptProcessor(std::shared_ptr<Host>                  host,
                    QVariantMap                            source,
                    QVariantMap                            configuration,
                    ProcessorCatalog::Provider::References references,
                    Limits                                 limits,
                    QString                                directory,
                    QVariantMap                            reportedConfiguration = {}) :
        _host(std::move(host)), _source(std::move(source)), _configuration(std::move(configuration)), _limits(limits),
        _directory(std::move(directory)), _id(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        _reportedConfiguration(std::move(reportedConfiguration))
    {
        _retryTimer.setInterval(1);
        connect(&_retryTimer, &QTimer::timeout, this, [this] { _retryCalls(); });
        _description = _source.value("description").toMap();
        _inputs      = _description.value("inputs").toList();
        _outputs     = _description.value("outputs").toList();
        _references  = std::move(references);
        QPointer<ScriptProcessor> guard(this);
        _host->handlers.insert(_id, [guard](const auto& header, const auto& bytes) {
            if (guard)
            {
                guard->_receive(header, bytes);
            }
        });
    }
    ~ScriptProcessor() override
    {
        _host->handlers.remove(_id);
        if (!_quiescent)
        {
            // A partial factory failure must retain child/provider ownership until
            // asynchronous cancellation acknowledges actual quiescence.
            for (const auto& child : std::as_const(_children))
            {
                child->cancel([retained = child] { });
            }
        }
    }
    bool start(QString* error)
    {
        auto source = sourceForExecution(_source, _directory, error);
        if (source.isEmpty())
        {
            return false;
        }
        const auto aliases = _description.value("nodes").toMap();
        for (auto it = aliases.cbegin(); it != aliases.cend(); ++it)
        {
            const auto node      = it.value().toMap();
            const auto reference = node.value("id").toString();
            if (!_references.contains(reference))
            {
                return fail(error, "Unknown declared child processor: " + reference);
            }
            auto child = _references.value(reference)(node.value("configuration").toMap(), _limits, error);
            if (!child)
            {
                return false;
            }
            _children.insert(it.key(), std::shared_ptr<Processor>(std::move(child)));
        }
        _startRequest = _host->next();
        _host->deadline(_startRequest, timeout(_source, "startupTimeoutMs", 3500));
        if (!_host->send({
                {        "op",                                                                       "create"},
                {   "request",                                                       qlonglong(_startRequest)},
                {      "node",                                                                            _id},
                {    "source",                                                                         source},
                {"parameters",                                                                 _configuration},
                {  "expected",                                                                   _description},
                { "directory",                                                                     _directory},
                {    "limits", QVariantMap {{"maxBytes", _limits.maxBytes}, {"maxChunks", _limits.maxChunks}}}
        }))
        {
            return fail(error, _host->failure);
        }
        return true;
    }
    QVariantMap configuration() const override
    {
        return _reportedConfiguration.isEmpty() ? _configuration : _reportedConfiguration;
    }
    int inputCount() const override
    {
        return _inputs.size();
    }
    int outputCount() const override
    {
        return _outputs.size();
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        if (_stopped || !_host->failure.isEmpty() || input.port < 0 || input.port >= inputCount()
            || input.bytes.size() > _limits.maxBytes)
        {
            return AcceptResult::Rejected;
        }
        if (!_ready)
        {
            return AcceptResult::WouldBlock;
        }
        for (const auto& pending : _pending)
        {
            if (pending.port == input.port)
            {
                return AcceptResult::WouldBlock;
            }
        }
        const auto request = _host->next();
        _pending.insert(request, {std::move(callbacks), input.port, 0, 0});
        _host->deadline(request, timeout(_source, "timeoutMs", 1000));
        _host->send(
            {
                {     "op",           "packet"},
                {   "node",                _id},
                {"request", qlonglong(request)},
                {   "port",         input.port}
        },
            input.bytes);
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> ready) override
    {
        if (_quiescent)
        {
            if (ready)
            {
                ready();
            }
            return;
        }
        if (ready)
        {
            _cancelled.push_back(std::move(ready));
        }
        if (_stopped)
        {
            return;
        }
        _stopped         = true;
        _settingUpCancel = true;
        _host->finished(_startRequest);
        for (auto it = _pending.cbegin(); it != _pending.cend(); ++it)
        {
            _host->finished(it.key());
        }
        _pending.clear();
        _retryTimer.stop();
        _queuedCalls.clear();
        _childCalls.clear();
        _queuedCallBytes = 0;
        setOutputHandler({});
        setErrorHandler({});
        _remainingChildren = _children.size();
        QPointer<ScriptProcessor> guard(this);
        for (const auto& child : std::as_const(_children))
        {
            child->cancel([guard] {
                if (guard)
                {
                    --guard->_remainingChildren;
                    guard->_finishCancel();
                }
            });
        }
        if (!_host->failure.isEmpty())
        {
            _pythonStopped = true;
        }
        else
        {
            _cancelRequest = _host->next();
            _host->deadline(_cancelRequest, timeout(_source, "stopTimeoutMs", 500));
            _host->send({
                {     "op",                  "cancel"},
                {   "node",                       _id},
                {"request", qlonglong(_cancelRequest)}
            });
        }
        _settingUpCancel = false;
        _finishCancel();
    }
    qint64 processId() const
    {
        return _host->process.processId();
    }

private:
    std::shared_ptr<Host>                     _host;
    QVariantMap                               _source;
    QVariantMap                               _configuration;
    Limits                                    _limits;
    QString                                   _directory;
    QString                                   _id;
    QVariantMap                               _reportedConfiguration;
    QVariantMap                               _description;
    QVariantList                              _inputs;
    QVariantList                              _outputs;
    ProcessorCatalog::Provider::References    _references;
    QMap<QString, std::shared_ptr<Processor>> _children;
    QMap<quint64, Pending>                    _pending;
    QQueue<ChildCall>                         _queuedCalls;
    QSet<quint64>                             _childCalls;
    QTimer                                    _retryTimer;
    qsizetype                                 _queuedCallBytes = 0;
    std::vector<std::function<void()>>        _cancelled;
    quint64                                   _cancelRequest   = 0;
    quint64                                   _startRequest    = 0;
    bool                                      _ready           = false;
    bool                                      _settingUpCancel = false;
    qsizetype                                 _idleBytes       = 0;
    qsizetype                                 _idleChunks      = 0;
    qint64                                    _idleWindow      = 0;
    QElapsedTimer                             _idleClock;
    int                                       _remainingChildren = 0;
    bool                                      _stopped           = false;
    bool                                      _pythonStopped     = false;
    bool                                      _quiescent         = false;

    void _finishCancel()
    {
        if (!_stopped || !_pythonStopped || _remainingChildren || _quiescent || _settingUpCancel)
        {
            return;
        }
        _quiescent = true;
        _children.clear();
        const auto callbacks = std::move(_cancelled);
        for (const auto& callback : callbacks)
        {
            callback();
        }
    }
    void _receive(const QVariantMap& header, const QByteArray& data)
    {
        const auto op      = header.value("op").toString();
        const auto request = header.value("request").toULongLong();
        if (op == "ready")
        {
            _host->finished(request);
            _ready = true;
            return;
        }
        if (op == "error")
        {
            _host->finished(request);
            _host->fault(header.value("error").toString());
            return;
        }
        if (op == "fault")
        {
            _pythonStopped     = true;
            const auto pending = std::move(_pending);
            _pending.clear();
            if (!_stopped)
            {
                const auto                error = header.value("error").toString();
                QPointer<ScriptProcessor> guard(this);
                for (const auto& invocation : pending)
                {
                    if (invocation.callbacks.completed)
                    {
                        invocation.callbacks.completed(error);
                    }
                    if (!guard)
                    {
                        return;
                    }
                }
                if (pending.isEmpty())
                {
                    publishError(error);
                }
                if (!guard)
                {
                    return;
                }
            }
            _finishCancel();
            return;
        }
        if (op == "cancelled")
        {
            _host->finished(request);
            _pythonStopped = true;
            _finishCancel();
            return;
        }
        if (_stopped)
        {
            return;
        }
        if (op == "call")
        {
            _call(header, data);
            return;
        }
        if (op == "output")
        {
            const int port = header.value("port").toInt();
            if (port < 0 || port >= outputCount())
            {
                _host->fault("Python returned an invalid output port");
                return;
            }
            if (!request)
            {
                if (!_idleClock.isValid())
                {
                    _idleClock.start();
                }
                if (_idleClock.elapsed() - _idleWindow >= 1000)
                {
                    _idleWindow = _idleClock.elapsed();
                    _idleBytes  = 0;
                    _idleChunks = 0;
                }
                if (++_idleChunks > _limits.maxChunks || data.size() > _limits.maxBytes - _idleBytes)
                {
                    _host->fault("Python unsolicited output exceeded its admission limit");
                    return;
                }
                _idleBytes += data.size();
                publishOutput({QByteArray(data.constData(), data.size()), port});
                return;
            }
            auto it = _pending.find(request);
            if (it == _pending.end())
            {
                return;
            }
            if (++it->chunks > _limits.maxChunks || data.size() > _limits.maxBytes - it->bytes)
            {
                _host->fault("Python packet output exceeded its expansion limit");
                return;
            }
            it->bytes         += data.size();
            const auto output  = it->callbacks.output;
            if (output)
            {
                output({QByteArray(data.constData(), data.size()), port});
            }
        }
        else if (op == "done")
        {
            _host->finished(request);
            const auto pending = _pending.take(request);
            if (pending.callbacks.completed)
            {
                pending.callbacks.completed(header.value("error").toString());
            }
        }
    }
    void _call(const QVariantMap& header, const QByteArray& bytes)
    {
        const auto alias   = header.value("alias").toString();
        const auto request = header.value("request").toULongLong();
        if (!_children.contains(alias))
        {
            _host->send({
                {     "op",                  "call_done"},
                {   "node",                          _id},
                {"request",           qlonglong(request)},
                {  "error", "Undeclared child processor"}
            });
            return;
        }
        if (_childCalls.size() >= 256 || bytes.size() > _limits.maxBytes * 4 - _queuedCallBytes)
        {
            _host->fault("Python child call queue exceeded its admission limit");
            return;
        }
        _childCalls.insert(request);
        _queuedCallBytes += bytes.size();
        _queuedCalls.enqueue({
            alias,
            request,
            {QByteArray(bytes.constData(), bytes.size()), header.value("port").toInt()}
        });
        if (!_retryTimer.isActive())
        {
            _retryTimer.start();
        }
    }
    void _retryCalls()
    {
        if (_stopped || !_host->failure.isEmpty())
        {
            _retryTimer.stop();
            _queuedCalls.clear();
            _childCalls.clear();
            _queuedCallBytes = 0;
            return;
        }
        QPointer<ScriptProcessor> guard(this);
        const int                 attempts = std::min<int>(64, _queuedCalls.size());
        for (int index = 0; index < attempts; ++index)
        {
            auto call           = _queuedCalls.dequeue();
            _queuedCallBytes   -= call.input.bytes.size();
            const auto request  = call.request;
            auto       outputs  = std::make_shared<qsizetype>(0);
            auto       chunks   = std::make_shared<qsizetype>(0);
            const auto child    = _children.value(call.alias);
            const auto result   = child->accept(call.input,
                                                {[guard, request, outputs, chunks](Chunk output) {
                                                   if (!guard || guard->_stopped)
                                                   {
                                                       return;
                                                   }
                                                   if (++*chunks > guard->_limits.maxChunks
                                                       || output.bytes.size() > guard->_limits.maxBytes - *outputs)
                                                   {
                                                       guard->_host->fault(
                                                           "Native child output exceeded Python call limits");
                                                       return;
                                                   }
                                                   *outputs += output.bytes.size();
                                                   guard->_host->send(
                                                       {
                                                           {     "op",      "call_output"},
                                                           {   "node",         guard->_id},
                                                           {"request", qlonglong(request)},
                                                           {   "port",        output.port}
                                                   },
                                                       output.bytes);
                                                 },
                                                 [guard, request](QString error) {
                                                   if (guard && !guard->_stopped)
                                                   {
                                                       guard->_childCalls.remove(request);
                                                       guard->_host->send({
                                                           {     "op",        "call_done"},
                                                           {   "node",         guard->_id},
                                                           {"request", qlonglong(request)},
                                                           {  "error",              error}
                                                       });
                                                   }
                                                 }});
            // Child callbacks may synchronously fault, cancel or destroy the parent.
            if (!guard || guard->_stopped || !guard->_host->failure.isEmpty())
            {
                return;
            }
            if (result == AcceptResult::WouldBlock)
            {
                _queuedCallBytes += call.input.bytes.size();
                _queuedCalls.enqueue(std::move(call));
            }
            else if (result == AcceptResult::Rejected)
            {
                _childCalls.remove(request);
                _host->send({
                    {     "op",                   "call_done"},
                    {   "node",                           _id},
                    {"request",            qlonglong(request)},
                    {  "error", "Native child rejected input"}
                });
                if (!guard || guard->_stopped || !guard->_host->failure.isEmpty())
                {
                    return;
                }
            }
        }
        if (_queuedCalls.isEmpty())
        {
            _retryTimer.stop();
        }
    }
};
std::unique_ptr<Processor> createScript(const QVariantMap&                            source,
                                        const QVariantMap&                            parameters,
                                        const ProcessorCatalog::Provider::References& references,
                                        const Processor::Limits&                      limits,
                                        QString*                                      error,
                                        const QVariantMap&                            reportedConfiguration = {})
{
    const auto execution = ProcessorRegistry::currentExecution();
    auto       python    = source.value("python").toString();
    if (python.isEmpty())
    {
        python = Codeflow::defaultPython();
    }
    graphHosts.removeIf([](const auto& entry) { return entry.value().expired(); });
    auto host = graphHosts.value(execution.id).lock();
    if (host && host->python != python)
    {
        fail(error, "Codeflow nodes in one diagram must use the same Python environment");
        return {};
    }
    if (!host)
    {
        host = std::make_shared<Host>(python, limits.maxBytes);
        if (!host->start(error))
        {
            return {};
        }
        graphHosts.insert(execution.id, host);
    }
    auto result = std::make_unique<ScriptProcessor>(host,
                                                    source,
                                                    parameters,
                                                    references,
                                                    limits,
                                                    execution.directory,
                                                    reportedConfiguration);
    if (!result->start(error))
    {
        return {};
    }
    return result;
}

QVariantMap templateDescription(int channels)
{
    const QVariantMap property {
        {           "type", "string"},
        {          "title", "Prefix"},
        {        "default",       ""},
        {"x-settings-path", "prefix"},
        {        "x-order",        0}
    };
    const QVariantMap schema {
        {   "$schema", "http://json-schema.org/draft-07/schema#"},
        {      "type",                                  "object"},
        {"properties",        QVariantMap {{"prefix", property}}},
        {  "required",                   QVariantList {"prefix"}}
    };
    const auto port = [](const QString& id, const QString& title, const QString& flow) {
        return QVariantMap {
            {   "id",      id},
            {"title",   title},
            { "flow",    flow},
            { "type", "bytes"}
        };
    };
    QVariantList inputs {port("incoming", "Data", "rx")};
    QVariantList outputs {port("outgoing", "Data", "rx")};
    if (channels == 2)
    {
        inputs  = {port("rx_in", "Wire", "rx"), port("tx_in", "Data", "tx")};
        outputs = {port("rx_out", "Data", "rx"), port("tx_out", "Wire", "tx")};
    }
    return {
        {"className",                   "Codeflow"},
        {    "title",                   "Codeflow"},
        {   "inputs",                       inputs},
        {  "outputs",                      outputs},
        {   "schema",                       schema},
        { "defaults", QVariantMap {{"prefix", ""}}},
        {    "nodes",               QVariantMap {}}
    };
}
} // namespace

bool Codeflow::isInline(const QString& id)
{
    return id == "native.python-code" || id == "native.duplex-python-code";
}

QVariantMap Codeflow::inlineDefinition(const QString&     id,
                                       const QString&     title,
                                       int                channels,
                                       const QVariantMap& configuration)
{
    if (channels != 1 && channels != 2)
    {
        return {};
    }
    auto source = configuration.value("source").toMap();
    source.remove("file");
    source.remove("digest");
    if (!source.contains("code"))
    {
        source.insert("code", templateSource(channels));
    }
    auto description = source.value("description").toMap();
    if (description.isEmpty())
    {
        description = templateDescription(channels);
    }
    auto result         = definition(id, title, source, description);
    auto embeddedSource = result.value("source").toMap();
    embeddedSource.remove("digest");
    result.insert("source", embeddedSource);
    result.insert("defaults", configuration.value("parameters", description.value("defaults")));
    return result;
}

QVariantMap Codeflow::inlineConfiguration(const QVariantMap& definition)
{
    auto source = definition.value("source").toMap();
    source.remove("file");
    source.remove("digest");
    return {
        {    "source",                               source},
        {"parameters", definition.value("defaults").toMap()}
    };
}

ProcessorCatalog::Type Codeflow::inlineType(int channels)
{
    ProcessorCatalog::Type type;
    type.id          = channels == 1 ? "native.python-code" : "native.duplex-python-code";
    type.title       = channels == 1 ? QString::fromUtf8("Python Code (1×1)") : QString::fromUtf8("Python Code (2×2)");
    type.group       = "General";
    type.inputCount  = channels;
    type.outputCount = channels;
    type.inputNames  = channels == 1 ? QStringList {"Data"} : QStringList {"Wire", "Data"};
    type.outputNames = channels == 1 ? QStringList {"Data"} : QStringList {"Data", "Wire"};
    type.editor      = {
        {"inlineCode",                  true},
        {  "channels",              channels},
        { "helpTopic", "processing.codeflow"}
    };
    type.settingsSchema = SettingsSchema::serializeValue(QVariantMap {
        {    "version",                                                                                                1},
        {       "type",                                                                      "termaster.settings-schema"},
        {"description",                                                            "Inline Python source and parameters"},
        {    "content", QVariantMap {{"type", "object"}, {"properties", QVariantMap {}}, {"additionalProperties", true}}}
    });
    // Descriptor maps are occurrence snapshots. Keeping them out of catalog
    // defaults avoids recursive overlays retaining fields from the template.
    type.defaults       = {
        {    "source", QVariantMap {{"code", templateSource(channels)}}},
        {"parameters",                                   QVariantMap {}}
    };
    type.create = [channels](const QVariantMap& configuration, const Processor::Limits& limits, QString* error) {
        const auto baseline    = inlineDefinition({}, {}, channels, configuration);
        const auto resolved    = inlineConfiguration(baseline);
        const auto source      = resolved.value("source").toMap();
        const auto description = source.value("description").toMap();
        const auto inputs      = description.value("inputs").toList();
        const auto outputs     = description.value("outputs").toList();
        if (inputs.size() != channels || outputs.size() != channels)
        {
            fail(error, "Inline Python Code ports must match the selected 1x1 or 2x2 node");
            return std::unique_ptr<Processor> {};
        }
        if (!description.value("nodes").toMap().isEmpty())
        {
            fail(error,
                 "Inline Python Code does not support declared Node children; publish a workspace Codeflow instead");
            return std::unique_ptr<Processor> {};
        }
        return createScript(source, resolved.value("parameters").toMap(), {}, limits, error, resolved);
    };
    return type;
}

QString Codeflow::defaultPython()
{
    const auto configured = qEnvironmentVariable("TERMASTER_PYTHON");
    if (!configured.isEmpty())
    {
        return configured;
    }
#ifdef TERMASTER_PYTHON_DEFAULT
    if (QFileInfo::exists(QStringLiteral(TERMASTER_PYTHON_DEFAULT)))
    {
        return QStringLiteral(TERMASTER_PYTHON_DEFAULT);
    }
#endif
    auto result = QStandardPaths::findExecutable("python3");
    return result.isEmpty() ? QStandardPaths::findExecutable("python") : result;
}

QString Codeflow::hostScript()
{
    const auto installed = QDir(QCoreApplication::applicationDirPath()).filePath("Python/CodeflowHost.py");
    if (QFileInfo::exists(installed))
    {
        return installed;
    }
#ifdef TERMASTER_PYTHON_RUNTIME
    return QStringLiteral(TERMASTER_PYTHON_RUNTIME) + "/CodeflowHost.py";
#else
    return installed;
#endif
}

QString Codeflow::templateSource(int channels)
{
    if (channels == 1)
    {
        return QStringLiteral(
            "from termaster import Processor, Property, Input, Output, handles\n\n\nclass Codeflow(Processor):\n"
            "    prefix = Property(str, default=\"\", title=\"Prefix\")\n"
            "    incoming = Input(bytes, title=\"Data\")\n    outgoing = Output(bytes, title=\"Data\")\n\n"
            "    @handles(incoming, output=outgoing)\n    def process(self, data: bytes):\n        return self.prefix.encode(\"utf-8\") + data\n");
    }
    return QStringLiteral(
        "from termaster import Processor, Property, Input, Output, handles\n\n\nclass Codeflow(Processor):\n"
        "    prefix = Property(str, default=\"\", title=\"Prefix\")\n"
        "    rx_in = Input(bytes, title=\"Wire\", flow=\"rx\")\n    rx_out = Output(bytes, title=\"Data\", flow=\"rx\")\n"
        "    tx_in = Input(bytes, title=\"Data\", flow=\"tx\")\n    tx_out = Output(bytes, title=\"Wire\", flow=\"tx\")\n\n"
        "    @handles(rx_in, output=rx_out)\n    def receive(self, data: bytes):\n        return self.prefix.encode(\"utf-8\") + data\n\n"
        "    @handles(tx_in, output=tx_out)\n    def transmit(self, data: bytes):\n        return data\n");
}

QVariantMap Codeflow::inspect(const QVariantMap& input, QString* error, std::shared_ptr<std::atomic_bool> cancelled)
{
    if (error)
    {
        error->clear();
    }
    const auto directory = input.value("directory").toString();
    auto       source    = sourceForExecution(input, directory, error);
    if (source.isEmpty())
    {
        return {};
    }
    auto host = std::make_shared<Host>(source.value("python", defaultPython()).toString(), 1024 * 1024);
    if (!host->start(error))
    {
        return {};
    }
    const auto reply = host->blocking(
        {
            {       "op", "describe"},
            {   "source",     source},
            {"directory",  directory}
    },
        timeout(source, "startupTimeoutMs", 3500),
        error,
        cancelled);
    return reply.value("description").toMap();
}

QVariantMap Codeflow::test(const QVariantMap&                input,
                           const QVariantMap&                parameters,
                           int                               port,
                           const QByteArray&                 data,
                           QString*                          error,
                           std::shared_ptr<std::atomic_bool> cancelled)
{
    if (error)
    {
        error->clear();
    }
    auto       source       = input;
    const auto dependencies = source.take("testDefinitions").toMap();
    const auto description  = inspect(source, error, cancelled);
    if (description.isEmpty())
    {
        return {};
    }
    const auto directory    = source.value("directory").toString();
    const auto id           = "test.codeflow." + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto       definition   = Codeflow::definition(id, "Test", source, description);
    auto       storedSource = definition.value("source").toMap();
    storedSource.remove("digest"); // Tests deliberately execute the editor draft, not the file on disk.
    definition.insert("source", storedSource);
    ProcessorRegistry registry;
    registerNativeProcessors(registry, "test.native");
    Codeflow::registerProvider(registry);
    auto definitions = dependencies;
    definitions.insert(id, definition);
    const auto parsed = ProcessorRegistry::parseDefinitions(definitions, error);
    if (!parsed || !registry.publishDefinitions("test", *parsed, error))
    {
        return {};
    }
    const auto prepared = registry.prepare(id, parameters, {}, error);
    if (!prepared)
    {
        return {};
    }
    graphHosts.removeIf([](const auto& entry) { return entry.value().expired(); });
    const auto previousHosts = graphHosts.keys();
    auto       runtime       = prepared->instantiate({}, error, directory);
    if (!runtime)
    {
        return {};
    }
    std::vector<std::shared_ptr<Host>> testHosts;
    for (auto it = graphHosts.cbegin(); it != graphHosts.cend(); ++it)
    {
        if (!previousHosts.contains(it.key()))
        {
            if (auto host = it.value().lock())
            {
                testHosts.push_back(std::move(host));
            }
        }
    }
    QVariantList         outputs;
    bool                 completed  = false;
    bool                 accepted   = false;
    bool                 stopped    = false;
    qsizetype            totalBytes = 0;
    QString              failure;
    Processor::Callbacks callbacks {[&](Processor::Chunk chunk) {
                                        if (outputs.size() >= 1024 || chunk.bytes.size() > 1024 * 1024 - totalBytes)
                                        {
                                            failure   = "Python test output exceeded limits";
                                            completed = true;
                                            return;
                                        }
                                        totalBytes += chunk.bytes.size();
                                        outputs.append(QVariantMap {
                                            {"port",  chunk.port},
                                            {"data", chunk.bytes}
                                        });
                                    },
                                    [&](QString reason) {
                                        failure   = std::move(reason);
                                        completed = true;
                                    }};
    runtime->setErrorHandler([&](QString reason) {
        failure   = std::move(reason);
        completed = true;
    });
    QElapsedTimer elapsed;
    elapsed.start();
    const int duration = timeout(source, "startupTimeoutMs", 3500) + timeout(source, "timeoutMs", 1000);
    while (!completed)
    {
        if ((cancelled && *cancelled) || elapsed.elapsed() >= duration)
        {
            failure = "Python test cancelled or timed out";
            for (const auto& host : testHosts)
            {
                host->fault(failure);
            }
            break;
        }
        if (!accepted)
        {
            const auto result = runtime->accept({data, port}, callbacks);
            accepted          = result == Processor::AcceptResult::Accepted;
            if (result == Processor::AcceptResult::Rejected)
            {
                failure = "Python test rejected the input port or packet";
                break;
            }
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    runtime->cancel([&] { stopped = true; });
    // Native child cancellation remains authoritative; keep its objects and callbacks
    // alive while pumping the test worker, never a GUI nested event loop.
    while (!stopped)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    QString diagnostics;
    for (const auto& host : testHosts)
    {
        diagnostics += host->diagnostics;
    }
    diagnostics += failure;
    runtime.reset();
    registry.shutdown(1000);
    if (!failure.isEmpty())
    {
        fail(error, failure);
    }
    return {
        {    "outputs",     outputs},
        {"diagnostics", diagnostics}
    };
}

QVariantMap Codeflow::definition(const QString&     id,
                                 const QString&     title,
                                 const QVariantMap& source,
                                 const QVariantMap& description)
{
    QVariantMap envelope {
        {    "version",                             1},
        {       "type",   "termaster.settings-schema"},
        {"description", "Python processor properties"},
        {    "content",   description.value("schema")}
    };
    auto stored = source;
    stored.remove("directory");
    stored.remove("testDefinitions");
    stored.insert("description", description);
    stored.insert("className", description.value("className"));
    stored.insert("digest",
                  QString::fromLatin1(
                      QCryptographicHash::hash(source.value("code").toString().toUtf8(), QCryptographicHash::Sha256)
                          .toHex()));
    QVariantList inputs;
    QVariantList outputs;
    for (const auto& port : description.value("inputs").toList())
    {
        inputs.append(port.toMap().value("title").toString());
    }
    for (const auto& port : description.value("outputs").toList())
    {
        outputs.append(port.toMap().value("title").toString());
    }
    QStringList references;
    for (const auto& child : description.value("nodes").toMap())
    {
        references.append(child.toMap().value("id").toString());
    }
    references.removeDuplicates();
    return {
        {            "id",                                                          id},
        {          "name",                                                       title},
        {          "kind",                                                  "codeflow"},
        {         "group",                                                    "Python"},
        {        "source",                                                      stored},
        {"settingsSchema", QString::fromUtf8(SettingsSchema::serializeValue(envelope))},
        {      "defaults",                               description.value("defaults")},
        {    "inputCount",                                               inputs.size()},
        {   "outputCount",                                              outputs.size()},
        {    "inputNames",                                                      inputs},
        {   "outputNames",                                                     outputs},
        {    "references",                               QVariant(references).toList()},
        {        "editor",          QVariantMap {{"helpTopic", "processing.codeflow"}}}
    };
}

void Codeflow::registerProvider(ProcessorRegistry& registry)
{
    ProcessorCatalog::Provider provider;
    provider.validate = [](const QVariantMap& source, QString* error) {
        const auto description = source.value("description").toMap();
        const auto inputs      = description.value("inputs").toList().size();
        if (inputs < 1 || inputs > 2 || description.value("outputs").toList().size() != inputs
            || source.value("code").toString().toUtf8().size() > maxSource || source.value("code").toString().isEmpty())
        {
            return fail(error, "Codeflow requires checked Python source and a 1x1 or 2x2 byte interface");
        }
        try
        {
            SettingsSchema schema(SettingsSchema::serializeValue(QVariantMap {
                {    "version",                           1},
                {       "type", "termaster.settings-schema"},
                {"description",                  "Codeflow"},
                {    "content", description.value("schema")}
            }));
        }
        catch (const std::exception& exception)
        {
            return fail(error, QString::fromUtf8(exception.what()));
        }
        return true;
    };
    provider.prepare = [](const QVariantMap& source,
                          const QVariantMap&,
                          const ProcessorCatalog::Provider::References& references,
                          const Processor::Limits&,
                          QString*) -> ProcessorCatalog::Type::Factory {
        return [source, references](const QVariantMap&       parameters,
                                    const Processor::Limits& limits,
                                    QString*                 error) -> std::unique_ptr<Processor> {
            return createScript(source, parameters, references, limits, error);
        };
    };
    registry.registerProvider("builtin.python", "codeflow", std::move(provider));
}
