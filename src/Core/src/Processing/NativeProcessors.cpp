#include "Nodes/Base64Decoder.h"
#include "Nodes/Base64Encoder.h"
#include "Nodes/Framer.h"
#include "Nodes/Gate.h"
#include "Nodes/HexDecoder.h"
#include "Nodes/HexEncoder.h"

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
    };
    const QList<Definition> definitions {
        {          "native.gate",           "Gate", "Control",          Gate::schema(),          Gate::create},
        {        "native.framer",         "Framer", "Framing",        Framer::schema(),        Framer::create},
        {   "native.hex-encoder",    "HEX Encoder",  "Codecs",    HexEncoder::schema(),    HexEncoder::create},
        {   "native.hex-decoder",    "HEX Decoder",  "Codecs",    HexDecoder::schema(),    HexDecoder::create},
        {"native.base64-encoder", "Base64 Encoder",  "Codecs", Base64Encoder::schema(), Base64Encoder::create},
        {"native.base64-decoder", "Base64 Decoder",  "Codecs", Base64Decoder::schema(), Base64Decoder::create}
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
