#include "../Threading/ThreadJoinGuard.h"

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QPointer>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <Termaster/Core/Processing/ThreadedProcessor.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <mutex>

namespace
{
Q_LOGGING_CATEGORY(workerLog, "termaster.processing.worker")
Q_LOGGING_CATEGORY(workerTrace, "termaster.processing.worker.trace", QtInfoMsg)
} // namespace

struct ThreadedProcessor::State : std::enable_shared_from_this<State>
{
    struct Event
    {
        enum class Kind
        {
            Output,
            Completed,
            Error
        };
        Kind    kind;
        quint64 id = 0;
        Chunk   output;
        QString error;
    };
    struct Input
    {
        quint64 id;
        Chunk   chunk;
    };

    std::shared_ptr<const ProcessorRegistry::Prepared> prepared;
    std::shared_ptr<const void>                        execution;
    QVariantMap                                        overrides;
    Limits                                             limits;
    QVariantMap                                        configuration;
    QStringList                                        inputNames;
    QStringList                                        outputNames;
    ThreadJoinGuard                                    nativeJoin;
    QSemaphore                                         started;
    QString                                            startupError;
    QObject*                                           ownerContext  = nullptr;
    QObject*                                           workerContext = nullptr;
    QThread*                                           worker        = nullptr;
    QPointer<ThreadedProcessor>                        facade; // Accessed only by the facade thread.
    std::atomic<bool>                                  stopping = false;
    std::mutex                                         contextMutex;
    std::mutex                                         eventsMutex;
    std::deque<Event>                                  events;
    qsizetype                                          eventBytes     = 0;
    qsizetype                                          eventChunks    = 0;
    bool                                               deliveryQueued = false;
    bool                                               overflowed     = false;

    // The following state belongs exclusively to the worker thread.
    std::shared_ptr<Processor> processor;
    std::deque<Input>          inputs;
    QEventLoop*                loop = nullptr;
    QString                    directory;
    bool                       cancellationStarted = false;
    bool                       cleaned             = false;
    bool                       retryQueued         = false;

    bool enqueue(std::function<void()> operation)
    {
        const std::lock_guard lock(contextMutex);
        return workerContext && QMetaObject::invokeMethod(workerContext, std::move(operation), Qt::QueuedConnection);
    }

    void start()
    {
        qCDebug(workerTrace) << "Processor worker started";
        try
        {
            const bool retained = nativeJoin.captureCurrentThread(&startupError);
            if (!stopping && retained)
            {
                processor = prepared->instantiate(overrides, &startupError, directory);
                if (processor)
                {
                    configuration = processor->configuration();
                    auto self     = shared_from_this();
                    processor->setOutputHandler(
                        [self](Chunk output) { self->post({Event::Kind::Output, 0, std::move(output), {}}); });
                    processor->setErrorHandler([self](QString error) {
                        self->post({Event::Kind::Error, 0, {}, std::move(error)});
                        self->stopWorker();
                    });
                }
            }
        }
        catch (const std::exception& exception)
        {
            startupError = QString::fromLatin1("Processor worker startup failed: ")
                         + QString::fromUtf8(exception.what());
        }
        catch (...)
        {
            startupError = QString::fromLatin1("Processor worker startup failed with an unknown exception");
        }
        if (!processor && startupError.isEmpty())
        {
            startupError = QString::fromLatin1("Processor worker startup was cancelled");
        }
        started.release();
        if (!processor || !startupError.isEmpty() || stopping)
        {
            stopWorker();
        }
    }

    void post(Event event)
    {
        if (stopping)
        {
            return;
        }
        bool enqueueDelivery = false;
        bool overflow        = false;
        {
            const std::lock_guard lock(eventsMutex);
            if (overflowed)
            {
                return;
            }
            const bool full = events.size() >= size_t(limits.maxChunks) + size_t(inputNames.size());
            if (full
                || (event.kind == Event::Kind::Output
                    && (event.output.port < 0 || event.output.port >= outputNames.size()
                        || event.output.bytes.size() > limits.maxBytes - eventBytes
                        || eventChunks >= limits.maxChunks)))
            {
                qCWarning(workerLog) << "Processor output delivery exceeded configured queue limits";
                overflowed = true;
                overflow   = true;
                events.clear();
                eventBytes  = 0;
                eventChunks = 0;
                event       = {Event::Kind::Error,
                               0,
                               {},
                               QString::fromLatin1("Processor worker output queue limit exceeded")};
            }
            if (event.kind == Event::Kind::Output)
            {
                eventBytes += event.output.bytes.size();
                ++eventChunks;
                event.output.bytes = QByteArray(event.output.bytes.constData(), event.output.bytes.size());
            }
            event.error = QString(event.error.constData(), std::min<qsizetype>(event.error.size(), 4096));
            events.push_back(std::move(event));
            enqueueDelivery = !deliveryQueued;
            deliveryQueued  = true;
        }
        if (enqueueDelivery)
        {
            auto self = shared_from_this();
            QMetaObject::invokeMethod(
                ownerContext,
                [self] {
                    if (self->facade)
                    {
                        self->facade->_drain();
                    }
                },
                Qt::QueuedConnection);
        }
        if (overflow)
        {
            stopWorker();
        }
    }

    void pump()
    {
        if (stopping || cancellationStarted)
        {
            stopWorker();
            return;
        }
        const auto count = inputs.size();
        for (size_t index = 0; index < count && !inputs.empty(); ++index)
        {
            auto input = std::move(inputs.front());
            inputs.pop_front();
            const auto   id     = input.id;
            auto         self   = shared_from_this();
            AcceptResult result = AcceptResult::Rejected;
            try
            {
                result = processor->accept(input.chunk,
                                           {[self, id](Chunk output) {
                                                self->post({Event::Kind::Output, id, std::move(output), {}});
                                            },
                                            [self, id](QString error) {
                                                self->post({Event::Kind::Completed, id, {}, std::move(error)});
                                            }});
            }
            catch (const std::exception& exception)
            {
                post({Event::Kind::Error,
                      0,
                      {},
                      QString::fromLatin1("Processor execution failed: ") + QString::fromUtf8(exception.what())});
                stopWorker();
                return;
            }
            catch (...)
            {
                post({Event::Kind::Error,
                      0,
                      {},
                      QString::fromLatin1("Processor execution failed with an unknown exception")});
                stopWorker();
                return;
            }
            if (result == AcceptResult::WouldBlock)
            {
                inputs.push_back(std::move(input));
            }
            else if (result == AcceptResult::Rejected)
            {
                post({Event::Kind::Completed, id, {}, QString::fromLatin1("Processor rejected an admitted packet")});
            }
            if (stopping || cancellationStarted)
            {
                break;
            }
        }
        if (!inputs.empty() && !cancellationStarted && !retryQueued)
        {
            retryQueued = true;
            auto self   = shared_from_this();
            QTimer::singleShot(1, workerContext, [self] {
                self->retryQueued = false;
                self->pump();
            });
        }
    }

    void stopWorker()
    {
        if (cancellationStarted)
        {
            return;
        }
        qCDebug(workerTrace) << "Processor worker cancellation requested";
        cancellationStarted = true;
        inputs.clear();
        auto self    = shared_from_this();
        auto release = [self] {
            // Leave the acknowledgement's stack before releasing a provider tree.
            QMetaObject::invokeMethod(self->workerContext, [self] { self->cleanup(); }, Qt::QueuedConnection);
        };
        if (processor)
        {
            processor->setOutputHandler({});
            processor->setErrorHandler({});
            try
            {
                processor->cancel(std::move(release));
            }
            catch (...)
            {
                qCCritical(workerLog) << "Processor cancellation threw; worker and provider remain pinned";
            }
        }
        else
        {
            release();
        }
    }

    void cleanup()
    {
        if (cleaned)
        {
            return;
        }
        processor.reset();
        // Composite retirement may release more native children. Keep the event loop
        // and provider pins alive until every native object on this private thread died.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (prepared->activeInstancesOnCurrentThread() != 0)
        {
            auto self = shared_from_this();
            QTimer::singleShot(1, workerContext, [self] { self->cleanup(); });
            return;
        }
        cleaned = true;
        qCDebug(workerTrace) << "Processor worker tree destroyed";
        if (loop)
        {
            loop->quit();
        }
    }
};

ThreadedProcessor::ThreadedProcessor(std::shared_ptr<State> state) :
    _state(std::move(state)), _pending(size_t(_state->inputNames.size()))
{
}

ThreadedProcessor::~ThreadedProcessor()
{
    cancel({});
    _state->facade = nullptr;
}

std::shared_ptr<Processor> ThreadedProcessor::create(std::shared_ptr<const ProcessorRegistry::Prepared> prepared,
                                                     const QVariantMap&                                 overrides,
                                                     QString*                                           error,
                                                     const QString&                                     directory)
{
    if (error)
    {
        error->clear();
    }
    if (!prepared)
    {
        if (error)
        {
            *error = QString::fromLatin1("Processor preparation is missing");
        }
        return {};
    }
    auto state       = std::make_shared<State>();
    state->execution = prepared->retainExecution(error);
    if (!state->execution)
    {
        return {};
    }
    state->limits        = prepared->limits();
    state->inputNames    = prepared->inputNames();
    state->outputNames   = prepared->outputNames();
    state->prepared      = std::move(prepared);
    state->overrides     = overrides;
    state->directory     = directory;
    state->ownerContext  = new QObject;
    state->workerContext = new QObject;
    auto facade          = std::shared_ptr<ThreadedProcessor>(new ThreadedProcessor(state));
    state->facade        = facade.get();
    state->worker        = QThread::create([state] {
        state->start();
        QEventLoop loop;
        state->loop = &loop;
        if (!state->cleaned)
        {
            loop.exec();
        }
        state->loop = nullptr;
        const std::lock_guard lock(state->contextMutex);
        delete state->workerContext;
        state->workerContext = nullptr;
    });
    state->workerContext->moveToThread(state->worker);
    QObject::connect(state->worker, &QThread::finished, state->ownerContext, [state] {
        // finished precedes thread-local destruction. Join before releasing provider code
        // or the registry shutdown reservation, then acknowledge owner-thread quiescence.
        QString error;
        if (!state->worker->wait() || !state->nativeJoin.waitAndClose(&error))
        {
            qCCritical(workerLog) << "Processor worker join failed; provider remains pinned" << error;
            return;
        }
        state->prepared.reset();
        state->execution.reset();
        if (state->facade)
        {
            state->facade->_finish();
        }
        state->ownerContext->deleteLater();
    });
    QObject::connect(state->worker, &QThread::finished, state->worker, &QObject::deleteLater);
    state->worker->start();
    if (!state->started.tryAcquire(1, 5000))
    {
        if (error)
        {
            *error = QString::fromLatin1("Processor worker startup timed out");
        }
        return {};
    }
    if (!state->startupError.isEmpty())
    {
        if (error)
        {
            *error = state->startupError;
        }
        return {};
    }
    return facade;
}

QVariantMap ThreadedProcessor::configuration() const
{
    return _state->configuration;
}

int ThreadedProcessor::inputCount() const
{
    return int(_state->inputNames.size());
}

int ThreadedProcessor::outputCount() const
{
    return int(_state->outputNames.size());
}

QString ThreadedProcessor::inputName(int port) const
{
    return port >= 0 && port < inputCount() ? _state->inputNames[port] : QString {};
}

QString ThreadedProcessor::outputName(int port) const
{
    return port >= 0 && port < outputCount() ? _state->outputNames[port] : QString {};
}

Processor::AcceptResult ThreadedProcessor::accept(Chunk input, Callbacks callbacks)
{
    if (_stopped || input.port < 0 || input.port >= inputCount() || input.bytes.size() > _state->limits.maxBytes)
    {
        return AcceptResult::Rejected;
    }
    auto& pending = _pending[size_t(input.port)];
    if (pending.id || _pendingChunks >= _state->limits.maxChunks
        || input.bytes.size() > _state->limits.maxBytes - _pendingBytes)
    {
        return AcceptResult::WouldBlock;
    }
    const auto id = ++_nextId;
    pending       = {id, input.bytes.size(), std::move(callbacks)};
    ++_pendingChunks;
    _pendingBytes     += input.bytes.size();
    input.bytes        = QByteArray(input.bytes.constData(), input.bytes.size());
    auto       state   = _state;
    const bool queued  = state->enqueue([state, id, input = std::move(input)]() mutable {
        if (!state->stopping && !state->cancellationStarted)
        {
            state->inputs.push_back({id, std::move(input)});
            state->pump();
        }
    });
    if (!queued)
    {
        _pendingBytes -= pending.bytes;
        --_pendingChunks;
        pending = {};
        return AcceptResult::Rejected;
    }
    return AcceptResult::Accepted;
}

void ThreadedProcessor::cancel(std::function<void()> quiescent)
{
    if (_finished)
    {
        if (quiescent)
        {
            quiescent();
        }
        return;
    }
    if (quiescent)
    {
        _quiescent.push_back(std::move(quiescent));
    }
    if (_stopped)
    {
        return;
    }
    _stopped         = true;
    _state->stopping = true;
    setOutputHandler({});
    setErrorHandler({});
    for (auto& pending : _pending)
    {
        pending = {};
    }
    {
        const std::lock_guard lock(_state->eventsMutex);
        _state->events.clear();
        _state->eventBytes  = 0;
        _state->eventChunks = 0;
    }
    auto state = _state;
    state->enqueue([state] { state->stopWorker(); });
}

void ThreadedProcessor::_drain()
{
    QPointer<ThreadedProcessor> guard(this);
    for (int count = 0; guard && !_stopped && count < _state->limits.eventBudget; ++count)
    {
        State::Event event;
        {
            const std::lock_guard lock(_state->eventsMutex);
            if (_state->events.empty())
            {
                _state->deliveryQueued = false;
                return;
            }
            event = std::move(_state->events.front());
            _state->events.pop_front();
            if (event.kind == State::Event::Kind::Output)
            {
                _state->eventBytes -= event.output.bytes.size();
                --_state->eventChunks;
            }
        }
        if (event.kind == State::Event::Kind::Error)
        {
            _fail(std::move(event.error));
            return;
        }
        if (!event.id)
        {
            publishOutput(std::move(event.output));
            continue;
        }
        auto it = std::find_if(_pending.begin(), _pending.end(), [&](const Pending& value) {
            return value.id == event.id;
        });
        if (it == _pending.end())
        {
            continue;
        }
        if (event.kind == State::Event::Kind::Output)
        {
            auto callback = it->callbacks.output;
            if (callback)
            {
                callback(std::move(event.output));
            }
        }
        else
        {
            auto completed  = std::move(it->callbacks.completed);
            _pendingBytes  -= it->bytes;
            --_pendingChunks;
            *it = {};
            if (completed)
            {
                completed(std::move(event.error));
            }
        }
    }
    if (guard && !_stopped)
    {
        auto state = _state;
        QMetaObject::invokeMethod(
            state->ownerContext,
            [state] {
                if (state->facade)
                {
                    state->facade->_drain();
                }
            },
            Qt::QueuedConnection);
    }
}

void ThreadedProcessor::_finish()
{
    QPointer<ThreadedProcessor> guard(this);
    if (!_stopped)
    {
        // Deliver a queued worker failure before announcing quiescence.
        _drain();
        if (!guard)
        {
            return;
        }
        if (!_stopped)
        {
            _fail(QString::fromLatin1("Processor worker stopped"));
        }
    }
    if (!guard)
    {
        return;
    }
    _finished      = true;
    auto callbacks = std::move(_quiescent);
    for (auto& callback : callbacks)
    {
        callback();
    }
}

void ThreadedProcessor::_fail(QString error)
{
    std::vector<std::function<void(QString)>> callbacks;
    for (auto& pending : _pending)
    {
        if (pending.id && pending.callbacks.completed)
        {
            callbacks.push_back(std::move(pending.callbacks.completed));
        }
    }
    auto handlerGuard = QPointer<ThreadedProcessor>(this);
    if (callbacks.empty())
    {
        publishError(error);
    }
    for (auto& callback : callbacks)
    {
        if (!handlerGuard || _stopped)
        {
            return;
        }
        callback(error);
    }
    if (handlerGuard)
    {
        cancel({});
    }
}
