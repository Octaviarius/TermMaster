#pragma once

#include "SyncProcessorNode.h"

#include <memory>

/** Compresses each whole packet as an independent zlib, gzip or raw DEFLATE stream. */
class Compress final : public SyncProcessorNode
{
public:
    ~Compress() override;

    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    struct Data;

    std::unique_ptr<Data> _data;
    qsizetype             _maxSteps;

    Compress(QVariantMap configuration, Limits limits);
    void _process(const Chunk& input, QByteArray& output, QString& error) override;
    void _reset() override;
};
