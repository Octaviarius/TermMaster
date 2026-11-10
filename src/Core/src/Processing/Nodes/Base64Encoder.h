#pragma once
#include "SyncProcessorNode.h"

#include <array>
#include <memory>

/** Encodes each complete packet independently, including its tail and padding. */
class Base64Encoder final : public SyncProcessorNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    std::array<unsigned char, 3> _unit {};
    QByteArray                   _lineEnding;
    int                          _count      = 0;
    int                          _column     = 0;
    int                          _lineLength = 0;
    bool                         _url        = false;
    bool                         _padding    = true;
    Base64Encoder(QVariantMap configuration, Limits limits);
    void _process(const Chunk& input, QByteArray& output, QString& error) override;
    void _reset() override;
    bool _encoded(QByteArray& output, char byte, QString& error);
    bool _quantum(QByteArray& output, int count, QString& error);
};
