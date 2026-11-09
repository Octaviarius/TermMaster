#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <Termaster/Core/Processing/Lineflow.h>
#include <Termaster/Core/Processing/ProcessorBlocks.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <algorithm>

namespace
{
QVariant ownedValue(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::QString)
    {
        const auto text = value.toString();
        return QString(text.constData(), text.size());
    }
    if (value.metaType().id() == QMetaType::QVariantMap)
    {
        QVariantMap result;
        const auto  map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
        {
            result.insert(QString(it.key().constData(), it.key().size()), ownedValue(it.value()));
        }
        return result;
    }
    if (value.metaType().id() == QMetaType::QVariantList)
    {
        QVariantList result;
        for (const auto& item : value.toList())
        {
            result.push_back(ownedValue(item));
        }
        return result;
    }
    return value;
}
} // namespace

std::unique_ptr<Lineflow> Lineflow::compose(std::vector<std::shared_ptr<Processor>> nodes,
                                            QVariantMap                             configuration,
                                            Limits                                  limits,
                                            QStringList                             nodeNames,
                                            int                                     channels,
                                            std::vector<int>                        lanes)
{
    Prepared prepared;
    prepared._configuration = std::move(configuration);
    prepared._limits        = limits;
    prepared._channels      = channels;
    auto result             = std::unique_ptr<Lineflow>(new Lineflow(prepared));
    result->_nodes          = std::move(nodes);
    result->_nodeNames      = std::move(nodeNames);
    result->_lanes          = lanes.empty() ? std::vector<int>(result->_nodes.size(), 0) : std::move(lanes);
    result->_connectOutputs();
    return result;
}

QVariantMap Lineflow::Prepared::configuration() const
{
    return _configuration;
}

int Lineflow::Prepared::inputCount() const
{
    return _channels;
}
int Lineflow::Prepared::outputCount() const
{
    return _channels;
}

std::unique_ptr<Lineflow> Lineflow::Prepared::instantiate(ProcessorRegistry& registry, QString* error) const
{
    return instantiate(registry, QVariantMap {}, error);
}

std::unique_ptr<Lineflow> Lineflow::Prepared::instantiate(ProcessorRegistry& registry,
                                                          const QVariantMap& overrides,
                                                          QString*           error) const
{
    QSet<QString> paths;
    for (const auto& node : _nodes)
    {
        paths.insert(node.path);
    }
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
    {
        if (!paths.contains(it.key()) || it.value().metaType().id() != QMetaType::QVariantMap)
        {
            if (error)
            {
                *error = QStringLiteral("Unknown processor occurrence or invalid override map: ") + it.key();
            }
            return {};
        }
    }

    QVariantMap              effectiveNodes;
    std::vector<QVariantMap> configurations;
    for (const auto& node : _nodes)
    {
        auto configuration = registry.resolveConfiguration(node.type,
                                                           node.configuration,
                                                           overrides.value(node.path).toMap(),
                                                           error);
        if (!configuration)
        {
            return {};
        }
        configurations.push_back(ownedValue(*configuration).toMap());
        effectiveNodes.insert(node.path, configurations.back());
    }

    auto runtime = std::unique_ptr<Lineflow>(new Lineflow(*this));
    runtime->_configuration.insert(QStringLiteral("overrides"), ownedValue(overrides));
    runtime->_configuration.insert(QStringLiteral("effectiveNodes"), effectiveNodes);
    for (size_t index = 0; index < _nodes.size(); ++index)
    {
        auto processor = registry.create(_nodes[index].type, configurations[index], _limits, error);
        if (!processor)
        {
            return {};
        }
        const int ports = _nodes[index].lane == 2 ? 2 : 1;
        if (processor->inputCount() != ports || processor->outputCount() != ports)
        {
            if (error)
            {
                *error = QStringLiteral("Lineflow processor port shape changed after preparation");
            }
            return {};
        }
        runtime->_nodes.push_back(std::move(processor));
        runtime->_lanes.push_back(_nodes[index].lane);
    }
    runtime->_connectOutputs();
    return runtime;
}

std::shared_ptr<const Lineflow::Prepared> Lineflow::prepare(const QVariantMap& blocks,
                                                            const QString&     id,
                                                            ProcessorRegistry& registry,
                                                            Limits             limits,
                                                            QString*           error)
{
    auto fail = [error](QString message) {
        if (error)
        {
            *error = std::move(message);
        }
    };
    if (limits.maxBytes <= 0 || limits.maxChunks <= 0 || limits.maxDepth <= 0 || limits.maxInstances <= 0
        || limits.eventBudget <= 0 || limits.maxSteps <= 0)
    {
        fail(QStringLiteral("Invalid processing limits"));
        return {};
    }
    auto result                                       = std::make_shared<Prepared>();
    result->_limits                                   = limits;
    // Restrict persisted input to JSON value types; reject QObject and custom mutable payloads.
    qsizetype                                 values  = 0;
    std::function<bool(const QVariant&, int)> bounded = [&](const QVariant& value, int depth) {
        if (++values > limits.maxBytes || qint64(depth) > qint64(limits.maxDepth) + 8)
        {
            return false;
        }
        auto type = value.metaType().id();
        if (type == QMetaType::QVariantMap)
        {
            const auto map = value.toMap();
            for (auto it = map.begin(); it != map.end(); ++it)
            {
                if (!bounded(it.value(), depth + 1))
                {
                    return false;
                }
            }
            return true;
        }
        if (type == QMetaType::QVariantList)
        {
            for (const auto& item : value.toList())
            {
                if (!bounded(item, depth + 1))
                {
                    return false;
                }
            }
            return true;
        }
        return !value.isValid() || type == QMetaType::QString || type == QMetaType::Bool || type == QMetaType::Int
            || type == QMetaType::UInt || type == QMetaType::LongLong || type == QMetaType::ULongLong
            || type == QMetaType::Double;
    };
    if (!bounded(blocks, 0)
        || QJsonDocument::fromVariant(blocks).toJson(QJsonDocument::Compact).size() > limits.maxBytes)
    {
        fail(QStringLiteral("Lineflow configuration exceeds bounds or contains unsupported values"));
        return {};
    }
    if (!ProcessorBlocks::validate(blocks, error))
    {
        return {};
    }

    result->_channels = blocks.value(id).toMap().value("channels", 1).toInt();
    QSet<QString>                                                 visiting;
    QString                                                       problem;
    qsizetype                                                     visitedNodes = 0;
    std::function<bool(const QString&, int, const QString&, int)> expand =
        [&](const QString& key, int depth, const QString& prefix, int placement) {
            if (depth > limits.maxDepth)
            {
                problem = QStringLiteral("Lineflow reference depth exceeded");
                return false;
            }
            if (visiting.contains(key))
            {
                problem = QStringLiteral("Lineflow reference cycle: ") + key;
                return false;
            }
            if (!blocks.contains(key) || blocks.value(key).metaType().id() != QMetaType::QVariantMap)
            {
                problem = QStringLiteral("Missing lineflow block: ") + key;
                return false;
            }
            const auto block = blocks.value(key).toMap();
            if (block.value(QStringLiteral("id")).toString() != key
                || block.value(QStringLiteral("nodes")).metaType().id() != QMetaType::QVariantList)
            {
                problem = QStringLiteral("Invalid lineflow block: ") + key;
                return false;
            }
            const int  channels = block.value("channels", 1).toInt();
            const auto start    = result->_nodes.size();
            visiting.insert(key);
            for (const auto& value : block.value(QStringLiteral("nodes")).toList())
            {
                if (value.metaType().id() != QMetaType::QVariantMap)
                {
                    problem = QStringLiteral("Invalid lineflow node");
                    return false;
                }
                if (++visitedNodes > limits.maxInstances)
                {
                    problem = QStringLiteral("Lineflow expanded node limit exceeded");
                    return false;
                }
                const auto node     = value.toMap();
                const auto nodeName = node.value(QStringLiteral("name")).toString();
                const auto path     = prefix.isEmpty() ? nodeName : prefix + QLatin1Char('/') + nodeName;
                const auto laneName = node.value("lane", "rx").toString();
                const int  lane     = laneName == "duplex" ? 2 : laneName == "tx" ? 1 : 0;
                if (node.contains(QStringLiteral("reference")))
                {
                    if (node.contains(QStringLiteral("type")) || node.contains(QStringLiteral("configuration"))
                        || node.value(QStringLiteral("reference")).metaType().id() != QMetaType::QString)
                    {
                        problem = QStringLiteral(
                            "Reference nodes require a block reference without processor configuration");
                        return false;
                    }
                    const auto target        = node.value("reference").toString();
                    const int  childChannels = blocks.value(target).toMap().value("channels", 1).toInt();
                    if ((childChannels == 2) != (lane == 2) || (channels == 1 && childChannels != 1))
                    {
                        problem = QStringLiteral("Lineflow reference shape does not match its lane");
                        return false;
                    }
                    if (!expand(target, depth + 1, path, lane))
                    {
                        return false;
                    }
                }
                else
                {
                    const auto type = node.value(QStringLiteral("type")).toString();
                    if (node.contains(QStringLiteral("configuration"))
                        && node.value(QStringLiteral("configuration")).metaType().id() != QMetaType::QVariantMap)
                    {
                        problem = QStringLiteral("Invalid processor configuration");
                        return false;
                    }
                    const auto metadata = registry.type(type);
                    if (!metadata || (metadata->inputCount != metadata->outputCount)
                        || (metadata->inputCount != 1 && metadata->inputCount != 2)
                        || ((metadata->inputCount == 2) != (lane == 2)) || (channels == 1 && metadata->inputCount != 1))
                    {
                        problem = QStringLiteral("Lineflow node port shape does not match its lane");
                        return false;
                    }
                    const auto configuration = node.value(QStringLiteral("configuration")).toMap();
                    if (!registry.validateConfiguration(type, configuration, &problem))
                    {
                        return false;
                    }
                    if (result->_nodes.size() >= size_t(limits.maxInstances))
                    {
                        problem = QStringLiteral("Lineflow instance limit exceeded");
                        return false;
                    }
                    result->_nodes.push_back({QString(path.constData(), path.size()),
                                              QString(type.constData(), type.size()),
                                              ownedValue(configuration).toMap(),
                                              lane});
                }
            }
            if (channels == 1 && placement >= 0)
            {
                if (placement == 1)
                {
                    std::reverse(result->_nodes.begin() + qsizetype(start), result->_nodes.end());
                }
                for (size_t index = start; index < result->_nodes.size(); ++index)
                {
                    result->_nodes[index].lane = placement;
                }
            }
            visiting.remove(key);
            return true;
        };
    if (!expand(id, 1, QString {}, -1))
    {
        fail(problem);
        return {};
    }
    result->_configuration = {
        {    QStringLiteral("id"),     id},
        {QStringLiteral("blocks"), blocks}
    };
    result->_configuration = ownedValue(result->_configuration).toMap();
    return result;
}

Lineflow::Lineflow(const Prepared& prepared) :
    _configuration(prepared._configuration), _limits(prepared._limits), _roots(size_t(prepared._channels)),
    _channels(prepared._channels)
{
    for (const auto& node : prepared._nodes)
    {
        _nodeNames.append(node.path);
    }
}

Lineflow::~Lineflow() = default;
QVariantMap Lineflow::configuration() const
{
    return _configuration;
}
int Lineflow::inputCount() const
{
    return _channels;
}
int Lineflow::outputCount() const
{
    return _channels;
}

Processor::AcceptResult Lineflow::accept(Chunk input, Callbacks callbacks)
{
    if (_stopped || input.port < 0 || input.port >= _channels || input.bytes.size() > _limits.maxBytes)
    {
        return AcceptResult::Rejected;
    }
    auto& root = _roots[size_t(input.port)];
    if (root.active || _queue.size() >= size_t(_limits.maxChunks)
        || input.bytes.size() > _limits.maxBytes - _queuedBytes)
    {
        return AcceptResult::WouldBlock;
    }
    const auto origin  = input.port;
    root.active        = true;
    root.callbacks     = std::move(callbacks);
    root.outputBytes   = 0;
    root.outputChunks  = 0;
    input.bytes        = QByteArray(input.bytes.constData(), input.bytes.size());
    const auto first   = _nextStage(origin == 0 ? -1 : int(_nodes.size()), origin);
    input.port         = first == _nodes.size() || _lanes[first] == 2 ? origin : 0;
    _queuedBytes      += input.bytes.size();
    _queue.push_back({first, std::move(input), origin, std::make_shared<Budget>(Budget {_limits.maxSteps})});
    _schedule();
    return AcceptResult::Accepted;
}

size_t Lineflow::_nextStage(int from, int direction) const
{
    const int step = direction == 0 ? 1 : -1;
    for (int index = from + step; index >= 0 && index < int(_nodes.size()); index += step)
    {
        if (_lanes[size_t(index)] == direction || _lanes[size_t(index)] == 2)
        {
            return size_t(index);
        }
    }
    return _nodes.size();
}

void Lineflow::_connectOutputs()
{
    _stages.resize(_nodes.size());
    QPointer<Lineflow> guard(this);
    for (size_t index = 0; index < _nodes.size(); ++index)
    {
        _nodes[index]->setOutputHandler([guard, index](Chunk chunk) {
            if (guard && !guard->_stopped)
            {
                guard->_enqueueOutput(index,
                                      std::move(chunk),
                                      -1,
                                      -1,
                                      std::make_shared<Budget>(Budget {guard->_limits.maxSteps}));
            }
        });
        _nodes[index]->setErrorHandler([guard, index](QString error) {
            if (guard && !guard->_stopped)
            {
                guard->_fail(guard->_nodeError(index, error.left(4096)));
            }
        });
    }
}

void Lineflow::_enqueueOutput(size_t index, Chunk chunk, int origin, int inputPort, std::shared_ptr<Budget> budget)
{
    if (chunk.port < 0 || chunk.port >= _nodes[index]->outputCount())
    {
        _fail(_nodeError(index, QStringLiteral("Invalid output port")));
        return;
    }
    const int   direction     = _lanes[index] == 2 ? chunk.port : _lanes[index];
    const auto  next          = _nextStage(int(index), direction);
    const auto  bytes         = chunk.bytes.size();
    const auto* invocation    = inputPort < 0 ? nullptr : &_stages[index].inputs[size_t(inputPort)];
    const auto  emissionLimit = std::max(_limits.maxBytes, _limits.maxChunks);
    const bool  final         = next == _nodes.size();
    if ((invocation && (invocation->chunks >= emissionLimit || bytes > _limits.maxBytes - invocation->bytes))
        || bytes > _limits.maxBytes - _queuedBytes || _queue.size() >= size_t(_limits.maxChunks)
        || (origin >= 0 && final
            && (_roots[size_t(origin)].outputChunks >= emissionLimit
                || bytes > _limits.maxBytes - _roots[size_t(origin)].outputBytes)))
    {
        _fail(_nodeError(index, QStringLiteral("Lineflow output or queue limit exceeded")));
        return;
    }
    if (invocation)
    {
        ++_stages[index].inputs[size_t(inputPort)].chunks;
        _stages[index].inputs[size_t(inputPort)].bytes += bytes;
    }
    if (origin >= 0 && final)
    {
        ++_roots[size_t(origin)].outputChunks;
        _roots[size_t(origin)].outputBytes += bytes;
    }
    chunk.port    = final || _lanes[next] == 2 ? direction : 0;
    chunk.bytes   = QByteArray(chunk.bytes.constData(), chunk.bytes.size());
    _queuedBytes += bytes;
    _queue.push_back({next, std::move(chunk), origin, std::move(budget)});
    _schedule();
}

void Lineflow::_schedule()
{
    if (_scheduled || _stopped)
    {
        return;
    }
    _scheduled = true;
    QTimer::singleShot(0, this, [this] {
        _scheduled = false;
        _pump();
    });
}

void Lineflow::_pump()
{
    if (_pumping || _stopped)
    {
        return;
    }
    _pumping = true;
    QPointer<Lineflow>               guard(this);
    std::vector<std::array<bool, 2>> blocked(_nodes.size());
    bool                             retry  = false;
    int                              budget = _limits.eventBudget;
    while (!_stopped && budget-- > 0)
    {
        auto position = std::find_if(_queue.begin(), _queue.end(), [&](const Work& work) {
            return work.stage == _nodes.size()
                || (!_stages[work.stage].inputs[size_t(work.chunk.port)].pending
                    && !blocked[work.stage][size_t(work.chunk.port)]);
        });
        if (position == _queue.end())
        {
            break;
        }
        const auto positionIndex = std::distance(_queue.begin(), position);
        auto       work          = std::move(*position);
        _queue.erase(position);
        _queuedBytes -= work.chunk.bytes.size();
        if (work.budget->remaining <= 0)
        {
            _fail(QStringLiteral("Lineflow processing step limit exceeded"));
            if (!guard)
            {
                return;
            }
            break;
        }
        --work.budget->remaining;
        if (work.stage == _nodes.size())
        {
            if (work.root < 0)
            {
                publishOutput(std::move(work.chunk));
            }
            else
            {
                auto output = _roots[size_t(work.root)].callbacks.output;
                if (output)
                {
                    output(std::move(work.chunk));
                }
            }
            if (!guard)
            {
                return;
            }
            continue;
        }

        const int inputPort   = work.chunk.port;
        auto&     stage       = _stages[work.stage].inputs[size_t(inputPort)];
        stage.pending         = true;
        stage.root            = work.root;
        stage.budget          = work.budget;
        stage.bytes           = 0;
        stage.chunks          = 0;
        const auto generation = _generation;
        const auto invocation = ++stage.invocation;
        const auto retryInput = work.chunk;
        Callbacks  callbacks;
        callbacks.output = [guard,
                            generation,
                            invocation,
                            stageIndex = work.stage,
                            origin     = work.root,
                            inputPort,
                            budget = work.budget](Chunk chunk) {
            if (!guard || guard->_stopped || guard->_generation != generation)
            {
                return;
            }
            const auto& stage = guard->_stages[stageIndex].inputs[size_t(inputPort)];
            if (!stage.pending || stage.invocation != invocation)
            {
                return;
            }
            guard->_enqueueOutput(stageIndex, std::move(chunk), origin, inputPort, budget);
        };
        callbacks.completed = [guard, generation, invocation, stageIndex = work.stage, inputPort](QString error) {
            if (!guard || guard->_stopped || guard->_generation != generation)
            {
                return;
            }
            auto& stage = guard->_stages[stageIndex].inputs[size_t(inputPort)];
            if (!stage.pending || stage.invocation != invocation)
            {
                return;
            }
            stage.pending = false;
            if (!error.isEmpty())
            {
                guard->_fail(guard->_nodeError(stageIndex, QString(error.constData(), error.size())));
            }
            else
            {
                guard->_schedule();
            }
        };
        const auto status = _nodes[work.stage]->accept(std::move(work.chunk), std::move(callbacks));
        if (!guard)
        {
            return;
        }
        if (_stopped || _generation != generation)
        {
            break;
        }
        if (status == AcceptResult::WouldBlock)
        {
            stage.pending  = false;
            _queuedBytes  += retryInput.bytes.size();
            ++work.budget->remaining;
            _queue.insert(_queue.begin() + positionIndex, {work.stage, retryInput, work.root, work.budget});
            blocked[work.stage][size_t(inputPort)] = true;
            retry                                  = true;
        }
        else if (status == AcceptResult::Rejected)
        {
            stage.pending = false;
            _fail(_nodeError(work.stage, QStringLiteral("Lineflow child rejected input")));
            if (!guard)
            {
                return;
            }
        }
    }
    _pumping = false;
    if (_stopped)
    {
        return;
    }
    for (int root = 0; root < _channels; ++root)
    {
        if (_roots[size_t(root)].active
            && std::none_of(_queue.begin(), _queue.end(), [root](const Work& work) { return work.root == root; })
            && std::none_of(_stages.begin(), _stages.end(), [root](const Stage& stage) {
                   return std::any_of(stage.inputs.begin(), stage.inputs.end(), [root](const Invocation& input) {
                       return input.pending && input.root == root;
                   });
               }))
        {
            _finish(root);
            if (!guard || guard->_stopped)
            {
                return;
            }
        }
    }
    if (retry && !_scheduled)
    {
        _scheduled = true;
        QTimer::singleShot(1, this, [this] {
            _scheduled = false;
            _pump();
        });
    }
    else if (std::any_of(_queue.begin(), _queue.end(), [&](const Work& work) {
                 return work.stage == _nodes.size() || !_stages[work.stage].inputs[size_t(work.chunk.port)].pending;
             }))
    {
        _schedule();
    }
}

void Lineflow::_finish(int index)
{
    auto& root     = _roots[size_t(index)];
    root.active    = false;
    auto completed = std::move(root.callbacks.completed);
    root.callbacks = {};
    if (completed)
    {
        completed({});
    }
}

QString Lineflow::_nodeError(size_t stage, const QString& error) const
{
    const auto    path   = stage < size_t(_nodeNames.size()) ? _nodeNames[qsizetype(stage)] : QString::number(stage);
    const QString prefix = QStringLiteral("Processing node [");
    if (error.startsWith(prefix) && error.indexOf(QStringLiteral("]: "), prefix.size()) > prefix.size())
    {
        return prefix + path + '/' + error.mid(prefix.size());
    }
    return prefix + path + QStringLiteral("]: ") + error;
}

void Lineflow::_fail(QString error)
{
    _failure = std::move(error);
    _stopped = true;
    _cancelNodes();
}

void Lineflow::cancel(std::function<void()> quiescent)
{
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
    setOutputHandler({});
    setErrorHandler({});
    for (auto& root : _roots)
    {
        root.callbacks = {};
    }
    if (_stopped)
    {
        return;
    }
    _stopped = true;
    _cancelNodes();
}

void Lineflow::_cancelNodes()
{
    ++_generation;
    _queue.clear();
    _queuedBytes = 0;
    _cancelling  = _nodes.size();
    if (_cancelling == 0)
    {
        _cancelled();
        return;
    }
    QPointer<Lineflow> guard(this);
    for (auto& node : _nodes)
    {
        node->setOutputHandler({});
        node->setErrorHandler({});
        auto once = std::make_shared<bool>(false);
        node->cancel([guard, once] {
            if (*once)
            {
                return;
            }
            *once = true;
            if (guard && --guard->_cancelling == 0)
            {
                guard->_cancelled();
            }
        });
        if (!guard)
        {
            return;
        }
    }
}

void Lineflow::_cancelled()
{
    _quiescent = true;
    for (auto& stage : _stages)
    {
        for (auto& input : stage.inputs)
        {
            input.pending = false;
        }
    }
    std::vector<std::function<void(QString)>> completions;
    for (auto& root : _roots)
    {
        root.active = false;
        if (root.callbacks.completed)
        {
            completions.push_back(std::move(root.callbacks.completed));
        }
        root.callbacks = {};
    }
    auto               callbacks = std::move(_cancelCallbacks);
    auto               failure   = _failure;
    QPointer<Lineflow> guard(this);
    if (!failure.isEmpty())
    {
        if (completions.empty())
        {
            publishError(failure);
        }
        for (auto& completed : completions)
        {
            if (!guard)
            {
                break;
            }
            completed(failure);
        }
    }
    for (auto& callback : callbacks)
    {
        callback();
    }
}
