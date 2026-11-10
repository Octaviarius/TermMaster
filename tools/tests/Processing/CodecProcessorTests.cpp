#include "../../../src/Core/src/Processing/Nodes/Base64Decoder.h"
#include "../../../src/Core/src/Processing/Nodes/Base64Encoder.h"
#include "../../../src/Core/src/Processing/Nodes/HexDecoder.h"
#include "../../../src/Core/src/Processing/Nodes/HexEncoder.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QtTest>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>

namespace
{

struct Result
{
    Processor::AcceptResult accepted = Processor::AcceptResult::Rejected;
    QList<Processor::Chunk> outputs;
    QString                 error;
    int                     completed = 0;
    QByteArray              bytes() const
    {
        QByteArray result;
        for (const auto& output : outputs)
        {
            result += output.bytes;
        }
        return result;
    }
};
Result invoke(Processor& processor, QByteArray bytes, int port = 0)
{
    auto result      = std::make_shared<Result>();
    result->accepted = processor.accept({std::move(bytes), port},
                                        {[result](auto output) { result->outputs.append(std::move(output)); },
                                         [result](QString error) {
                                             result->error = std::move(error);
                                             ++result->completed;
                                         }});
    if (result->accepted == Processor::AcceptResult::Accepted)
    {
        QElapsedTimer timer;
        timer.start();
        while (result->completed == 0 && timer.elapsed() < 1000)
        {
            QCoreApplication::processEvents();
        }
        if (result->completed == 0)
        {
            result->error = QStringLiteral("Timed out waiting for processor completion");
        }
    }
    return *result;
}
QByteArray allBytes()
{
    QByteArray bytes;
    for (int value = 0; value < 256; ++value)
    {
        bytes.append(char(value));
    }
    return bytes;
}
QVariantMap node(const QString& name, const QString& type, QVariantMap configuration = {})
{
    return {
        {         "name",          name},
        {         "type",          type},
        {"configuration", configuration}
    };
}
} // namespace
class CodecProcessorTests final : public QObject
{
    Q_OBJECT
private slots:
    void unaryNodesRejectInvalidPorts()
    {
        for (const auto& id : QStringList {"native.bypass",
                                           "native.stub",
                                           "native.gate",
                                           "native.framer",
                                           "native.pacer",
                                           "native.hex-encoder",
                                           "native.hex-decoder",
                                           "native.base64-encoder",
                                           "native.base64-decoder"})
        {
            ProcessorRegistry registry;
            registerNativeProcessors(registry, "native");
            auto runtime = registry.create(id, {}, {});
            QVERIFY(runtime);
            QCOMPARE(invoke(*runtime, {}, -1).accepted, Processor::AcceptResult::Rejected);
            QCOMPARE(invoke(*runtime, {}, 1).accepted, Processor::AcceptResult::Rejected);
            QCOMPARE(invoke(*runtime, {}).accepted, Processor::AcceptResult::Accepted);
        }
    }
    void hexEverySplitAndPacketReset()
    {
        const auto bytes = allBytes();
        for (int split = 0; split <= bytes.size(); ++split)
        {
            auto encoder = HexEncoder::create(
                {
                    {"separator", ""}
            },
                {});
            QCOMPARE(invoke(*encoder, bytes.left(split)).bytes(), bytes.left(split).toHex().toUpper());
            QCOMPARE(invoke(*encoder, bytes.mid(split)).bytes(), bytes.mid(split).toHex().toUpper());
        }
        auto encoder = HexEncoder::create({}, {});
        QCOMPARE(invoke(*encoder, "ab").bytes(), QByteArray("61 62"));
        QCOMPARE(invoke(*encoder, "c").bytes(), QByteArray("63"));
        const auto encoded = bytes.toHex();
        for (int split = 0; split <= encoded.size(); ++split)
        {
            auto decoder = HexDecoder::create(
                {
                    {"accumulate", true}
            },
                {});
            const auto first  = invoke(*decoder, encoded.left(split));
            const auto second = invoke(*decoder, encoded.mid(split));
            QVERIFY(first.error.isEmpty() && second.error.isEmpty());
            QCOMPARE(first.bytes() + second.bytes(), bytes);
        }
    }

    void hexWhitespaceTailsAndMalformed()
    {
        auto decoder = HexDecoder::create(
            {
                {"accumulate", true}
        },
            {});
        QVERIFY(invoke(*decoder, "A").error.isEmpty());
        QVERIFY(invoke(*decoder, " \t\r\n\v\f").error.isEmpty());
        QCOMPARE(invoke(*decoder, "b").bytes(), QByteArray::fromHex("ab"));
        auto strict = HexDecoder::create(
            {
                {"ignoreWhitespace", false}
        },
            {});
        QVERIFY(!invoke(*strict, "00 11").error.isEmpty());
        for (const auto& bytes : QList<QByteArray> {"0", "001x", QByteArray("00\0", 3), QByteArray("0\xA0", 2)})
        {
            auto       instance = HexDecoder::create({}, {});
            const auto result   = invoke(*instance, bytes);
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.outputs.isEmpty());
            QCOMPARE(result.completed, 1);
            QCOMPARE(invoke(*instance, "00").accepted, Processor::AcceptResult::Rejected);
        }
        auto accumulating = HexDecoder::create(
            {
                {"accumulate", true}
        },
            {});
        QVERIFY(invoke(*accumulating, "0").error.isEmpty());
        QVERIFY(!invoke(*accumulating, "x").error.isEmpty());
        QVERIFY(!HexDecoder::create(
            {
                {"accumulate", "true"}
        },
            {}));
    }

    void base64EverySplitAllBytesAndBothAlphabets()
    {
        const auto bytes = allBytes();
        for (const auto& alphabet : QStringList {"standard", "url"})
        {
            for (bool padding : {true, false})
            {
                QByteArray::Base64Options flags = alphabet == "url" ? QByteArray::Base64UrlEncoding :
                                                                      QByteArray::Base64Encoding;
                if (!padding)
                {
                    flags |= QByteArray::OmitTrailingEquals;
                }
                for (int split = 0; split <= bytes.size(); ++split)
                {
                    auto encoder = Base64Encoder::create(
                        {
                            {"alphabet", alphabet},
                            { "padding",  padding}
                    },
                        {});
                    auto decoder = Base64Decoder::create(
                        {
                            {      "alphabet", alphabet},
                            {"requirePadding",  padding}
                    },
                        {});
                    const auto first  = invoke(*encoder, bytes.left(split));
                    const auto second = invoke(*encoder, bytes.mid(split));
                    QCOMPARE(first.bytes(), bytes.left(split).toBase64(flags));
                    QCOMPARE(second.bytes(), bytes.mid(split).toBase64(flags));
                    QCOMPARE(invoke(*decoder, first.bytes()).bytes(), bytes.left(split));
                    QCOMPARE(invoke(*decoder, second.bytes()).bytes(), bytes.mid(split));
                }
            }
        }
    }

    void base64StreamTailFinalizationAndWrapping()
    {
        auto encoder = Base64Encoder::create({}, {});
        QCOMPARE(invoke(*encoder, "f").bytes(), QByteArray("Zg=="));
        QCOMPARE(invoke(*encoder, "o").bytes(), QByteArray("bw=="));
        QCOMPARE(invoke(*encoder, {}).outputs.size(), 1);
        QCOMPARE(invoke(*encoder, "fo").bytes(), QByteArray("Zm8="));
        for (const auto& ending : QStringList {"lf", "crlf"})
        {
            auto wrapped = Base64Encoder::create(
                {
                    {"lineLength",      4},
                    {"lineEnding", ending}
            },
                {});
            const auto newline = ending == "lf" ? QByteArray("\n") : QByteArray("\r\n");
            QCOMPARE(invoke(*wrapped, "foobar").bytes(), QByteArray("Zm9v") + newline + "YmFy");
            QCOMPARE(invoke(*wrapped, "bar").bytes(), QByteArray("YmFy"));
        }
        auto decoder = Base64Decoder::create({}, {});
        QVERIFY(!invoke(*decoder, "Zg=").error.isEmpty());
        QCOMPARE(invoke(*decoder, "=").accepted, Processor::AcceptResult::Rejected);
        auto optional = Base64Decoder::create(
            {
                {"requirePadding", false}
        },
            {});
        QCOMPARE(invoke(*optional, "Zm8").bytes(), QByteArray("fo"));
        QCOMPARE(invoke(*optional, "Zg").bytes(), QByteArray("f"));
    }

    void base64StrictMalformedAndCanonicalBits()
    {
        const QList<QByteArray> malformed {"A",
                                           "Zg",
                                           "Zm8",
                                           "=AAA",
                                           "A===",
                                           "Z===",
                                           "Zg=",
                                           "Zg===",
                                           "Zg==A",
                                           "Zg==Zm8=",
                                           "Zh==",
                                           "Zm9=",
                                           "Zm9v!",
                                           QByteArray("Zm9v\0", 5),
                                           "----",
                                           "____"};
        for (const auto& bytes : malformed)
        {
            auto       decoder = Base64Decoder::create({}, {});
            const auto result  = invoke(*decoder, bytes);
            QVERIFY2(!result.error.isEmpty(), bytes.constData());
            QVERIFY(result.outputs.isEmpty());
            QCOMPARE(result.completed, 1);
        }
        for (const auto& bytes : QList<QByteArray> {"A", "Zh", "Zm9", "Zg="})
        {
            auto decoder = Base64Decoder::create(
                {
                    {"requirePadding", false}
            },
                {});
            const auto result = invoke(*decoder, bytes);
            QVERIFY2(!result.error.isEmpty(), bytes.constData());
            QVERIFY(result.outputs.isEmpty());
        }
        auto strict = Base64Decoder::create(
            {
                {"ignoreWhitespace", false}
        },
            {});
        QVERIFY(!invoke(*strict, "Zg==\n").error.isEmpty());
        auto lenient = Base64Decoder::create({}, {});
        QCOMPARE(invoke(*lenient, " Z\tg\r=\n=\v\f ").bytes(), QByteArray("f"));
        auto url = Base64Decoder::create(
            {
                {"alphabet", "url"}
        },
            {});
        QVERIFY(!invoke(*url, "+/8=").error.isEmpty());
        auto high = Base64Decoder::create({}, {});
        QVERIFY(!invoke(*high, QByteArray("Zg==\xA0", 5)).error.isEmpty());
    }
    void boundsConfigurationCancellationAndIndependentInstances()
    {
        QString           error;
        Processor::Limits limits;
        limits.maxBytes     = 3;
        auto       bounded  = Base64Encoder::create({}, limits);
        const auto tooLarge = invoke(*bounded, "foo");
        QVERIFY(!tooLarge.error.isEmpty());
        QVERIFY(tooLarge.outputs.isEmpty());
        QCOMPARE(invoke(*bounded, "x").accepted, Processor::AcceptResult::Rejected);
        limits.maxBytes = 1;
        auto tail       = Base64Encoder::create({}, limits);
        QVERIFY(!invoke(*tail, "a").error.isEmpty());
        QCOMPARE(invoke(*tail, "b").accepted, Processor::AcceptResult::Rejected);
        auto first  = Base64Encoder::create({}, {});
        auto second = Base64Encoder::create({}, {});
        invoke(*first, "a");
        QCOMPARE(invoke(*second, "b").bytes(), QByteArray("Yg=="));
        bool quiescent = false;
        first->cancel([&] { quiescent = true; });
        QVERIFY(quiescent);
        QCOMPARE(invoke(*first, "bc").accepted, Processor::AcceptResult::Rejected);
        QVERIFY(!HexEncoder::create(
            {
                {"letterCase", "mixed"}
        },
            {},
            &error));
        QVERIFY(!HexEncoder::create(
            {
                {"separator", QString(17, 'x')}
        },
            {},
            &error));
        QVERIFY(HexEncoder::create(
            {
                {"separator", QString::fromUtf8("😀").repeated(16)}
        },
            {},
            &error));
        QVERIFY(!HexDecoder::create(
            {
                {"ignoreWhitespace", "true"}
        },
            {},
            &error));
        for (const QVariant& value : QVariantList {-1, 3, 4097, 4.5, QString("4"), true})
        {
            QVERIFY(!Base64Encoder::create(
                {
                    {"lineLength", value}
            },
                {},
                &error));
        }
        QVERIFY(!Base64Decoder::create(
            {
                {"alphabet", "both"}
        },
            {},
            &error));
        limits.maxChunks = 0;
        QVERIFY(!HexEncoder::create({}, limits, &error));
    }
    void reentryCancellationAndDestructionFromOutput()
    {
        auto processor = HexEncoder::create({}, {});
        bool output    = false;
        bool completed = false;
        bool cancelled = false;
        QCOMPARE(processor->accept({QByteArray("x")},
                                   {[&](auto) {
                                        output = true;
                                        QCOMPARE(processor->accept({QByteArray("y")}, {}),
                                                 Processor::AcceptResult::WouldBlock);
                                        processor->cancel([&] { cancelled = true; });
                                    },
                                    [&](auto) { completed = true; }}),
                 Processor::AcceptResult::Accepted);
        QVERIFY(output && cancelled && !completed);
        auto                destroy = Base64Encoder::create({}, {});
        auto*               raw     = destroy.release();
        QPointer<Processor> guard(raw);
        QCOMPARE(raw->accept({QByteArray("foo")},
                             {[&](auto) { delete raw; },
                              [&](auto) { QFAIL("Completion must be suppressed after destruction"); }}),
                 Processor::AcceptResult::Accepted);
        QVERIFY(!guard);
    }
    void duplexMetadataDefaultsAndConfiguration()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        for (const auto& format : QStringList {"hex", "base64"})
        {
            const auto id       = "native." + format + "-codec";
            const auto metadata = registry.type(id);
            QVERIFY(metadata);
            QCOMPARE(metadata->inputCount, 2);
            QCOMPARE(metadata->outputCount, 2);
            const auto label = format == "hex" ? QString("HEX") : QString("Base64");
            QCOMPARE(metadata->inputNames, QStringList({label, "Bytes"}));
            QCOMPARE(metadata->outputNames, QStringList({"Bytes", label}));
            const QVariantMap defaults = format == "hex" ?
                                             QVariantMap {
                                                 {      "encodeLetterCase", "upper"},
                                                 {       "encodeSeparator",     " "},
                                                 {"decodeIgnoreWhitespace",    true},
                                                 {      "decodeAccumulate",   false}
            } :
                                             QVariantMap {{"alphabet", "standard"},
                                                          {"encodePadding", true},
                                                          {"encodeLineLength", 0},
                                                          {"encodeLineEnding", "lf"},
                                                          {"decodeIgnoreWhitespace", true},
                                                          {"decodeRequirePadding", true}};
            const auto        resolved = registry.resolveConfiguration(id, {}, {}, &error);
            QVERIFY2(resolved, qPrintable(error));
            QCOMPARE(*resolved, defaults);
            auto runtime = registry.create(id, {}, {}, &error);
            QVERIFY2(runtime, qPrintable(error));
            QCOMPARE(runtime->configuration(), defaults);
            QCOMPARE(runtime->inputCount(), 2);
            QCOMPARE(runtime->outputCount(), 2);
            for (int port = 0; port < 2; ++port)
            {
                QCOMPARE(runtime->inputName(port), metadata->inputNames[port]);
                QCOMPARE(runtime->outputName(port), metadata->outputNames[port]);
            }
            QVERIFY(!registry.resolveConfiguration(id,
                                                   {
                                                       {"decodeIgnoreWhitespace", "true"}
            },
                                                   {},
                                                   &error));
            const auto key = format == "hex" ? "encodeLetterCase" : "alphabet";
            QVERIFY(!registry.resolveConfiguration(id,
                                                   {
                                                       {key, "invalid"}
            },
                                                   {},
                                                   &error));
        }
        auto hex = registry.create("native.hex-codec",
                                   {
                                       {"encodeLetterCase", "lower"},
                                       { "encodeSeparator",     ":"}
        },
                                   {},
                                   &error);
        QVERIFY2(hex, qPrintable(error));
        QCOMPARE(invoke(*hex, QByteArray::fromHex("ab00"), 1).bytes(), QByteArray("ab:00"));
        auto base64 = registry.create("native.base64-codec",
                                      {
                                          {            "alphabet", "url"},
                                          {       "encodePadding", false},
                                          {"decodeRequirePadding", false}
        },
                                      {},
                                      &error);
        QVERIFY2(base64, qPrintable(error));
        QCOMPARE(invoke(*base64, QByteArray::fromHex("fbff"), 1).bytes(), QByteArray("-_8"));
        QCOMPARE(invoke(*base64, "-_8", 0).bytes(), QByteArray::fromHex("fbff"));
    }

    void duplexInterleavedBinaryStreamsAndIndependentPacketReset()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        for (const auto& format : QStringList {"hex", "base64"})
        {
            auto runtime = registry.create("native." + format + "-codec", {}, {});
            QVERIFY(runtime);
            for (char byte : allBytes())
            {
                const QByteArray packet(1, byte);
                const auto       encoded = invoke(*runtime, packet, 1);
                QVERIFY(encoded.error.isEmpty());
                QCOMPARE(encoded.outputs.size(), 1);
                QCOMPARE(encoded.outputs[0].port, 1);
                const auto expected = format == "hex" ? packet.toHex().toUpper() : packet.toBase64();
                QCOMPARE(encoded.bytes(), expected);
                const auto decoded = invoke(*runtime, expected, 0);
                QVERIFY(decoded.error.isEmpty());
                QCOMPARE(decoded.bytes(), packet);
                QCOMPARE(decoded.outputs[0].port, 0);
            }
        }
        auto accumulating = registry.create("native.hex-codec",
                                            {
                                                {"decodeAccumulate", true}
        },
                                            {});
        QVERIFY(invoke(*accumulating, "A", 0).error.isEmpty());
        QCOMPARE(invoke(*accumulating, "f", 1).bytes(), QByteArray("66"));
        QCOMPARE(invoke(*accumulating, "b", 0).bytes(), QByteArray::fromHex("ab"));
    }

    void duplexInvalidPortsMalformedInputAndBoundsFailStop()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        for (const auto& format : QStringList {"hex", "base64"})
        {
            const auto id      = "native." + format + "-codec";
            auto       runtime = registry.create(id, {}, {});
            QVERIFY(runtime);
            for (int port : {-1, 2})
            {
                const auto invalid = invoke(*runtime, "!", port);
                QCOMPARE(invalid.accepted, Processor::AcceptResult::Rejected);
                QCOMPARE(invalid.completed, 0);
                QVERIFY(invalid.outputs.isEmpty());
            }
            QVERIFY(invoke(*runtime, "x", 1).error.isEmpty());
            const auto malformed = invoke(*runtime, format == "hex" ? QByteArray("0") : QByteArray("Zg="), 0);
            QVERIFY(!malformed.error.isEmpty());
            QCOMPARE(malformed.completed, 1);
            QVERIFY(malformed.outputs.isEmpty());
            QCOMPARE(invoke(*runtime, "x", 1).accepted, Processor::AcceptResult::Rejected);
            QCOMPARE(invoke(*runtime, {}, 0).accepted, Processor::AcceptResult::Rejected);
            Processor::Limits limits;
            limits.maxBytes = 3;
            auto bounded    = registry.create(id, {}, limits);
            QVERIFY(bounded);
            const auto overflow = invoke(*bounded, "foo", 1);
            QVERIFY(!overflow.error.isEmpty());
            QVERIFY(overflow.outputs.isEmpty());
            QCOMPARE(invoke(*bounded, "00", 0).accepted, Processor::AcceptResult::Rejected);
        }
    }

    void duplexCrossPortReentryCancellationAndDestruction()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        for (const auto& format : QStringList {"hex", "base64"})
        {
            const auto id      = "native." + format + "-codec";
            auto       runtime = registry.create(id, {}, {});
            QVERIFY(runtime);
            bool       output    = false;
            bool       completed = false;
            const auto text      = format == "hex" ? QByteArray("00") : QByteArray("AA==");
            QCOMPARE(runtime->accept({text, 0},
                                     {[&](auto chunk) {
                                          QCOMPARE(chunk.port, 0);
                                          output = true;
                                          QCOMPARE(runtime->accept({text, 0}, {}), Processor::AcceptResult::WouldBlock);
                                          const auto peer = invoke(*runtime, "x", 1);
                                          QCOMPARE(peer.accepted, Processor::AcceptResult::Accepted);
                                          QVERIFY(peer.error.isEmpty());
                                          QCOMPARE(peer.outputs.last().port, 1);
                                      },
                                      [&](QString error) {
                                          QVERIFY(error.isEmpty());
                                          completed = true;
                                      }}),
                     Processor::AcceptResult::Accepted);
            QVERIFY(output && completed);
            bool cancelled = false;
            completed      = false;
            QCOMPARE(runtime->accept({text, 0},
                                     {[&](auto) { runtime->cancel([&] { cancelled = true; }); },
                                      [&](auto) { completed = true; }}),
                     Processor::AcceptResult::Accepted);
            QVERIFY(cancelled && !completed);
            for (int port : {0, 1})
            {
                QCOMPARE(invoke(*runtime, {}, port).accepted, Processor::AcceptResult::Rejected);
            }
            auto destroy = registry.create(id, {}, {});
            QVERIFY(destroy);
            auto*               raw = destroy.get();
            QPointer<Processor> guard(raw);
            QCOMPARE(raw->accept({text, 0},
                                 {[&](auto) { destroy.reset(); },
                                  [&](auto) { QFAIL("Completion after destruction"); }}),
                     Processor::AcceptResult::Accepted);
            QVERIFY(!guard);
        }
    }

    void duplexCrossPortFailureCompletesEveryAcceptedInvocation()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        for (const auto& format : QStringList {"hex", "base64"})
        {
            auto runtime = registry.create("native." + format + "-codec", {}, {});
            QVERIFY(runtime);
            int     outerOutputs   = 0;
            int     outerCompleted = 0;
            QString outerError;
            Result  inner;
            QCOMPARE(runtime->accept({QByteArray("foo"), 1},
                                     {[&](auto output) {
                                          ++outerOutputs;
                                          QCOMPARE(output.port, 1);
                                          inner = invoke(*runtime,
                                                         format == "hex" ? QByteArray("0") : QByteArray("Zg="),
                                                         0);
                                      },
                                      [&](QString error) {
                                          ++outerCompleted;
                                          outerError = std::move(error);
                                      }}),
                     Processor::AcceptResult::Accepted);
            QCOMPARE(outerOutputs, 1);
            QCOMPARE(inner.accepted, Processor::AcceptResult::Accepted);
            QCOMPARE(inner.completed, 1);
            QVERIFY(!inner.error.isEmpty());
            QVERIFY(inner.outputs.isEmpty());
            QCOMPARE(outerCompleted, 1);
            QCOMPARE(outerError, inner.error);
            for (int port : {0, 1})
            {
                const auto rejected = invoke(*runtime, {}, port);
                QCOMPARE(rejected.accepted, Processor::AcceptResult::Rejected);
                QCOMPARE(rejected.completed, 0);
                QVERIFY(rejected.outputs.isEmpty());
            }
            bool cancelled = false;
            runtime->cancel([&] { cancelled = true; });
            QVERIFY(cancelled);
            QCOMPARE(outerCompleted, 1);
            QCOMPARE(outerOutputs, 1);
        }
    }

    void duplexLineflowFlipRoutesBothCodecDirections()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        for (const auto& format : QStringList {"hex", "base64"})
        {
            for (bool flipped : {false, true})
            {
                auto entry = node("codec", "native." + format + "-codec");
                entry.insert("lane", "duplex");
                entry.insert("flipped", flipped);
                ProcessorRegistry::Definition definition {
                    "paired",
                    "Paired codec",
                    "lineflow",
                    {{"channels", 2}, {"nodes", QVariantList {entry}}},
                    {},
                    {},
                    {},
                    {}
                };
                QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
                auto runtime = registry.create("paired", {}, {}, &error);
                QVERIFY2(runtime, qPrintable(error));
                const QByteArray bytes("a\0", 2);
                const auto       text       = format == "hex" ? QByteArray("61 00") : bytes.toBase64();
                const int        decodePort = flipped ? 1 : 0;
                const int        encodePort = flipped ? 0 : 1;
                const auto       decoded    = invoke(*runtime, text, decodePort);
                QVERIFY2(decoded.error.isEmpty(), qPrintable(decoded.error));
                QCOMPARE(decoded.bytes(), bytes);
                QCOMPARE(decoded.outputs.last().port, decodePort);
                const auto encoded = invoke(*runtime, bytes, encodePort);
                QVERIFY2(encoded.error.isEmpty(), qPrintable(encoded.error));
                QCOMPARE(encoded.bytes(), text);
                QCOMPARE(encoded.outputs.last().port, encodePort);
            }
        }
    }

    void registeredSchemasAndLineflowRoundTrips()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        for (const auto& prefix : QStringList {"hex", "base64"})
        {
            const auto encoder = "native." + prefix + "-encoder";
            const auto decoder = "native." + prefix + "-decoder";
            QVERIFY(registry.type(encoder));
            QVERIFY(registry.type(decoder));
            ProcessorRegistry::Definition definition {"roundtrip",
                                                      "Round trip",
                                                      "lineflow",
                                                      {{"nodes",
                                                        QVariantList {node("encode", encoder),
                                                                      node("decode", decoder)}}},
                                                      {},
                                                      {},
                                                      {},
                                                      {}};
            QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
            auto runtime = registry.create("roundtrip", {}, {}, &error);
            QVERIFY2(runtime, qPrintable(error));
            const auto bytes  = allBytes();
            const auto result = invoke(*runtime, bytes);
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QCOMPARE(result.completed, 1);
            QCOMPARE(result.bytes(), bytes);

            runtime.reset();
            runtime = registry.create("roundtrip", {}, {}, &error);
            QVERIFY2(runtime, qPrintable(error));
            QByteArray streamed;
            for (char byte : bytes)
            {
                const auto part = invoke(*runtime, QByteArray(1, byte));
                QVERIFY2(part.error.isEmpty(), qPrintable(part.error));
                QCOMPARE(part.completed, 1);
                streamed += part.bytes();
            }
            const auto final = invoke(*runtime, {});
            QVERIFY2(final.error.isEmpty(), qPrintable(final.error));
            streamed += final.bytes();
            QCOMPARE(streamed, bytes);

            runtime.reset();
            QCoreApplication::processEvents();
        }
        QVERIFY(!registry.resolveConfiguration("native.base64-encoder",
                                               {
                                                   {"lineLength", 5}
        },
                                               {},
                                               &error));
        QVERIFY(!registry.resolveConfiguration("native.hex-encoder",
                                               {
                                                   {"letterCase", "mixed"}
        },
                                               {},
                                               &error));
    }
};
QTEST_GUILESS_MAIN(CodecProcessorTests)
#include "CodecProcessorTests.moc"
