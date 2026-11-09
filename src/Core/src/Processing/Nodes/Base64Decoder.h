#pragma once
#include "SyncProcessorNode.h"

#include <array>
#include <memory>

/** Strict canonical Base64 decoding. Unpadded tails require an explicit packet. */
class Base64Decoder final : public SyncProcessorNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    std::array<unsigned char, 4> _unit {};
    int                          _count            = 0;
    int                          _paddingNeeded    = 0;
    bool                         _url              = false;
    bool                         _ignoreWhitespace = true;
    bool                         _requirePadding   = true;
    bool                         _ended            = false;
    Base64Decoder(QVariantMap configuration, Limits limits);
    void _process(const Chunk& input, QByteArray& output, QString& error) override;
    void _reset() override;
    bool _decoded(QByteArray& output, int count, QString& error);
};
