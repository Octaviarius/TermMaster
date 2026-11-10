#include "Nodes/Compress.h"
#include "Nodes/Uncompress.h"

#include <QDir>
#include <QFile>
#include <QTest>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/SettingsSchema.h>

namespace
{
struct Result
{
    Processor::AcceptResult accepted = Processor::AcceptResult::Rejected;
    QList<Processor::Chunk> output;
    QString                 error;
    int                     completed = 0;
};
Result invoke(Processor& processor, QByteArray bytes)
{
    Result result;
    result.accepted = processor.accept({std::move(bytes)},
                                       {[&](auto chunk) { result.output.append(std::move(chunk)); },
                                        [&](QString error) {
                                            result.error = std::move(error);
                                            ++result.completed;
                                        }});
    return result;
}
QByteArray payload()
{
    return QByteArray::fromHex("410042ff0d0a48656c6c6f2c20636f6d7072657373696f6e21");
}
QByteArray fixture(const QString& format)
{
    if (format == "zlib")
    {
        return QByteArray::fromHex("789c736470facfcbe5919a9393afa3909c9f5b50945a5c9c999fa7080069d708ad");
    }
    if (format == "gzip")
    {
        return QByteArray::fromHex(
            "1f8b080000000000000a736470facfcbe5919a9393afa3909c9f5b50945a5c9c999fa708005dbc281b19000000");
    }
    return QByteArray::fromHex("736470facfcbe5919a9393afa3909c9f5b50945a5c9c999fa70800");
}
} // namespace

class CompressionProcessorTests final : public QObject
{
    Q_OBJECT
private:
    ProcessorRegistry _registry;

    std::shared_ptr<Processor> create(const char*              id,
                                      const QVariantMap&       configuration = {},
                                      const Processor::Limits& limits        = {})
    {
        QString error;
        auto    node = _registry.create(QString::fromLatin1(id), configuration, limits, &error);
        if (!node)
        {
            qWarning().noquote() << error;
        }
        return node;
    }

private slots:
    void initTestCase()
    {
        registerNativeProcessors(_registry, "native");
    }
    void defaultsAndIndependentFixtures()
    {
        for (const auto* id : {"native.compress", "native.uncompress"})
        {
            const auto type = _registry.type(id);
            QVERIFY(type);
            QCOMPARE(type->group, QString("Compression"));
            auto node = create(id,
                               {
                                   {"extra", 42}
            });
            QVERIFY(node);
            QCOMPARE(node->configuration().value("extra").toInt(), 42);
            SettingsSchema schema(type->settingsSchema);
            QVERIFY(schema.validate(node->configuration()).isEmpty());
        }
        for (const auto& format : {QString("zlib"), QString("gzip"), QString("raw")})
        {
            auto node = create("native.uncompress",
                               {
                                   {"format", format}
            });
            QVERIFY(node);
            auto result = invoke(*node, fixture(format));
            QCOMPARE(result.accepted, Processor::AcceptResult::Accepted);
            QCOMPARE(result.completed, 1);
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QCOMPARE(result.output.size(), 1);
            QCOMPARE(result.output.first().bytes, payload());
            QCOMPARE(result.output.first().port, 0);
            result = invoke(*node, fixture(format));
            QCOMPARE(result.output.first().bytes, payload());
            if (format != "raw")
            {
                auto automatic = create("native.uncompress");
                QCOMPARE(invoke(*automatic, fixture(format)).output.first().bytes, payload());
            }
        }
    }
    void settingsMatrixAndEmptyPackets()
    {
        for (const auto& format : {QString("zlib"), QString("gzip"), QString("raw")})
        {
            for (const auto& strategy :
                 {QString("default"), QString("filtered"), QString("huffman"), QString("rle"), QString("fixed")})
            {
                for (const auto level : {-1, 0, 1, 6, 9})
                {
                    for (const auto window : {9, 15})
                    {
                        auto encoder = create("native.compress",
                                              {
                                                  {    "format",              format},
                                                  {  "strategy",            strategy},
                                                  {     "level",               level},
                                                  {"windowBits",              window},
                                                  {  "memLevel", window == 9 ? 1 : 9}
                        });
                        auto decoder = create("native.uncompress",
                                              {
                                                  {    "format", format},
                                                  {"windowBits", window}
                        });
                        QVERIFY(encoder && decoder);
                        for (const auto& bytes : {payload(), QByteArray {}, QByteArray(1000, 'x')})
                        {
                            const auto compressed = invoke(*encoder, bytes);
                            QCOMPARE(compressed.completed, 1);
                            QVERIFY2(compressed.error.isEmpty(), qPrintable(compressed.error));
                            QCOMPARE(compressed.output.size(), 1);
                            QVERIFY(!compressed.output.first().bytes.isEmpty());
                            const auto decoded = invoke(*decoder, compressed.output.first().bytes);
                            QVERIFY2(decoded.error.isEmpty(), qPrintable(decoded.error));
                            QCOMPARE(decoded.output.size(), 1);
                            QCOMPARE(decoded.output.first().bytes, bytes);
                        }
                    }
                }
            }
        }
    }
    void malformedPacketsAndChecksums()
    {
        for (const auto& format : {QString("zlib"), QString("gzip"), QString("raw")})
        {
            const auto complete = fixture(format);
            for (qsizetype size = 0; size < complete.size(); ++size)
            {
                auto       node   = create("native.uncompress",
                                           {
                                               {"format", format}
                });
                const auto result = invoke(*node, complete.left(size));
                QCOMPARE(result.accepted, Processor::AcceptResult::Accepted);
                QCOMPARE(result.completed, 1);
                QVERIFY(!result.error.isEmpty());
                QVERIFY(result.output.isEmpty());
            }
            auto       node     = create("native.uncompress",
                                         {
                                             {"format", format}
            });
            const auto trailing = invoke(*node, complete + "junk");
            QVERIFY(!trailing.error.isEmpty());
            QVERIFY(trailing.output.isEmpty());
            if (format != "raw")
            {
                node                         = create("native.uncompress",
                                                      {
                                                          {"format", format}
                });
                auto corrupt                 = complete;
                corrupt[corrupt.size() - 1] ^= 0x40;
                const auto result            = invoke(*node, corrupt);
                QVERIFY(!result.error.isEmpty());
                QVERIFY(result.output.isEmpty());
            }
        }
        auto       dictionary = create("native.uncompress");
        const auto result = invoke(*dictionary, QByteArray::fromHex("78bb762309694bc621ae50905899939f980200ce410c73"));
        QVERIFY(result.error.contains("dictionary", Qt::CaseInsensitive));
        QVERIFY(result.output.isEmpty());
    }
    void concatenatedMembers()
    {
        for (const auto& format : {QString("zlib"), QString("gzip"), QString("raw")})
        {
            auto       strict = create("native.uncompress",
                                       {
                                           {"format", format}
            });
            const auto stream = fixture(format) + fixture(format);
            QVERIFY(!invoke(*strict, stream).error.isEmpty());
            auto       multiple = create("native.uncompress",
                                         {
                                             {      "format", format},
                                             {"concatenated",   true}
            });
            const auto result   = invoke(*multiple, stream);
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QCOMPARE(result.output.size(), 1);
            QCOMPARE(result.output.first().bytes, payload() + payload());
            multiple          = create("native.uncompress",
                                       {
                                           {      "format", format},
                                           {"concatenated",   true}
            });
            const auto broken = invoke(*multiple, fixture(format) + fixture(format).chopped(1));
            QVERIFY(!broken.error.isEmpty());
            QVERIFY(broken.output.isEmpty());
        }
        auto mixed = create("native.uncompress",
                            {
                                {"concatenated", true}
        });
        QCOMPARE(invoke(*mixed, fixture("gzip") + fixture("zlib")).output.first().bytes, payload() + payload());
    }
    void outputBoundsAndLifecycle()
    {
        auto       limited = create("native.uncompress",
                                    {
                                        {"maxOutputBytes", 8}
        });
        const auto result  = invoke(*limited, fixture("zlib"));
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.output.isEmpty());
        QCOMPARE(result.completed, 1);
        QCOMPARE(limited->accept({fixture("zlib")}, {}), Processor::AcceptResult::Rejected);

        auto              encoder = create("native.compress");
        const auto        packed  = invoke(*encoder, QByteArray(8192, 'x')).output.first().bytes;
        Processor::Limits limits;
        limits.maxBytes        = 64;
        auto       hostBounded = create("native.uncompress", {}, limits);
        const auto oversized   = invoke(*hostBounded, packed);
        QVERIFY(!oversized.error.isEmpty());
        QVERIFY(oversized.output.isEmpty());
        limits.maxBytes            = 10;
        auto       compressBounded = create("native.compress",
                                            {
                                                {"format", "gzip"}
        },
                                            limits);
        const auto expansion       = invoke(*compressBounded, "0123456789");
        QVERIFY(!expansion.error.isEmpty());
        QVERIFY(expansion.output.isEmpty());

        auto cancelled = create("native.compress");
        bool quiescent = false;
        cancelled->cancel([&] { quiescent = true; });
        QVERIFY(quiescent);
        QCOMPARE(cancelled->accept({"late"}, {}), Processor::AcceptResult::Rejected);
    }
    void stepBudgetAndBorrowedStorage()
    {
        Processor::Limits limits;
        limits.maxSteps    = 1;
        auto       decoder = create("native.uncompress",
                                    {
                                        {"concatenated", true}
        },
                                    limits);
        const auto result  = invoke(*decoder, fixture("gzip") + fixture("gzip"));
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.output.isEmpty());

        QByteArray noise(65536, Qt::Uninitialized);
        quint32    state = 42;
        for (auto& byte : noise)
        {
            state = state * 1664525U + 1013904223U;
            byte  = char(state >> 24);
        }
        auto       encoder = create("native.compress", {}, limits);
        const auto limited = invoke(*encoder, noise);
        QVERIFY(!limited.error.isEmpty());
        QVERIFY(limited.output.isEmpty());

        char source[] {'A', '\0', char(255)};
        encoder           = create("native.compress");
        const auto packed = invoke(*encoder, QByteArray::fromRawData(source, 3)).output.first().bytes;
        source[0]         = 'B';
        QVERIFY(invoke(*encoder, "a different packet").error.isEmpty());
        decoder            = create("native.uncompress");
        const auto decoded = invoke(*decoder, packed).output.first().bytes;
        QCOMPARE(decoded, QByteArray("A\0\xff", 3));
    }

    void invalidConfiguration()
    {
        for (const auto& options : {QVariantMap {{"format", "bad"}},
                                    QVariantMap {{"level", 10}},
                                    QVariantMap {{"level", true}},
                                    QVariantMap {{"windowBits", 8}},
                                    QVariantMap {{"memLevel", 0}},
                                    QVariantMap {{"strategy", "bad"}},
                                    QVariantMap {{"level", "6"}},
                                    QVariantMap {{"level", 2.5}}})
        {
            QString error;
            QVERIFY(!Compress::create(options, {}, &error));
            QVERIFY(!error.isEmpty());
        }
        for (const auto& options : {QVariantMap {{"format", "bad"}},
                                    QVariantMap {{"maxOutputBytes", 0}},
                                    QVariantMap {{"maxOutputBytes", true}},
                                    QVariantMap {{"concatenated", 1}}})
        {
            QString error;
            QVERIFY(!Uncompress::create(options, {}, &error));
            QVERIFY(!error.isEmpty());
        }
    }
    void exportInteroperability()
    {
        const auto directory = qEnvironmentVariable("TERMASTER_COMPRESSION_CAPTURE");
        if (directory.isEmpty())
        {
            QSKIP("Opt-in independent Python zlib interoperability capture");
        }
        QVERIFY(QDir().mkpath(directory));
        const auto bytes = payload() + QByteArray(16384, 'A');
        QFile      original(QDir(directory).filePath("original.bin"));
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write(bytes), qint64(bytes.size()));
        for (const auto& format : {QString("zlib"), QString("gzip"), QString("raw")})
        {
            auto       encoder = create("native.compress",
                                        {
                                            {"format", format}
            });
            const auto result  = invoke(*encoder, bytes);
            QVERIFY(result.error.isEmpty());
            QFile file(QDir(directory).filePath(format + ".bin"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(result.output.first().bytes), qint64(result.output.first().bytes.size()));
        }
    }
};
QTEST_GUILESS_MAIN(CompressionProcessorTests)
#include "CompressionProcessorTests.moc"
