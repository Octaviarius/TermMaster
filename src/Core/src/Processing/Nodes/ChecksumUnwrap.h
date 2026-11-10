#pragma once

#include "ChecksumField.h"
#include "SyncProcessorNode.h"

#include <memory>

/** Validates and removes a payload-only checksum field on each whole packet. */
class ChecksumUnwrap final : public SyncProcessorNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    ChecksumField _field;
    qsizetype     _maxSteps;

    ChecksumUnwrap(QVariantMap configuration, Limits limits);
    void _process(const Chunk& input, QByteArray& output, QString& error) override;
    void _reset() override;
};
