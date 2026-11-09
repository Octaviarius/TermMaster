#pragma once
#include "SyncProcessorNode.h"

#include <memory>

/** Passes bytes and packet boundaries unchanged without configurable behavior. */
class Bypass final : public SyncProcessorNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    Bypass(QVariantMap configuration, Limits limits);
    void _process(const Chunk& input, QByteArray& output, QString& error) override;
    void _reset() override;
};
