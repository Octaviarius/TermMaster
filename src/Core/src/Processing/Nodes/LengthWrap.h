#pragma once

#include "LengthField.h"
#include "SyncProcessorNode.h"

#include <memory>

/** Adds an unsigned binary length field to each whole packet. */
class LengthWrap final : public SyncProcessorNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    LengthField _field;
    qsizetype   _maxSteps;

    LengthWrap(QVariantMap configuration, Limits limits);
    void _process(const Chunk& input, QByteArray& output, QString& error) override;
    void _reset() override;
};
