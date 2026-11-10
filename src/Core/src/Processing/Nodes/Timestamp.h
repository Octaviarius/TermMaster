#pragma once

#include "SyncProcessorNode.h"

/** Adds one formatted wall-clock timestamp to each packet without decoding its bytes. */
class Timestamp final : public SyncProcessorNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    struct Part
    {
        enum class Kind
        {
            Literal,
            Timestamp,
            Packet
        };
        Kind       kind = Kind::Literal;
        QByteArray literal;
    };

    QString     _format;
    QList<Part> _parts;
    bool        _utc = false;

    Timestamp(QVariantMap configuration, Limits limits, QList<Part> parts);
    static bool _parseTemplate(const QString& text, QList<Part>& parts, QString* error);
    void        _process(const Chunk& input, QByteArray& output, QString& error) override;
    void        _reset() override;
};
