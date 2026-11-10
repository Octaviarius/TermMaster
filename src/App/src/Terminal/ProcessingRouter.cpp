#include "ProcessingRouter.h"

#include <QPointer>
#include <algorithm>
#include <limits>

namespace
{
void count(quint64& value, quint64 amount)
{
    value += std::min(amount, std::numeric_limits<quint64>::max() - value);
}
} // namespace

ProcessingRouter::ProcessingRouter(QObject* parent) : ProcessingRouter(Limits {}, parent)
{
}

ProcessingRouter::ProcessingRouter(Limits limits, QObject* parent) : QObject(parent), _limits(limits)
{
    _limits.maxBytes    = std::max<qsizetype>(1, _limits.maxBytes);
    _limits.maxChunks   = std::max<qsizetype>(1, _limits.maxChunks);
    _limits.eventBudget = std::max(1, _limits.eventBudget);
    _retry.setSingleShot(true);
    _retry.setInterval(std::max(1, _limits.retryMs));
    connect(&_retry, &QTimer::timeout, this, [this] {
        _pump(Direction::Rx);
        _pump(Direction::Tx);
    });
}

ProcessingRouter::~ProcessingRouter()
{
    ++_pairGeneration;
    if (_duplex)
    {
        _lanes[1].processor.reset();
    }

    if (_pairProcessors[0])
    {
        _retire(_pairProcessors[0], {});
    }
    if (_pairProcessors[1] && _pairProcessors[1] != _pairProcessors[0])
    {
        _retire(_pairProcessors[1], {});
    }
    _pairProcessors = {};

    for (auto& lane : _lanes)
    {
        ++lane.generation;
        _retire(std::move(lane.processor), {});
        _retire(std::move(lane.replacement), {});
    }
}

ProcessingRouter::Lane& ProcessingRouter::_lane(Direction direction)
{
    return _lanes[direction == Direction::Rx ? 0 : 1];
}

const ProcessingRouter::Lane& ProcessingRouter::_lane(Direction direction) const
{
    return _lanes[direction == Direction::Rx ? 0 : 1];
}

void ProcessingRouter::setOutput(Direction direction, Output output)
{
    _lane(direction).output = std::move(output);
    _schedule();
}

Processor::AcceptResult ProcessingRouter::submit(Direction                         direction,
                                                 Processor::Chunk                  chunk,
                                                 const TerminalSendQueue::Options& options,
                                                 QString*                          error)
{
    auto& lane = _lane(direction);
    if (lane.failed)
    {
        if (error)
        {
            *error = QStringLiteral("Processing direction is unavailable");
        }
        return Processor::AcceptResult::Rejected;
    }
    if (chunk.bytes.size() > _limits.maxBytes)
    {
        if (error)
        {
            *error = QStringLiteral("Processing input exceeds byte limit");
        }
        return Processor::AcceptResult::Rejected;
    }
    constexpr qsizetype maximumStreamChunkBytes = 1024;
    const qsizetype     pieces = chunk.boundary == Processor::Boundary::StreamChunk && !chunk.bytes.isEmpty() ?
                                     1 + (chunk.bytes.size() - 1) / maximumStreamChunkBytes :
                                     1;
    if (pieces > _limits.maxChunks)
    {
        if (error)
        {
            *error = QStringLiteral("Processing input exceeds chunk limit");
        }
        return Processor::AcceptResult::Rejected;
    }
    if (pieces > _limits.maxChunks - lane.inputs.size() - (lane.active ? 1 : 0)
        || chunk.bytes.size() > _limits.maxBytes - lane.inputBytes - lane.activeBytes)
    {
        if (error)
        {
            *error = QStringLiteral("Processing input queue is full");
        }
        return Processor::AcceptResult::WouldBlock;
    }
    count(lane.acceptedBytes, quint64(chunk.bytes.size()));
    lane.inputBytes += chunk.bytes.size();
    if (chunk.boundary == Processor::Boundary::Packet || chunk.bytes.isEmpty())
    {
        lane.inputs.enqueue({
            {QByteArray(chunk.bytes.constData(), chunk.bytes.size()), chunk.boundary},
            options
        });
    }
    else
    {
        for (qsizetype offset = 0; offset < chunk.bytes.size(); offset += maximumStreamChunkBytes)
        {
            const qsizetype length = qMin(maximumStreamChunkBytes, chunk.bytes.size() - offset);
            lane.inputs.enqueue({
                {QByteArray(chunk.bytes.constData() + offset, length), chunk.boundary},
                options
            });
        }
    }
    _schedule();
    return Processor::AcceptResult::Accepted;
}

void ProcessingRouter::_retire(std::shared_ptr<Processor> processor, std::function<void()> ready)
{
    if (!processor)
    {
        if (ready)
        {
            ready();
        }
        return;
    }
    // The holder survives a deferred acknowledgement, including router destruction.
    auto holder = std::make_shared<std::shared_ptr<Processor>>(std::move(processor));
    (*holder)->cancel([holder, ready = std::move(ready)] {
        // Never destroy plugin code while its cancellation frame is still executing.
        QTimer::singleShot(0, [holder, ready] {
            holder->reset();
            if (ready)
            {
                ready();
            }
        });
    });
}

void ProcessingRouter::_clear(Lane& lane)
{
    lane.inputs.clear();
    lane.outputs.clear();
    lane.inputBytes  = 0;
    lane.outputBytes = 0;
    lane.activeBytes = 0;
    lane.active      = false;
}

void ProcessingRouter::replace(Direction direction, std::shared_ptr<Processor> processor, std::function<void()> ready)
{
    if (processor && (processor->inputCount() != 1 || processor->outputCount() != 1 || processor->thread() != thread()))
    {
        emit error(direction, QStringLiteral("An independent direction requires a 1x1 processor"));
        return;
    }

    const auto otherIndex = direction == Direction::Rx ? 1 : 0;
    const auto ownIndex   = direction == Direction::Rx ? 0 : 1;
    if (processor
        && (processor == _lanes[size_t(otherIndex)].processor || processor == _lanes[size_t(otherIndex)].replacement
            || processor == _lanes[size_t(ownIndex)].processor
            || (_pairReplacing && processor == _pairProcessors[size_t(otherIndex)])))
    {
        emit error(direction, QStringLiteral("Independent processing requires distinct fresh instances"));
        return;
    }

    if (_duplex || _pairReplacing)
    {
        auto desired = _pairReplacing && !_pairShared ? _pairProcessors : std::array<std::shared_ptr<Processor>, 2> {};
        desired[direction == Direction::Rx ? 0 : 1] = std::move(processor);
        _replacePair(std::move(desired), false, std::move(ready));
        return;
    }

    _replaceLane(direction, std::move(processor), std::move(ready));
}

bool ProcessingRouter::duplex() const
{
    return _duplex;
}

void ProcessingRouter::replaceDuplex(std::shared_ptr<Processor> processor, std::function<void()> ready)
{
    if (processor
        && (processor->thread() != thread() || processor->inputCount() != 2 || processor->outputCount() != 2
            || processor == _lanes[0].processor || processor == _lanes[1].processor))
    {
        emit error(Direction::Rx,
                   QStringLiteral("Shared processing requires a fresh 2x2 instance on the router thread"));
        return;
    }

    _replacePair({processor, processor}, bool(processor), std::move(ready));
}

void ProcessingRouter::_replacePair(std::array<std::shared_ptr<Processor>, 2> processors,
                                    bool                                      shared,
                                    std::function<void()>                     ready)
{
    if (ready)
    {
        _pairReady.append(std::move(ready));
    }
    // Prepared instances may own timers even before their first accepted invocation.
    auto abandoned  = std::move(_pairProcessors);
    _pairProcessors = std::move(processors);
    _pairShared     = shared;
    for (size_t index = 0; index < abandoned.size(); ++index)
    {
        if (abandoned[index] && abandoned[index] != _pairProcessors[0] && abandoned[index] != _pairProcessors[1]
            && (index == 0 || abandoned[index] != abandoned[0]))
        {
            _retire(abandoned[index], {});
        }
    }

    if (_pairReplacing)
    {
        for (auto& lane : _lanes)
        {
            ++lane.generation;
            _clear(lane);
            lane.failed = false;
            lane.lastError.clear();
        }
        return;
    }

    const auto epoch = ++_pairGeneration;
    _pairReplacing   = true;
    if (_duplex)
    {
        _lanes[1].processor.reset();
    }
    _duplex = false;
    for (auto& lane : _lanes)
    {
        _pairReady.append(std::move(lane.ready));
        lane.ready.clear();
    }

    auto                       remaining = std::make_shared<int>(2);
    QPointer<ProcessingRouter> guard(this);
    auto                       install = [guard, epoch, remaining] {
        if (!guard || guard->_pairGeneration != epoch || --*remaining != 0)
        {
            return;
        }

        guard->_duplex             = guard->_pairShared;
        guard->_lanes[0].processor = std::move(guard->_pairProcessors[0]);
        guard->_lanes[1].processor = std::move(guard->_pairProcessors[1]);
        guard->_pairReplacing      = false;
        guard->_installHandlers(Direction::Rx);
        if (!guard->_duplex)
        {
            guard->_installHandlers(Direction::Tx);
        }
        auto callbacks = std::move(guard->_pairReady);
        for (auto& callback : callbacks)
        {
            callback();
            if (!guard)
            {
                return;
            }
        }
        emit guard->capacityAvailable(Direction::Rx);
        if (guard)
        {
            emit guard->capacityAvailable(Direction::Tx);
        }
        if (guard)
        {
            guard->_schedule();
        }
    };
    _replaceLane(Direction::Rx, {}, install);
    if (guard && guard->_pairGeneration == epoch)
    {
        _replaceLane(Direction::Tx, {}, install);
    }
}

void ProcessingRouter::_installHandlers(Direction direction)
{
    auto& lane = _lane(direction);
    if (!lane.processor)
    {
        return;
    }
    QPointer<ProcessingRouter> guard(this);
    const auto                 generation = lane.generation;
    lane.processor->setOutputHandler([guard, direction, generation](Processor::Chunk chunk) {
        if (guard)
        {
            guard->_output(direction, generation, std::move(chunk), {}, false);
        }
    });
    lane.processor->setErrorHandler([guard, direction, generation](QString message) {
        if (guard && guard->_lane(direction).generation == generation)
        {
            guard->fail(direction, std::move(message));
        }
    });
}

void ProcessingRouter::_output(Direction                         source,
                               quint64                           generation,
                               Processor::Chunk                  chunk,
                               const TerminalSendQueue::Options& options,
                               bool                              requireActive)
{
    const auto& origin = _lane(source);
    if (_pairReplacing || origin.generation != generation || origin.failed || origin.replacing
        || (requireActive && !origin.active))
    {
        return;
    }
    if (chunk.port < 0 || chunk.port >= (_duplex ? 2 : 1))
    {
        fail(source, QStringLiteral("Processor emitted an invalid output port"));
        return;
    }

    const auto direction = _duplex ? (chunk.port == 0 ? Direction::Rx : Direction::Tx) : source;
    auto&      target    = _lane(direction);
    if (target.outputs.size() >= _limits.maxChunks || chunk.bytes.size() > _limits.maxBytes - target.outputBytes)
    {
        fail(direction, QStringLiteral("Processing output exceeds queue limits"));
        return;
    }

    chunk.port          = 0;
    chunk.bytes         = QByteArray(chunk.bytes.constData(), chunk.bytes.size());
    target.outputBytes += chunk.bytes.size();
    target.outputs.enqueue({std::move(chunk), direction == source ? options : TerminalSendQueue::Options {}});
    _schedule();
}

void ProcessingRouter::_replaceLane(Direction                  direction,
                                    std::shared_ptr<Processor> processor,
                                    std::function<void()>      ready)
{
    auto&       lane  = _lane(direction);
    const auto& other = _lane(direction == Direction::Rx ? Direction::Tx : Direction::Rx);
    if (processor
        && (processor->thread() != thread() || processor == other.processor || processor == other.replacement
            || processor == lane.processor))
    {
        emit error(direction, QStringLiteral("Processing instances must be independent and use the router thread"));
        return;
    }
    ++lane.generation;
    _clear(lane);
    lane.failed = false;
    lane.lastError.clear();
    if (lane.replacement && lane.replacement != processor)
    {
        _retire(std::move(lane.replacement), {});
    }
    lane.replacement = std::move(processor);
    if (ready)
    {
        lane.ready.append(std::move(ready));
    }
    if (lane.replacing)
    {
        return;
    }

    lane.replacing = true;
    QPointer<ProcessingRouter> guard(this);
    _retire(std::move(lane.processor), [guard, direction] {
        if (!guard)
        {
            return;
        }
        auto& current     = guard->_lane(direction);
        current.processor = std::move(current.replacement);
        current.replacing = false;
        guard->_installHandlers(direction);
        auto callbacks = std::move(current.ready);
        emit guard->capacityAvailable(direction);
        if (!guard)
        {
            return;
        }
        for (auto& callback : callbacks)
        {
            callback();
            if (!guard)
            {
                return;
            }
        }
        guard->_schedule();
    });
}

void ProcessingRouter::cancel(Direction direction, std::function<void()> ready)
{
    replace(direction, {}, std::move(ready));
}

void ProcessingRouter::wake(Direction direction)
{
    _pump(direction);
}

ProcessingRouter::Diagnostics ProcessingRouter::diagnostics(Direction direction) const
{
    const auto& lane = _lane(direction);
    Diagnostics result;
    result.state          = lane.failed                                                        ? State::Failed :
                            (lane.replacing || _pairReplacing)                                 ? State::Cancelling :
                            (lane.active || !lane.inputs.isEmpty() || !lane.outputs.isEmpty()) ? State::Busy :
                            lane.processor                                                     ? State::Idle :
                                                                                                 State::Bypass;
    result.pendingBytes   = pendingBytes(direction);
    result.pendingChunks  = pendingSubmissions(direction);
    result.acceptedBytes  = lane.acceptedBytes;
    result.forwardedBytes = lane.forwardedBytes;
    result.failures       = lane.failures;
    result.lastError      = lane.lastError;
    return result;
}

qsizetype ProcessingRouter::pendingBytes(Direction direction) const
{
    const auto& lane = _lane(direction);
    return lane.inputBytes + lane.outputBytes + lane.activeBytes;
}

qsizetype ProcessingRouter::pendingSubmissions(Direction direction) const
{
    const auto& lane = _lane(direction);
    return lane.inputs.size() + lane.outputs.size() + (lane.active ? 1 : 0);
}

void ProcessingRouter::fail(Direction direction, QString message)
{
    message.truncate(4096);
    QPointer<ProcessingRouter> guard(this);
    const bool                 shared = _duplex;
    replace(direction, {});
    if (!guard)
    {
        return;
    }
    _lane(direction).failed    = true;
    _lane(direction).lastError = message.left(4096);
    count(_lane(direction).failures, 1);
    if (shared)
    {
        auto& other     = _lane(direction == Direction::Rx ? Direction::Tx : Direction::Rx);
        other.failed    = true;
        other.lastError = message;
        count(other.failures, 1);
    }
    emit error(direction, message);
}

void ProcessingRouter::_schedule()
{
    if (!_retry.isActive())
    {
        _retry.start();
    }
}

void ProcessingRouter::_pump(Direction direction)
{
    auto& lane = _lane(direction);
    if (_pumping || _pairReplacing || lane.replacing || lane.failed)
    {
        return;
    }
    _pumping = true;
    QPointer<ProcessingRouter> guard(this);
    int                        budget = _limits.eventBudget;
    while (budget-- > 0 && !lane.failed && !lane.replacing)
    {
        const auto generation = lane.generation;
        if (!lane.outputs.isEmpty())
        {
            if (!lane.output)
            {
                break;
            }
            const auto item = lane.outputs.head();
            QString    message;
            const auto result = lane.output(item.chunk, item.options, &message);
            if (!guard)
            {
                return;
            }
            if (lane.generation != generation)
            {
                break;
            }
            if (result == Processor::AcceptResult::WouldBlock)
            {
                break;
            }
            if (result == Processor::AcceptResult::Rejected)
            {
                fail(direction, message.isEmpty() ? QStringLiteral("Processing output was rejected") : message);
                if (!guard)
                {
                    return;
                }
                break;
            }
            count(lane.forwardedBytes, quint64(item.chunk.bytes.size()));
            lane.outputBytes -= lane.outputs.dequeue().chunk.bytes.size();
            emit capacityAvailable(direction);
            if (!guard)
            {
                return;
            }
            continue;
        }
        if (lane.active || lane.inputs.isEmpty())
        {
            break;
        }
        auto item = lane.inputs.head();
        if (!lane.processor)
        {
            lane.inputBytes  -= lane.inputs.dequeue().chunk.bytes.size();
            lane.outputBytes += item.chunk.bytes.size();
            lane.outputs.enqueue(std::move(item));
            continue;
        }
        lane.active         = true;
        lane.activeBytes    = item.chunk.bytes.size();
        lane.activeOptions  = item.options;
        lane.inputBytes    -= lane.inputs.dequeue().chunk.bytes.size();
        auto processor      = lane.processor;
        item.chunk.port     = _duplex && direction == Direction::Tx ? 1 : 0;
        const auto result   = processor->accept(item.chunk,
                                                {[guard, direction, generation](Processor::Chunk output) {
                                                   if (guard)
                                                   {
                                                       guard->_output(direction,
                                                                      generation,
                                                                      std::move(output),
                                                                      guard->_lane(direction).activeOptions,
                                                                      true);
                                                   }
                                                 },
                                                 [guard, direction, generation](QString message) {
                                                   if (!guard)
                                                   {
                                                       return;
                                                   }
                                                   auto& current = guard->_lane(direction);
                                                   if (current.generation != generation || !current.active)
                                                   {
                                                       return;
                                                   }
                                                   current.active      = false;
                                                   current.activeBytes = 0;
                                                   if (!message.isEmpty())
                                                   {
                                                       guard->fail(direction, std::move(message));
                                                       return;
                                                   }
                                                   emit guard->capacityAvailable(direction);
                                                   if (guard)
                                                   {
                                                       guard->_schedule();
                                                   }
                                                 }});
        if (!guard)
        {
            return;
        }
        if (lane.generation != generation)
        {
            break;
        }
        if (result != Processor::AcceptResult::Accepted)
        {
            lane.active      = false;
            lane.activeBytes = 0;
            if (result == Processor::AcceptResult::Rejected)
            {
                fail(direction, QStringLiteral("Processor rejected input"));
                if (!guard)
                {
                    return;
                }
                break;
            }
            lane.inputBytes += item.chunk.bytes.size();
            lane.inputs.prepend(std::move(item));
            break;
        }
    }
    _pumping = false;
    if (!lane.failed && !lane.replacing && (!lane.outputs.isEmpty() || (!lane.active && !lane.inputs.isEmpty())))
    {
        _schedule();
    }
}
