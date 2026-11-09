#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <Termaster/Core/Processing/Lineflow.h>
#include <Termaster/Core/Processing/ProcessorBlocks.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>

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
                                            Limits                                  limits)
{
    Prepared prepared;
    prepared._configuration = std::move(configuration);
    prepared._limits        = limits;
    auto result             = std::unique_ptr<Lineflow>(new Lineflow(prepared));
    result->_nodes          = std::move(nodes);
    return result;
}

QVariantMap Lineflow::Prepared::configuration() const
{
    return _configuration;
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
        runtime->_nodes.push_back(std::move(processor));
    }
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
        || limits.eventBudget <= 0)
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

    QSet<QString>                                            visiting;
    QString                                                  problem;
    qsizetype                                                visitedNodes = 0;
    std::function<bool(const QString&, int, const QString&)> expand =
        [&](const QString& key, int depth, const QString& prefix) {
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
                if (node.contains(QStringLiteral("reference")))
                {
                    if (node.contains(QStringLiteral("type")) || node.contains(QStringLiteral("configuration"))
                        || node.value(QStringLiteral("reference")).metaType().id() != QMetaType::QString)
                    {
                        problem = QStringLiteral(
                            "Reference nodes require a block reference without processor configuration");
                        return false;
                    }
                    if (!expand(node.value(QStringLiteral("reference")).toString(), depth + 1, path))
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
                                              ownedValue(configuration).toMap()});
                }
            }
            visiting.remove(key);
            return true;
        };
    if (!expand(id, 1, QString {}))
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

Lineflow::Lineflow(const Prepared& prepared) : _configuration(prepared._configuration), _limits(prepared._limits)
{
}

Lineflow::~Lineflow() = default;
QVariantMap Lineflow::configuration() const
{
    return _configuration;
}

Processor::AcceptResult Lineflow::accept(Chunk input, Callbacks callbacks)
{
    if (_stopped || input.bytes.size() > _limits.maxBytes)
    {
        return AcceptResult::Rejected;
    }
    if (_active)
    {
        return AcceptResult::WouldBlock;
    }
    _active       = true;
    _callbacks    = std::move(callbacks);
    _queuedBytes  = input.bytes.size();
    _outputBytes  = 0;
    _outputChunks = 0;
    _queue.push_back({0, std::move(input)});
    _schedule();
    return AcceptResult::Accepted;
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
    if (!_active || _pending || _stopped)
    {
        return;
    }

    int budget = _limits.eventBudget;
    while (!_queue.empty() && !_pending && !_stopped && budget-- > 0)
    {
        auto work = std::move(_queue.front());
        _queue.pop_front();
        _queuedBytes -= work.chunk.bytes.size();
        if (work.stage == _nodes.size())
        {
            QPointer<Lineflow> guard(this);
            auto               output = _callbacks.output;
            if (output)
            {
                output(std::move(work.chunk));
            }
            if (!guard)
            {
                return;
            }
            continue;
        }

        _pending                      = true;
        _childBytes                   = 0;
        _childChunks                  = 0;
        const auto         generation = _generation;
        const auto         invocation = ++_invocation;
        const auto         retryInput = work.chunk;
        QPointer<Lineflow> guard(this);
        Callbacks          callbacks;
        callbacks.output = [guard, generation, invocation, next = work.stage + 1](Chunk chunk) {
            if (!guard || guard->_stopped || guard->_generation != generation || guard->_invocation != invocation
                || !guard->_pending)
            {
                return;
            }

            const bool final = next == guard->_nodes.size();
            const auto bytes = chunk.bytes.size();
            if (guard->_childChunks >= guard->_limits.maxChunks || bytes > guard->_limits.maxBytes - guard->_childBytes
                || bytes > guard->_limits.maxBytes - guard->_queuedBytes
                || guard->_queue.size() >= size_t(guard->_limits.maxChunks)
                || (final
                    && (guard->_outputChunks >= guard->_limits.maxChunks
                        || bytes > guard->_limits.maxBytes - guard->_outputBytes)))
            {
                guard->_fail(QStringLiteral("Lineflow output or queue limit exceeded"));
                return;
            }

            ++guard->_childChunks;
            guard->_childBytes += bytes;
            if (final)
            {
                ++guard->_outputChunks;
                guard->_outputBytes += bytes;
            }
            guard->_queuedBytes += bytes;
            chunk.bytes          = QByteArray(chunk.bytes.constData(), chunk.bytes.size());
            guard->_queue.push_back({next, std::move(chunk)});
        };
        callbacks.completed = [guard, generation, invocation](QString error) {
            if (!guard || guard->_stopped || guard->_generation != generation || guard->_invocation != invocation
                || !guard->_pending)
            {
                return;
            }

            guard->_pending = false;
            if (!error.isEmpty())
            {
                guard->_fail(QString(error.constData(), error.size()));
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
        if (!_stopped && status == AcceptResult::WouldBlock)
        {
            _pending      = false;
            _queuedBytes += retryInput.bytes.size();
            _queue.push_front({work.stage, retryInput});
            if (!_scheduled)
            {
                _scheduled = true;
                QTimer::singleShot(1, this, [this] {
                    _scheduled = false;
                    _pump();
                });
            }
            return;
        }
        if (!_stopped && status == AcceptResult::Rejected)
        {
            _pending = false;
            _fail(QStringLiteral("Lineflow child rejected input"));
        }
    }

    if (_stopped || _pending)
    {
        return;
    }
    if (_queue.empty())
    {
        _finish({});
    }
    else
    {
        _schedule();
    }
}

void Lineflow::_finish(QString error)
{
    _active        = false;
    auto completed = std::move(_callbacks.completed);
    _callbacks     = {};
    if (completed)
    {
        completed(std::move(error));
    }
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
    _callbacks = {};
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
    _quiescent     = true;
    _pending       = false;
    _active        = false;
    auto completed = std::move(_callbacks.completed);
    _callbacks     = {};
    auto callbacks = std::move(_cancelCallbacks);
    auto failure   = _failure;
    if (completed && !failure.isEmpty())
    {
        completed(std::move(failure));
    }
    for (auto& callback : callbacks)
    {
        callback();
    }
}
