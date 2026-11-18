#include "Tap.h"

#include <QPointer>
#include <QTimer>
#include <algorithm>

Tap::Tap(std::shared_ptr<Processor> branch, QVariantMap configuration, Limits limits) :
    _configuration(std::move(configuration)), _limits(limits), _branch(std::move(branch))
{
    if (_branch)
    {
        QPointer<Tap> guard(this);
        _branch->setOutputHandler([guard](Chunk output) {
            if (guard && !guard->_stopped && output.port != 0)
            {
                guard->_fail(QStringLiteral("Tap branch emitted an invalid output port"));
            }
        });
        _branch->setErrorHandler([guard](QString error) {
            if (guard && !guard->_stopped)
            {
                guard->_fail(QStringLiteral("Tap branch: ") + error.left(4096));
            }
        });
    }
}

QByteArray Tap::schema()
{
    return R"(version: 1
type: termaster.settings-schema
description: Tap copy branch configuration
content:
  type: object
)";
}

std::unique_ptr<Processor> Tap::create(const QVariantMap& configuration, const Limits& limits, QString* error)
{
    return compose({}, configuration, limits, error);
}

std::unique_ptr<Processor> Tap::compose(std::shared_ptr<Processor> branch,
                                        QVariantMap                configuration,
                                        Limits                     limits,
                                        QString*                   error)
{
    if ((branch && (branch->inputCount() != 1 || branch->outputCount() != 1)) || limits.maxBytes <= 0
        || limits.maxChunks <= 0 || limits.eventBudget <= 0 || limits.maxSteps <= 0 || limits.maxDepth <= 0
        || limits.maxInstances <= 0)
    {
        if (error)
        {
            *error = QStringLiteral("Tap requires a 1x1 branch and positive processing limits");
        }
        return {};
    }
    return std::unique_ptr<Tap>(new Tap(std::move(branch), std::move(configuration), limits));
}

QVariantMap Tap::configuration() const
{
    return _configuration;
}

Processor::AcceptResult Tap::accept(Chunk input, Callbacks callbacks)
{
    if (_stopped || input.port != 0 || input.bytes.size() > _limits.maxBytes)
    {
        return AcceptResult::Rejected;
    }
    if (_accepting)
    {
        return AcceptResult::WouldBlock;
    }
    if (_branch)
    {
        if (_queue.size() + size_t(_busy) >= size_t(_limits.maxChunks)
            || input.bytes.size() > _limits.maxBytes - _pendingBytes)
        {
            _stopped = true;
            _cancelBranch();
            if (callbacks.completed)
            {
                callbacks.completed(QStringLiteral("Tap copy branch queue limit exceeded"));
            }
            return AcceptResult::Accepted;
        }
        Chunk copy {QByteArray(input.bytes.constData(), input.bytes.size()), 0};
        _pendingBytes += copy.bytes.size();
        _queue.push_back(std::move(copy));
        _schedule();
    }

    _accepting = true;
    QPointer<Tap> guard(this);
    if (callbacks.output)
    {
        input.bytes = QByteArray(input.bytes.constData(), input.bytes.size());
        callbacks.output(std::move(input));
    }
    if (!guard)
    {
        return AcceptResult::Accepted;
    }
    _accepting = false;
    if (!_stopped && callbacks.completed)
    {
        callbacks.completed({});
    }
    return AcceptResult::Accepted;
}

void Tap::cancel(std::function<void()> quiescent)
{
    _stopped = true;
    _failure.clear();
    setOutputHandler({});
    setErrorHandler({});
    if (_quiescent)
    {
        if (quiescent)
        {
            quiescent();
        }
        return;
    }
    if (quiescent)
    {
        _cancelCallbacks.push_back(std::move(quiescent));
    }
    _cancelBranch();
}

void Tap::_schedule(int delay)
{
    if (_stopped || _scheduled || _busy || _queue.empty())
    {
        return;
    }
    _scheduled = true;
    QTimer::singleShot(delay, this, [this] {
        _scheduled = false;
        _pump();
    });
}

void Tap::_pump()
{
    if (_stopped || _busy || _queue.empty())
    {
        return;
    }
    auto input = std::move(_queue.front());
    _queue.pop_front();
    const auto retry         = input;
    _activeBytes             = input.bytes.size();
    _outputBytes             = 0;
    _outputChunks            = 0;
    _busy                    = true;
    const auto    generation = _generation;
    QPointer<Tap> guard(this);
    const auto    result = _branch->accept(std::move(input),
                                           {[guard, generation](Chunk output) {
                                             if (!guard || guard->_stopped || guard->_generation != generation)
                                             {
                                                 return;
                                             }
                                             if (output.port != 0
                                                 || output.bytes.size() > guard->_limits.maxBytes - guard->_outputBytes
                                                 || guard->_outputChunks
                                                        >= std::max(guard->_limits.maxBytes, guard->_limits.maxChunks))
                                             {
                                                 guard->_fail(QStringLiteral("Tap branch output limit exceeded"));
                                                 return;
                                             }
                                             guard->_outputBytes += output.bytes.size();
                                             ++guard->_outputChunks;
                                            },
                                            [guard, generation](QString error) {
                                             if (!guard || guard->_stopped || guard->_generation != generation)
                                             {
                                                 return;
                                             }
                                             guard->_busy          = false;
                                             guard->_pendingBytes -= guard->_activeBytes;
                                             guard->_activeBytes   = 0;
                                             if (!error.isEmpty())
                                             {
                                                 guard->_fail(QStringLiteral("Tap branch: ") + error.left(4096));
                                             }
                                             else
                                             {
                                                 guard->_schedule();
                                             }
                                            }});
    if (!guard || _stopped || _generation != generation)
    {
        return;
    }
    if (result == AcceptResult::WouldBlock)
    {
        _busy        = false;
        _activeBytes = 0;
        _queue.push_front(retry);
        _schedule(1);
    }
    else if (result == AcceptResult::Rejected)
    {
        _fail(QStringLiteral("Tap copy branch rejected a packet"));
    }
}

void Tap::_fail(QString error)
{
    if (_stopped)
    {
        return;
    }
    _stopped = true;
    _failure = std::move(error);
    _cancelBranch();
}

void Tap::_cancelBranch()
{
    if (_cancelling || _quiescent)
    {
        return;
    }
    _cancelling = true;
    ++_generation;
    _queue.clear();
    _pendingBytes = 0;
    _activeBytes  = 0;
    if (!_branch)
    {
        _cancelled();
        return;
    }
    _branch->setOutputHandler({});
    _branch->setErrorHandler({});
    QPointer<Tap> guard(this);
    _branch->cancel([guard] {
        if (guard)
        {
            guard->_cancelled();
        }
    });
}

void Tap::_cancelled()
{
    if (_quiescent)
    {
        return;
    }
    _quiescent     = true;
    _busy          = false;
    auto callbacks = std::move(_cancelCallbacks);
    auto error     = std::move(_failure);
    if (!error.isEmpty())
    {
        publishError(std::move(error));
    }
    for (auto& callback : callbacks)
    {
        if (callback)
        {
            callback();
        }
    }
}
