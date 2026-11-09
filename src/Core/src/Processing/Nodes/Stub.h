#pragma once
#include "SyncProcessorNode.h"

#include <memory>

/** Discards all input, including empty packets, while completing each invocation. */
class Stub final : public SyncProcessorNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    Stub(QVariantMap configuration, Limits limits);
    void _process(const Chunk& input, QByteArray& output, QString& error) override;
    void _reset() override;
    bool _emitEmptyPacket() const override;
};
