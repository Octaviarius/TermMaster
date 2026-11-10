#pragma once
#include "SyncProcessorNode.h"

#include <memory>

/** Packet hexadecimal decoding with optional explicit nibble accumulation. */
class HexDecoder final : public SyncProcessorNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    int  _nibble           = -1;
    bool _ignoreWhitespace = true;
    bool _accumulate       = false;
    HexDecoder(QVariantMap configuration, Limits limits);
    void _process(const Chunk& input, QByteArray& output, QString& error) override;
    void _reset() override;
    bool _retainPacketState() const override;
    bool _emitEmptyPacket() const override;
};
