#pragma once

#include "DuplexCodecNode.h"

/** Full-duplex Base64 decoding and encoding with independent stream state. */
class Base64Codec final : public DuplexCodecNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    Base64Codec(QVariantMap configuration, std::unique_ptr<Processor> decoder, std::unique_ptr<Processor> encoder);
};
