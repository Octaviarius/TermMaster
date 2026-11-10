#pragma once
#include "SyncProcessorNode.h"

#include <memory>

/** Encodes each packet independently with a literal separator between bytes. */
class HexEncoder final : public SyncProcessorNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    QByteArray _separator;
    bool       _lower    = false;
    bool       _haveByte = false;
    HexEncoder(QVariantMap configuration, Limits limits);
    void _process(const Chunk& input, QByteArray& output, QString& error) override;
    void _reset() override;
};
