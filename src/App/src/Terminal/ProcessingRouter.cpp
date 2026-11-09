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
    if (lane.inputs.size() + (lane.active ? 1 : 0) >= _limits.maxChunks
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
    lane.inputs.enqueue({std::move(chunk), options});
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
        auto callbacks    = std::move(current.ready);
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
                            lane.replacing                                                     ? State::Cancelling :
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

void ProcessingRouter::_schedule()
{
    if (!_retry.isActive())
    {
        _retry.start();
    }
}

void ProcessingRouter::_fail(Direction direction, QString message)
{
    message.truncate(4096);
    QPointer<ProcessingRouter> guard(this);
    replace(direction, {});
    if (!guard)
    {
        return;
    }
    _lane(direction).failed    = true;
    _lane(direction).lastError = message.left(4096);
    count(_lane(direction).failures, 1);
    emit error(direction, message);
}

void ProcessingRouter::_pump(Direction direction)
{
    auto& lane = _lane(direction);
    if (_pumping || lane.replacing || lane.failed)
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
                _fail(direction, message.isEmpty() ? QStringLiteral("Processing output was rejected") : message);
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
        lane.active           = true;
        lane.activeBytes      = item.chunk.bytes.size();
        lane.activeOptions    = item.options;
        lane.inputBytes      -= lane.inputs.dequeue().chunk.bytes.size();
        auto       processor  = lane.processor;
        const auto result     = processor->accept(item.chunk,
                                                  {[guard, direction, generation](Processor::Chunk output) {
                                                   if (!guard)
                                                   {
                                                       return;
                                                   }
                                                   auto& current = guard->_lane(direction);
                                                   if (current.generation != generation || !current.active)
                                                   {
                                                       return;
                                                   }
                                                   if (current.outputs.size() >= guard->_limits.maxChunks
                                                       || output.bytes.size()
                                                              > guard->_limits.maxBytes - current.outputBytes)
                                                   {
                                                       guard->_fail(direction,
                                                                    QStringLiteral(
                                                                        "Processing output exceeds queue limits"));
                                                       return;
                                                   }
                                                   current.outputBytes += output.bytes.size();
                                                   current.outputs.enqueue({std::move(output), current.activeOptions});
                                                   guard->_schedule();
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
                                                       guard->_fail(direction, std::move(message));
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
                _fail(direction, QStringLiteral("Processor rejected input"));
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
