#include "PrefixProcessor.h"

#include <QThread>

PrefixProcessor::PrefixProcessor(QVariantMap configuration, Limits limits) :
    _configuration(std::move(configuration)), _prefix(_configuration.value("prefix").toString().toUtf8()),
    _limits(limits)
{
}

QVariantMap PrefixProcessor::configuration() const
{
    return _configuration;
}

Processor::AcceptResult PrefixProcessor::accept(Chunk input, Callbacks callbacks)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (_cancelled || input.port != 0 || _limits.maxChunks < 1 || _limits.maxBytes < _prefix.size()
        || input.bytes.size() > _limits.maxBytes - _prefix.size())
    {
        return AcceptResult::Rejected;
    }
    if (_busy)
    {
        return AcceptResult::WouldBlock;
    }

    _busy = true;
    // append() copies arbitrary bytes, including NUL. Even empty input is one packet.
    QByteArray bytes;
    bytes.reserve(_prefix.size() + input.bytes.size());
    bytes.append(_prefix);
    bytes.append(input.bytes);
    if (callbacks.output)
    {
        callbacks.output({std::move(bytes), 0});
    }
    // Output callbacks may reenter cancel(). Suppress completion in that case.
    if (!_cancelled && callbacks.completed)
    {
        callbacks.completed({});
    }

    _busy                 = false;
    auto acknowledgements = std::move(_quiescent);
    // Nothing touches this instance after acknowledgement: the caller may release it.
    for (auto& acknowledge : acknowledgements)
    {
        acknowledge();
    }
    return AcceptResult::Accepted;
}

void PrefixProcessor::cancel(std::function<void()> quiescent)
{
    Q_ASSERT(QThread::currentThread() == thread());
    _cancelled = true;
    setOutputHandler({});
    setErrorHandler({});

    if (!quiescent)
    {
        return;
    }
    if (_busy)
    {
        _quiescent.append(std::move(quiescent));
        return;
    }
    quiescent();
}
