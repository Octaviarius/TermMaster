#pragma once

#include "SyncProcessorNode.h"

#include <Termaster/Core/Formatting/CompiledFormat.h>
#include <optional>

/** Per-packet clocks and index supplied lazily to a prepared byte format. */
class Timestamp final : public SyncProcessorNode
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

private:
    FormatSchema                          _schema;
    FormatSchema::Slot<QString>           _datetime = _schema.add<QString>("datetime");
    FormatSchema::Slot<double>            _unix     = _schema.add<double>("unix");
    FormatSchema::Slot<qint64>            _unixMs   = _schema.add<qint64>("unix_ms");
    FormatSchema::Slot<qint64>            _unixUs   = _schema.add<qint64>("unix_us");
    FormatSchema::Slot<qint64>            _unixNs   = _schema.add<qint64>("unix_ns");
    FormatSchema::Slot<double>            _steady   = _schema.add<double>("steady");
    FormatSchema::Slot<qint64>            _steadyMs = _schema.add<qint64>("steady_ms");
    FormatSchema::Slot<qint64>            _steadyUs = _schema.add<qint64>("steady_us");
    FormatSchema::Slot<qint64>            _steadyNs = _schema.add<qint64>("steady_ns");
    FormatSchema::Slot<quint64>           _index    = _schema.add<quint64>("index");
    FormatSchema::Slot<QByteArrayView>    _packet   = _schema.add<QByteArrayView>("packet");
    std::shared_ptr<const CompiledFormat> _compiled;
    std::optional<FormatSchema::Values>   _values;
    QString                               _format;
    bool                                  _utc         = false;
    quint64                               _packetIndex = 0;

    Timestamp(QVariantMap configuration, Limits limits);
    static qint64 _bootNanoseconds(QString& error);
    void          _process(const Chunk& input, QByteArray& output, QString& error) override;
    void          _reset() override;
};
