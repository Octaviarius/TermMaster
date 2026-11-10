#include "Nodes/Base64.h"
#include "Nodes/Bypass.h"
#include "Nodes/Checksum.h"
#include "Nodes/Compression.h"
#include "Nodes/Framer.h"
#include "Nodes/Gate.h"
#include "Nodes/Hex.h"
#include "Nodes/Length.h"
#include "Nodes/Pacer.h"
#include "Nodes/PatternFramer.h"
#include "Nodes/Stub.h"
#include "Nodes/Stuffing.h"
#include "Nodes/TimedFramer.h"
#include "Nodes/Timestamp.h"

#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorCatalog.h>

void registerNativeProcessors(ProcessorCatalog& catalog, const QString& owner)
{
    struct Definition
    {
        QString                         id;
        QString                         title;
        QString                         group;
        QByteArray                      schema;
        ProcessorCatalog::Type::Factory create;
        QString                         inputName;
        QString                         outputName;
        int                             channels = 1;
    };
    QList<Definition> definitions {
        Definition("native.bypass", "Bypass", "Control", Bypass::schema(), Bypass::create, "Data", "Data"),
        Definition("native.stub", "Stub", "Control", Stub::schema(), Stub::create, "Data", "Data"),
        Definition("native.gate", "Gate", "Control", Gate::schema(), Gate::create, "Data", "Data"),
        Definition("native.timestamp", "Timestamp", "Timing", Timestamp::schema(), Timestamp::create, "Data", "Data"),
        Definition("native.pacer", "Pacer", "Timing", Pacer::schema(), Pacer::create, "Data", "Data"),
        Definition("native.framer",
                   "Delimiter Framer",
                   "Framing",
                   Framer::schema(),
                   Framer::create,
                   "Stream",
                   "Frames"),
        Definition("native.timed-framer",
                   "Timed Framer",
                   "Framing",
                   TimedFramer::schema(),
                   TimedFramer::create,
                   "Stream",
                   "Frames"),
        Definition("native.pattern-framer",
                   "Pattern Framer",
                   "Framing",
                   PatternFramer::schema(),
                   PatternFramer::create,
                   "Stream",
                   "Frames"),

    };
    auto addFamily = [&](const Codec::Definition& family,
                         const QString&           group,
                         const QString&           directId,
                         const QString&           directTitle,
                         const QString&           inverseId,
                         const QString&           inverseTitle,
                         const QString&           codecId,
                         const QString&           codecTitle,
                         const QString&           wire,
                         const QString&           payload) {
        auto add = [&](Codec::Mode variant, const QString& id, const QString& title) {
            const bool direct = variant == Codec::Mode::Direct;
            definitions.append(
                {"native." + id,
                 title,
                 group,
                 family.schema(variant),
                 [family, variant](const QVariantMap& configuration, const Processor::Limits& limits, QString* error) {
                     return family.create(variant, configuration, limits, error);
                 },
                 direct ? payload : wire,
                 direct ? wire : payload,
                 variant == Codec::Mode::Duplex ? 2 : 1});
        };
        add(Codec::Mode::Direct, directId, directTitle);
        add(Codec::Mode::Inverse, inverseId, inverseTitle);
        add(Codec::Mode::Duplex, codecId, codecTitle);
    };
    addFamily(Hex::descriptor(),
              "Codecs",
              "hex-encoder",
              "HEX Encoder",
              "hex-decoder",
              "HEX Decoder",
              "hex-codec",
              "HEX Codec",
              "HEX",
              "Bytes");
    addFamily(Base64::descriptor(),
              "Codecs",
              "base64-encoder",
              "Base64 Encoder",
              "base64-decoder",
              "Base64 Decoder",
              "base64-codec",
              "Base64 Codec",
              "Base64",
              "Bytes");
    addFamily(Length::descriptor(),
              "Framing",
              "length-wrap",
              "Length Wrap",
              "length-unwrap",
              "Length Unwrap",
              "length-codec",
              "Length Codec",
              "Packet",
              "Payload");
    addFamily(Checksum::descriptor(),
              "Integrity",
              "checksum-wrap",
              "Checksum Wrap",
              "checksum-unwrap",
              "Checksum Unwrap",
              "checksum-codec",
              "Checksum Codec",
              "Packet",
              "Payload");
    addFamily(Stuffing::descriptor(),
              "Framing",
              "stuffing-wrap",
              "Stuffing Wrap",
              "stuffing-unwrap",
              "Stuffing Unwrap",
              "stuffing-codec",
              "Stuffing Codec",
              "Packet",
              "Payload");
    addFamily(Compression::descriptor(),
              "Compression",
              "compress",
              "Compress",
              "uncompress",
              "Uncompress",
              "compression-codec",
              "Compression Codec",
              "Compressed",
              "Bytes");
    QStringList registered;
    try
    {
        for (const auto& definition : definitions)
        {
            ProcessorCatalog::Type type;
            type.id             = definition.id;
            type.title          = definition.title;
            type.group          = definition.group;
            type.settingsSchema = definition.schema;
            type.create         = definition.create;
            type.inputNames     = {definition.inputName};
            type.outputNames    = {definition.outputName};
            type.inputCount     = definition.channels;
            type.outputCount    = definition.channels;
            if (definition.channels == 2)
            {
                type.inputNames.append(definition.outputName);
                type.outputNames.append(definition.inputName);
            }
            catalog.registerType(owner, std::move(type));
            registered.append(definition.id);
        }
    }
    catch (...)
    {
        for (const auto& id : registered)
        {
            catalog.unregisterType(owner, id);
        }
        throw;
    }
}
