#include "Nodes/Base64Codec.h"
#include "Nodes/Base64Decoder.h"
#include "Nodes/Base64Encoder.h"
#include "Nodes/Bypass.h"
#include "Nodes/Framer.h"
#include "Nodes/Gate.h"
#include "Nodes/HexCodec.h"
#include "Nodes/HexDecoder.h"
#include "Nodes/HexEncoder.h"
#include "Nodes/Pacer.h"
#include "Nodes/Stub.h"
#include "Nodes/TimedFramer.h"

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
    const QList<Definition> definitions {
        Definition("native.hex-codec", "HEX Codec", "Codecs", HexCodec::schema(), HexCodec::create, "HEX", "Bytes", 2),
        Definition("native.base64-codec",
                   "Base64 Codec",
                   "Codecs",
                   Base64Codec::schema(),
                   Base64Codec::create,
                   "Base64",
                   "Bytes",
                   2),
        Definition("native.bypass", "Bypass", "Control", Bypass::schema(), Bypass::create, "Data", "Data"),
        Definition("native.stub", "Stub", "Control", Stub::schema(), Stub::create, "Data", "Data"),
        Definition("native.gate", "Gate", "Control", Gate::schema(), Gate::create, "Data", "Data"),
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
        Definition("native.hex-encoder",
                   "HEX Encoder",
                   "Codecs",
                   HexEncoder::schema(),
                   HexEncoder::create,
                   "Bytes",
                   "HEX"),
        Definition("native.hex-decoder",
                   "HEX Decoder",
                   "Codecs",
                   HexDecoder::schema(),
                   HexDecoder::create,
                   "HEX",
                   "Bytes"),
        Definition("native.base64-encoder",
                   "Base64 Encoder",
                   "Codecs",
                   Base64Encoder::schema(),
                   Base64Encoder::create,
                   "Bytes",
                   "Base64"),
        Definition("native.base64-decoder",
                   "Base64 Decoder",
                   "Codecs",
                   Base64Decoder::schema(),
                   Base64Decoder::create,
                   "Base64",
                   "Bytes"),
    };
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
