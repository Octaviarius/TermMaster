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
Result invoke(Processor& processor, QByteArray bytes, Processor::Boundary boundary = Processor::Boundary::StreamChunk)
{
    auto result      = std::make_shared<Result>();
    result->accepted = processor.accept({std::move(bytes), boundary},
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
    void hexEverySplitAndPacketReset()
    {
        const auto bytes = allBytes();
        QByteArray expected;
        for (int index = 0; index < bytes.size(); ++index)
        {
            if (index)
            {
                expected += ' ';
            }
            expected += bytes.mid(index, 1).toHex().toUpper();
        }
        for (int split = 0; split <= bytes.size(); ++split)
        {
            auto       encoder = HexEncoder::create({}, {});
            const auto first   = invoke(*encoder, bytes.left(split));
            const auto second  = invoke(*encoder, bytes.mid(split), Processor::Boundary::Packet);
            QVERIFY(first.error.isEmpty() && second.error.isEmpty());
            QCOMPARE(first.completed, 1);
            QCOMPARE(second.completed, 1);
            QCOMPARE(first.bytes() + second.bytes(), expected);
            QCOMPARE(second.outputs.size(), 1);
            QCOMPARE(second.outputs[0].boundary, Processor::Boundary::Packet);
            QCOMPARE(invoke(*encoder, QByteArray("\0", 1), Processor::Boundary::Packet).bytes(), QByteArray("00"));
        }
        for (int split = 0; split <= expected.size(); ++split)
        {
            auto       decoder = HexDecoder::create({}, {});
            const auto first   = invoke(*decoder, expected.left(split));
            const auto second  = invoke(*decoder, expected.mid(split), Processor::Boundary::Packet);
            QVERIFY2(first.error.isEmpty() && second.error.isEmpty(), qPrintable(first.error + second.error));
            QCOMPARE(first.bytes() + second.bytes(), bytes);
        }
        auto encoder = HexEncoder::create(
            {
                {"letterCase",                "lower"},
                { "separator", QString::fromUtf8("·")}
        },
            {});
        QCOMPARE(invoke(*encoder, QByteArray::fromHex("abcdef"), Processor::Boundary::Packet).bytes(),
                 QString::fromUtf8("ab·cd·ef").toUtf8());
        QCOMPARE(invoke(*encoder, {}, Processor::Boundary::Packet).outputs[0].bytes, QByteArray {});
    }
    void hexWhitespaceTailsAndMalformed()
    {
        auto decoder = HexDecoder::create({}, {});
        QCOMPARE(invoke(*decoder, "A").outputs.size(), 0);
        QCOMPARE(invoke(*decoder, " \t\r\n\v\f").outputs.size(), 0);
        QCOMPARE(invoke(*decoder, "b", Processor::Boundary::Packet).bytes(), QByteArray::fromHex("ab"));
        QCOMPARE(invoke(*decoder, {}, Processor::Boundary::Packet).outputs.size(), 1);
        auto strict = HexDecoder::create(
            {
                {"ignoreWhitespace", false}
        },
            {});
        const auto invalid = invoke(*strict, "00 11", Processor::Boundary::Packet);
        QVERIFY(!invalid.error.isEmpty());
        QVERIFY(invalid.outputs.isEmpty());
        QCOMPARE(invoke(*strict, "00").accepted, Processor::AcceptResult::Rejected);
        for (const auto& bytes : QList<QByteArray> {"0", "001x", QByteArray("00\0", 3), QByteArray("0\xA0", 2)})
        {
            auto       instance = HexDecoder::create({}, {});
            const auto result   = invoke(*instance, bytes, Processor::Boundary::Packet);
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.outputs.isEmpty());
            QCOMPARE(result.completed, 1);
        }
    }
    void base64EverySplitAllBytesAndBothAlphabets()
    {
        const auto bytes = allBytes();
        for (const auto& alphabet : QStringList {"standard", "url"})
        {
            for (bool padding : {true, false})
            {
                QByteArray::Base64Options options = alphabet == "url" ? QByteArray::Base64UrlEncoding :
                                                                        QByteArray::Base64Encoding;
                if (!padding)
                {
                    options |= QByteArray::OmitTrailingEquals;
                }
                const auto expected = bytes.toBase64(options);
                for (int split = 0; split <= bytes.size(); ++split)
                {
                    auto encoder = Base64Encoder::create(
                        {
                            {"alphabet", alphabet},
                            { "padding",  padding}
                    },
                        {});
                    const auto first  = invoke(*encoder, bytes.left(split));
                    const auto second = invoke(*encoder, bytes.mid(split), Processor::Boundary::Packet);
                    QVERIFY(first.error.isEmpty() && second.error.isEmpty());
                    QCOMPARE(first.bytes() + second.bytes(), expected);
                    QCOMPARE(second.outputs[0].boundary, Processor::Boundary::Packet);
                }
                for (int split = 0; split <= expected.size(); ++split)
                {
                    auto decoder = Base64Decoder::create(
                        {
                            {      "alphabet", alphabet},
                            {"requirePadding",  padding}
                    },
                        {});
                    const auto first  = invoke(*decoder, expected.left(split));
                    const auto second = invoke(*decoder, expected.mid(split), Processor::Boundary::Packet);
                    QVERIFY2(first.error.isEmpty() && second.error.isEmpty(), qPrintable(first.error + second.error));
                    QCOMPARE(first.bytes() + second.bytes(), bytes);
                }
            }
        }
    }
    void base64StreamTailFinalizationAndWrapping()
    {
        auto encoder = Base64Encoder::create({}, {});
        QCOMPARE(invoke(*encoder, "f").outputs.size(), 0);
        QCOMPARE(invoke(*encoder, {}).outputs.size(), 0);
        QCOMPARE(invoke(*encoder, "o").outputs.size(), 0);
        QCOMPARE(invoke(*encoder, {}, Processor::Boundary::Packet).bytes(), QByteArray("Zm8="));
        QCOMPARE(invoke(*encoder, "f", Processor::Boundary::Packet).bytes(), QByteArray("Zg=="));
        QCOMPARE(invoke(*encoder, {}, Processor::Boundary::Packet).outputs.size(), 1);
        auto unpadded = Base64Encoder::create(
            {
                {"padding", false}
        },
            {});
        QCOMPARE(invoke(*unpadded, "fo", Processor::Boundary::Packet).bytes(), QByteArray("Zm8"));
        for (const auto& ending : QStringList {"lf", "crlf"})
        {
            auto wrapped = Base64Encoder::create(
                {
                    {"lineLength",      4},
                    {"lineEnding", ending}
            },
                {});
            QCOMPARE(invoke(*wrapped, "foo").bytes(), QByteArray("Zm9v"));
            QCOMPARE(invoke(*wrapped, "bar").bytes(),
                     (ending == "lf" ? QByteArray("\n") : QByteArray("\r\n")) + "YmFy");
            const auto final = invoke(*wrapped, {}, Processor::Boundary::Packet);
            QVERIFY(final.bytes().isEmpty());
            QCOMPARE(final.outputs[0].boundary, Processor::Boundary::Packet);
            QCOMPARE(invoke(*wrapped, "f", Processor::Boundary::Packet).bytes(), QByteArray("Zg=="));
        }
        auto decoder = Base64Decoder::create({}, {});
        QCOMPARE(invoke(*decoder, "Zg=").outputs.size(), 0);
        QCOMPARE(invoke(*decoder, "=").bytes(), QByteArray("f"));
        QCOMPARE(invoke(*decoder, {}, Processor::Boundary::Packet).outputs[0].boundary, Processor::Boundary::Packet);
        QCOMPARE(invoke(*decoder, "Zm8=", Processor::Boundary::Packet).bytes(), QByteArray("fo"));
        auto optional = Base64Decoder::create(
            {
                {"requirePadding", false}
        },
            {});
        QCOMPARE(invoke(*optional, "Zm8").outputs.size(), 0);
        QCOMPARE(invoke(*optional, {}).outputs.size(), 0);
        QCOMPARE(invoke(*optional, {}, Processor::Boundary::Packet).bytes(), QByteArray("fo"));
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
            const auto result  = invoke(*decoder, bytes, Processor::Boundary::Packet);
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
            const auto result = invoke(*decoder, bytes, Processor::Boundary::Packet);
            QVERIFY2(!result.error.isEmpty(), bytes.constData());
            QVERIFY(result.outputs.isEmpty());
        }
        auto strict = Base64Decoder::create(
            {
                {"ignoreWhitespace", false}
        },
            {});
        QVERIFY(!invoke(*strict, "Zg==\n", Processor::Boundary::Packet).error.isEmpty());
        auto lenient = Base64Decoder::create({}, {});
        QCOMPARE(invoke(*lenient, " Z\tg\r=\n=\v\f ", Processor::Boundary::Packet).bytes(), QByteArray("f"));
        auto url = Base64Decoder::create(
            {
                {"alphabet", "url"}
        },
            {});
        QVERIFY(!invoke(*url, "+/8=", Processor::Boundary::Packet).error.isEmpty());
        auto high = Base64Decoder::create({}, {});
        QVERIFY(!invoke(*high, QByteArray("Zg==\xA0", 5), Processor::Boundary::Packet).error.isEmpty());
    }
    void boundsConfigurationCancellationAndIndependentInstances()
    {
        QString           error;
        Processor::Limits limits;
        limits.maxBytes     = 3;
        auto       bounded  = Base64Encoder::create({}, limits);
        const auto tooLarge = invoke(*bounded, "foo", Processor::Boundary::Packet);
        QVERIFY(!tooLarge.error.isEmpty());
        QVERIFY(tooLarge.outputs.isEmpty());
        QCOMPARE(invoke(*bounded, "x").accepted, Processor::AcceptResult::Rejected);
        limits.maxBytes = 1;
        auto tail       = Base64Encoder::create({}, limits);
        QVERIFY(invoke(*tail, "a").error.isEmpty());
        QVERIFY(!invoke(*tail, "b").error.isEmpty());
        auto first  = Base64Encoder::create({}, {});
        auto second = Base64Encoder::create({}, {});
        invoke(*first, "a");
        QCOMPARE(invoke(*second, "b", Processor::Boundary::Packet).bytes(), QByteArray("Yg=="));
        bool quiescent = false;
        first->cancel([&] { quiescent = true; });
        QVERIFY(quiescent);
        QCOMPARE(invoke(*first, "bc", Processor::Boundary::Packet).accepted, Processor::AcceptResult::Rejected);
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
            const auto result = invoke(*runtime, bytes, Processor::Boundary::Packet);
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QCOMPARE(result.completed, 1);
            QCOMPARE(result.bytes(), bytes);
            QCOMPARE(result.outputs.last().boundary, Processor::Boundary::Packet);
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
                for (const auto& output : part.outputs)
                {
                    QCOMPARE(output.boundary, Processor::Boundary::StreamChunk);
                }
            }
            const auto final = invoke(*runtime, {}, Processor::Boundary::Packet);
            QVERIFY2(final.error.isEmpty(), qPrintable(final.error));
            streamed += final.bytes();
            QCOMPARE(streamed, bytes);
            QCOMPARE(final.outputs.last().boundary, Processor::Boundary::Packet);
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
