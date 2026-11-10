#pragma once

#include "DuplexCodecNode.h"

/** Full-duplex HEX decoding and encoding with independent stream state. */
class HexCodec final : public DuplexCodecNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    HexCodec(QVariantMap configuration, std::unique_ptr<Processor> decoder, std::unique_ptr<Processor> encoder);
};
