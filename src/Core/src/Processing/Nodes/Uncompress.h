#pragma once

#include "SyncProcessorNode.h"

#include <memory>

/** Expands complete independent packets with bounded output and strict stream validation. */
class Uncompress final : public SyncProcessorNode
{
public:
    ~Uncompress() override;

    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    struct Data;

    std::unique_ptr<Data> _data;
    qsizetype             _maxSteps;
    qsizetype             _maxOutputBytes;
    bool                  _concatenated;

    Uncompress(QVariantMap configuration, Limits limits);
    void _process(const Chunk& input, QByteArray& output, QString& error) override;
    void _reset() override;
};
