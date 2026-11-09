#include "Stub.h"

Stub::Stub(QVariantMap configuration, Limits limits) : SyncProcessorNode(std::move(configuration), limits)
{
}

QByteArray Stub::schema()
{
    return "version: 1\ntype: termaster.settings-schema\ndescription: Stub processor\ncontent:\n  type: object\n  properties: {}\n";
}

std::unique_ptr<Processor> Stub::create(const QVariantMap& configuration, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error))
    {
        return {};
    }
    return std::unique_ptr<Processor>(new Stub(configuration, limits));
}

void Stub::_process(const Chunk& input, QByteArray& output, QString& error)
{
    Q_UNUSED(input)
    Q_UNUSED(output)
    Q_UNUSED(error)
}

void Stub::_reset()
{
}

bool Stub::_emitEmptyPacket() const
{
    return false;
}
