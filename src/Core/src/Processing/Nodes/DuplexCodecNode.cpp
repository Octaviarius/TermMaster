#include "DuplexCodecNode.h"

DuplexCodecNode::DuplexCodecNode(QVariantMap                configuration,
                                 QString                    encoding,
                                 std::unique_ptr<Processor> decoder,
                                 std::unique_ptr<Processor> encoder) :
    _configuration(std::move(configuration)), _encoding(std::move(encoding)),
    _codecs {std::move(decoder), std::move(encoder)}
{
    for (int port = 0; port < 2; ++port)
    {
        const auto prefix = port == 0 ? QStringLiteral("decode") : QStringLiteral("encode");
        const auto values = _codecs[port]->configuration();
        for (auto it = values.cbegin(); it != values.cend(); ++it)
        {
            auto key = it.key();
            key[0]   = key[0].toUpper();
            _configuration.insert(prefix + key, it.value());
        }
    }
}

QVariantMap DuplexCodecNode::configuration() const
{
    return _configuration;
}

int DuplexCodecNode::inputCount() const
{
    return 2;
}

int DuplexCodecNode::outputCount() const
{
    return 2;
}

QString DuplexCodecNode::inputName(int port) const
{
    return port == 0 ? _encoding : port == 1 ? QStringLiteral("Bytes") : QString {};
}

QString DuplexCodecNode::outputName(int port) const
{
    return port == 0 ? QStringLiteral("Bytes") : port == 1 ? _encoding : QString {};
}

Processor::AcceptResult DuplexCodecNode::accept(Chunk input, Callbacks callbacks)
{
    if (_stopped || !_failure.isEmpty() || input.port < 0 || input.port >= 2)
    {
        return AcceptResult::Rejected;
    }

    const int port = input.port;
    input.port     = 0;
    QPointer<DuplexCodecNode> guard(this);
    auto                      output    = std::move(callbacks.output);
    auto                      completed = std::move(callbacks.completed);
    return _codecs[port]->accept(std::move(input),
                                 {[guard, port, output = std::move(output)](Chunk chunk) {
                                      if (guard && !guard->_stopped && guard->_failure.isEmpty() && output)
                                      {
                                          chunk.port = port;
                                          output(std::move(chunk));
                                      }
                                  },
                                  [guard, completed = std::move(completed)](QString error) {
                                      if (!guard || guard->_stopped)
                                      {
                                          return;
                                      }
                                      if (!error.isEmpty() && guard->_failure.isEmpty())
                                      {
                                          // Preserve completions already in flight on the peer port.
                                          // Only explicit cancellation suppresses accepted callbacks.
                                          guard->_failure = error;
                                      }
                                      if (completed)
                                      {
                                          completed(guard->_failure.isEmpty() ? std::move(error) : guard->_failure);
                                      }
                                  }});
}

void DuplexCodecNode::cancel(std::function<void()> quiescent)
{
    _stop();
    if (quiescent)
    {
        quiescent();
    }
}

QVariantMap DuplexCodecNode::_directionConfiguration(const QVariantMap& configuration, const QString& prefix)
{
    QVariantMap result;
    for (auto it = configuration.cbegin(); it != configuration.cend(); ++it)
    {
        if (it.key().startsWith(prefix) && it.key().size() > prefix.size())
        {
            auto key = it.key().mid(prefix.size());
            key[0]   = key[0].toLower();
            result.insert(key, it.value());
        }
    }
    return result;
}

void DuplexCodecNode::_stop()
{
    _stopped = true;
    for (const auto& codec : _codecs)
    {
        codec->cancel({});
    }
}
