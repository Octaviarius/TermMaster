#include "Bypass.h"

#include "Codec.h"

Bypass::Bypass(QVariantMap configuration, Limits limits) : SyncProcessorNode(std::move(configuration), limits)
{
}

QByteArray Bypass::schema()
{
    return "version: 1\ntype: termaster.settings-schema\ndescription: Bypass processor\ncontent:\n  type: object\n  properties: {}\n";
}

std::unique_ptr<Processor> Bypass::create(const QVariantMap& configuration, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error))
    {
        return {};
    }
    return std::unique_ptr<Processor>(new Bypass(configuration, limits));
}

std::unique_ptr<Processor> Bypass::createDuplex(const QVariantMap& configuration, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error))
    {
        return {};
    }

    auto receive  = create(configuration, limits, error);
    auto transmit = create(configuration, limits, error);
    return std::make_unique<Codec>(configuration, "Wire", "Bytes", std::move(receive), std::move(transmit));
}

void Bypass::_process(const Chunk& input, QByteArray& output, QString& error)
{
    output = QByteArray(input.bytes.constData(), input.bytes.size());
    Q_UNUSED(error)
}

void Bypass::_reset()
{
}
