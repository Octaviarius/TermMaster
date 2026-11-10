#pragma once

#include "StuffingSettings.h"
#include "SyncProcessorNode.h"

#include <memory>

/** Encodes one independent packet with fixed two-byte escape pairs. */
class StuffingWrap final : public SyncProcessorNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    StuffingSettings _settings;
    qsizetype        _maxSteps;

    StuffingWrap(QVariantMap configuration, Limits limits);
    void _process(const Chunk& input, QByteArray& output, QString& error) override;
    void _reset() override;
};
