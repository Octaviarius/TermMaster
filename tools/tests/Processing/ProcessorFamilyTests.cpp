#include "Nodes/Base64.h"
#include "Nodes/Checksum.h"
#include "Nodes/Compression.h"
#include "Nodes/Hex.h"
#include "Nodes/Length.h"
#include "Nodes/Stuffing.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
#include <Termaster/Core/Gui/Settings/SettingsFormModel.h>
#include <Termaster/Core/Gui/Settings/SettingsFormWidget.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/SettingsSchema.h>

namespace
{
struct Result
{
    Processor::AcceptResult accepted;
    QList<Processor::Chunk> output;
    QString                 error;
    int                     completed = 0;
};
Result invoke(Processor& processor, QByteArray bytes, int port)
{
    Result result;
    result.accepted = processor.accept({std::move(bytes), port},
                                       {[&](auto output) { result.output.append(std::move(output)); },
                                        [&](QString error) {
                                            result.error = std::move(error);
                                            ++result.completed;
                                        }});
    QElapsedTimer timer;
    timer.start();
    while (result.accepted == Processor::AcceptResult::Accepted && result.completed == 0 && timer.elapsed() < 2000)
    {
        QTest::qWait(1);
    }
    if (result.accepted == Processor::AcceptResult::Accepted && result.completed == 0)
    {
        processor.cancel({});
        result.error = "Timed out waiting for family processor";
    }
    return result;
}
QStringList keys(const SettingsSchema& schema)
{
    QStringList result;
    for (const auto& field : schema.fields())
    {
        result.append(field.key);
    }
    result.sort();
    return result;
}
class Deferred final : public Processor
{
public:
    QVariantMap configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk, Callbacks callbacks) override
    {
        if (_busy)
        {
            return AcceptResult::WouldBlock;
        }
        _busy      = true;
        _callbacks = std::move(callbacks);
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> done) override
    {
        _callbacks = {};
        _cancelled = std::move(done);
    }
    void acknowledge()
    {
        auto callback = std::move(_cancelled);
        if (callback)
        {
            callback();
        }
    }

private:
    bool                  _busy = false;
    Callbacks             _callbacks;
    std::function<void()> _cancelled;
};
} // namespace

class ProcessorFamilyTests final : public QObject
{
    Q_OBJECT
private slots:
    void selectedDescriptorsAndEditors()
    {
        struct Definition
        {
            Codec::Definition family;
            QStringList       common;
            QStringList       direct;
            QStringList       inverse;
            QString           directPrefix;
            QString           inversePrefix;
        };
        const QList<Definition> definitions {
            {     Length::descriptor(),
             {"encoding", "includeField", "adjustment", "position", "width", "endianness", "maxBytes"},
             {},
             {},
             "wrap",     "unwrap"    },
            {   Checksum::descriptor(),
             {"algorithm", "position", "endianness", "width", "polynomial", "init", "refin", "refout", "xorout"},
             {},
             {},
             "wrap",     "unwrap"    },
            {   Stuffing::descriptor(),
             {"boundaryMode", "delimiter", "escape", "delimiterCode", "escapeCode"},
             {},
             {"framing"},
             "wrap",     "unwrap"    },
            {        Hex::descriptor(),
             {},
             {"letterCase", "separator"},
             {"ignoreWhitespace", "accumulate"},
             "encode",     "decode"  },
            {     Base64::descriptor(),
             {"alphabet"},
             {"padding", "lineLength", "lineEnding"},
             {"ignoreWhitespace", "requirePadding"},
             "encode",     "decode"  },
            {Compression::descriptor(),
             {"format", "windowBits"},
             {"level", "memLevel", "strategy"},
             {"autoDetect", "maxOutputBytes", "concatenated"},
             "compress", "uncompress"}
        };
        QTemporaryDir          directory;
        Settings               settings(directory.filePath("settings.yaml"));
        SettingsWidgetRegistry widgets;
        for (const auto& definition : definitions)
        {
            for (auto mode : {Codec::Mode::Direct, Codec::Mode::Inverse, Codec::Mode::Duplex})
            {
                auto descriptor = std::make_shared<const SettingsSchema>(definition.family.schema(mode));
                auto expected   = definition.common;
                auto add        = [&](const QStringList& fields, const QString& prefix) {
                    for (auto key : fields)
                    {
                        if (mode == Codec::Mode::Duplex)
                        {
                            key[0] = key[0].toUpper();
                            key    = prefix + key;
                        }
                        expected.append(key);
                    }
                };
                if (mode != Codec::Mode::Inverse)
                {
                    add(definition.direct, definition.directPrefix);
                }
                if (mode != Codec::Mode::Direct)
                {
                    add(definition.inverse, definition.inversePrefix);
                }
                expected.sort();
                QCOMPARE(keys(*descriptor), expected);
                SettingsFormModel  model(descriptor, settings.node("node"));
                SettingsFormWidget form(model, widgets);
                for (const auto& key : expected)
                {
                    QVERIFY2(form.editor(key), qPrintable(key));
                }
                QCOMPARE(form.findChildren<SettingsFieldEditor*>().size(), expected.size());
                const auto unused = mode == Codec::Mode::Direct ? definition.inverse : definition.direct;
                for (const auto& key : unused)
                {
                    if (mode != Codec::Mode::Duplex)
                    {
                        QVERIFY(!form.editor(key));
                    }
                }
            }
        }
    }
    void codecConfigurationPrunesOnlyKnownInactiveCommonFields()
    {
        QString error;
        auto    length = Length::descriptor().create(Codec::Mode::Duplex,
                                                     {
                                                         {  "encoding", "varint"},
                                                         {     "width",        8},
                                                         {  "position", "suffix"},
                                                         {"endianness",    "big"},
                                                         {   "unknown",       42}
        },
                                                     {},
                                                     &error);
        QVERIFY2(length, qPrintable(error));
        for (const auto* key : {"width", "position", "endianness"})
        {
            QVERIFY(!length->configuration().contains(key));
        }
        QCOMPARE(length->configuration().value("maxBytes").toInt(), 10);
        QCOMPARE(length->configuration().value("unknown").toInt(), 42);
        auto checksum = Checksum::descriptor().create(Codec::Mode::Duplex,
                                                      {
                                                          { "algorithm", "sum8"},
                                                          {     "width",     32},
                                                          {"polynomial",      1},
                                                          {      "init",      9},
                                                          {     "refin",   true},
                                                          {    "refout",   true},
                                                          {    "xorout",      1},
                                                          {   "unknown",     42}
        },
                                                      {},
                                                      &error);
        QVERIFY2(checksum, qPrintable(error));
        for (const auto* key : {"width", "polynomial", "init", "refin", "refout", "xorout"})
        {
            QVERIFY(!checksum->configuration().contains(key));
        }
        QCOMPARE(checksum->configuration().value("unknown").toInt(), 42);
    }
    void factoriesConstructOnlySelectedDirection()
    {
        int  direct  = 0;
        int  inverse = 0;
        auto factory = [](int& count) {
            return [&count](const QVariantMap&, const Processor::Limits&, QString*) -> std::unique_ptr<Processor> {
                ++count;
                return std::make_unique<Deferred>();
            };
        };
        Codec::Definition family(
            "version: 1\ntype: termaster.settings-schema\ndescription: Fixture\ncontent: {type: object, properties: {}}\n",
            {},
            {},
            factory(direct),
            factory(inverse),
            "Wire",
            "Payload");
        auto ordinary = family.create(Codec::Mode::Direct, {}, {});
        QCOMPARE(direct, 1);
        QCOMPARE(inverse, 0);
        QCOMPARE(ordinary->inputCount(), 1);
        auto reverse = family.create(Codec::Mode::Inverse, {}, {});
        QCOMPARE(direct, 1);
        QCOMPARE(inverse, 1);
        auto codec = family.create(Codec::Mode::Duplex, {}, {});
        QCOMPARE(direct, 2);
        QCOMPARE(inverse, 2);
        QCOMPARE(codec->inputCount(), 2);
    }
    void catalogTopologyRoundTripsAndIndependentPorts()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        int unary  = 0;
        int duplex = 0;
        for (const auto& type : registry.types())
        {
            if (type.inputCount == 1 && type.outputCount == 1)
            {
                ++unary;
            }
            if (type.inputCount == 2 && type.outputCount == 2)
            {
                ++duplex;
            }
        }
        QCOMPARE(unary, 20);
        QCOMPARE(duplex, 6);
        QByteArray payload;
        for (int byte = 0; byte < 256; ++byte)
        {
            payload.append(char(byte));
        }
        for (const auto& family : QStringList {"length", "checksum", "stuffing", "hex", "base64", "compression"})
        {
            QString    error;
            const auto id    = "native." + family + "-codec";
            auto       codec = registry.create(id, {}, {}, &error);
            QVERIFY2(codec, qPrintable(error));
            QCOMPARE(codec->inputCount(), 2);
            QCOMPARE(codec->outputCount(), 2);
            const auto type = registry.type(id);
            for (int port : {0, 1})
            {
                QCOMPARE(codec->inputName(port), type->inputNames[port]);
                QCOMPARE(codec->outputName(port), type->outputNames[port]);
            }
            for (int iteration = 0; iteration < 2; ++iteration)
            {
                const auto encoded = invoke(*codec, payload, 1);
                QVERIFY2(encoded.error.isEmpty(), qPrintable(encoded.error));
                QCOMPARE(encoded.completed, 1);
                QCOMPARE(encoded.output.size(), 1);
                QCOMPARE(encoded.output.first().port, 1);
                const auto decoded = invoke(*codec, encoded.output.first().bytes, 0);
                QVERIFY2(decoded.error.isEmpty(), qPrintable(decoded.error));
                QCOMPARE(decoded.completed, 1);
                QCOMPARE(decoded.output.size(), 1);
                QCOMPARE(decoded.output.first().port, 0);
                QCOMPARE(decoded.output.first().bytes, payload);
            }
            QCOMPARE(invoke(*codec, {}, 2).accepted, Processor::AcceptResult::Rejected);
        }
    }
    void framingStateCoexistsWithWrapAndOwnsOutputs()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto       codec   = registry.create("native.stuffing-codec", {}, {});
        const auto pending = invoke(*codec, QByteArray::fromHex("41db"), 0);
        QVERIFY(pending.error.isEmpty());
        QVERIFY(pending.output.isEmpty());
        QCOMPARE(pending.completed, 1);
        const auto wrapped = invoke(*codec, QByteArray::fromHex("c0db00"), 1);
        QCOMPARE(wrapped.output.first().bytes, QByteArray::fromHex("dbdcdbdd00c0"));
        const auto decoded = invoke(*codec, QByteArray::fromHex("dc00c042c0"), 0);
        QCOMPARE(decoded.output.size(), 2);
        QCOMPARE(decoded.output[0].bytes, QByteArray::fromHex("41c000"));
        QCOMPARE(decoded.output[1].bytes, QByteArray("B"));
        codec.reset();
        QCOMPARE(wrapped.output.first().bytes, QByteArray::fromHex("dbdcdbdd00c0"));
        QCOMPARE(decoded.output[0].bytes, QByteArray::fromHex("41c000"));
    }
    void cancellationWaitsForBothChildrenAndRepeatedCallers()
    {
        auto  inverse    = std::make_unique<Deferred>();
        auto  direct     = std::make_unique<Deferred>();
        auto* inverseRaw = inverse.get();
        auto* directRaw  = direct.get();
        auto  codec = std::make_unique<Codec>(QVariantMap {}, "Wire", "Payload", std::move(inverse), std::move(direct));
        QCOMPARE(codec->accept({QByteArray("a"), 0}, {}), Processor::AcceptResult::Accepted);
        QCOMPARE(codec->accept({QByteArray("b"), 0}, {}), Processor::AcceptResult::WouldBlock);
        QCOMPARE(codec->accept({QByteArray("b"), 1}, {}), Processor::AcceptResult::Accepted);
        int acknowledgements = 0;
        codec->cancel([&] { ++acknowledgements; });
        codec->cancel([&] { ++acknowledgements; });
        QCOMPARE(acknowledgements, 0);
        inverseRaw->acknowledge();
        QCOMPARE(acknowledgements, 0);
        directRaw->acknowledge();
        QCOMPARE(acknowledgements, 2);
        codec->cancel([&] { ++acknowledgements; });
        QCOMPARE(acknowledgements, 3);
        QCOMPARE(codec->accept({QByteArray("x"), 1}, {}), Processor::AcceptResult::Rejected);
    }
    void allFamilyLifecycleAndBounds()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        const QStringList       families {"length", "checksum", "stuffing", "hex", "base64", "compression"};
        const QList<QByteArray> malformed {QByteArray("x"),
                                           QByteArray("x"),
                                           QByteArray::fromHex("dbffc0"),
                                           QByteArray("x"),
                                           QByteArray("x"),
                                           QByteArray("x")};
        for (int index = 0; index < families.size(); ++index)
        {
            const auto id    = "native." + families[index] + "-codec";
            auto       codec = registry.create(id, {}, {});
            QVERIFY(codec);
            bool completed = false;
            QCOMPARE(codec->accept({QByteArray("data"), 1},
                                   {[&](auto output) {
                                        QCOMPARE(codec->accept({QByteArray("again"), 1}, {}),
                                                 Processor::AcceptResult::WouldBlock);
                                        const auto peer = invoke(*codec, output.bytes, 0);
                                        QVERIFY2(peer.error.isEmpty(), qPrintable(peer.error));
                                        QCOMPARE(peer.output.first().bytes, QByteArray("data"));
                                    },
                                    [&](auto error) {
                                        QVERIFY(error.isEmpty());
                                        completed = true;
                                    }}),
                     Processor::AcceptResult::Accepted);
            QVERIFY(completed);
            bool cancelled = false;
            completed      = false;
            QCOMPARE(codec->accept({QByteArray("data"), 1},
                                   {[&](auto) { codec->cancel([&] { cancelled = true; }); },
                                    [&](auto) { completed = true; }}),
                     Processor::AcceptResult::Accepted);
            QVERIFY(cancelled && !completed);
            QCOMPARE(codec->accept({QByteArray("data"), 0}, {}), Processor::AcceptResult::Rejected);
            codec                   = registry.create(id, {}, {});
            auto*               raw = codec.get();
            QPointer<Processor> guard(raw);
            QCOMPARE(raw->accept({QByteArray("data"), 1},
                                 {[&](auto) { codec.reset(); }, [&](auto) { QFAIL("Completion after destruction"); }}),
                     Processor::AcceptResult::Accepted);
            QVERIFY(!guard);
            codec              = registry.create(id, {}, {});
            const auto invalid = invoke(*codec, malformed[index], 0);
            QVERIFY(!invalid.error.isEmpty());
            QVERIFY(invalid.output.isEmpty());
            QCOMPARE(invalid.completed, 1);
            QCOMPARE(codec->accept({QByteArray("data"), 1}, {}), Processor::AcceptResult::Rejected);
            Processor::Limits limits;
            limits.maxBytes = 3;
            codec           = registry.create(id, {}, limits);
            QVERIFY(codec);
            const auto bounded = invoke(*codec, QByteArray("foo"), 1);
            QVERIFY(!bounded.error.isEmpty());
            QVERIFY(bounded.output.isEmpty());
        }
    }
    void composedFamiliesPreserveBidirectionalOrdering()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QVariantList nodes;
        for (const auto& family : QStringList {"length", "checksum", "stuffing", "hex", "base64", "compression"})
        {
            nodes.append(QVariantMap {
                {"name",                        family},
                {"type", "native." + family + "-codec"},
                {"lane",                      "duplex"}
            });
        }
        ProcessorRegistry::Definition definition {
            "families",
            "Family protocol",
            "lineflow",
            {{"channels", 2}, {"nodes", nodes}},
            {},
            {},
            {},
            {}
        };
        QString error;
        QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
        auto runtime = registry.create("families", {}, {}, &error);
        QVERIFY2(runtime, qPrintable(error));
        const QByteArray bytes("payload\0\xff", 9);
        const auto       wire = invoke(*runtime, bytes, 1);
        QVERIFY2(wire.error.isEmpty(), qPrintable(wire.error));
        QCOMPARE(wire.output.size(), 1);
        const auto payload = invoke(*runtime, wire.output.first().bytes, 0);
        QVERIFY2(payload.error.isEmpty(), qPrintable(payload.error));
        QCOMPARE(payload.output.size(), 1);
        QCOMPARE(payload.output.first().bytes, bytes);
    }
    void compressionSharedFormatAndDetection()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        for (const auto& format : QStringList {"zlib", "gzip", "raw"})
        {
            auto codec = registry.create("native.compression-codec",
                                         {
                                             {              "format", format},
                                             {"uncompressAutoDetect",  false}
            },
                                         {});
            QVERIFY(codec);
            const auto encoded = invoke(*codec, QByteArray("hello"), 1);
            QCOMPARE(invoke(*codec, encoded.output.first().bytes, 0).output.first().bytes, QByteArray("hello"));
        }
        auto       gzipEncoder = registry.create("native.compress",
                                                 {
                                                     {"format", "gzip"}
        },
                                                 {});
        const auto gzip        = invoke(*gzipEncoder, QByteArray("gzip peer"), 0).output.first().bytes;
        auto       automatic   = registry.create("native.compression-codec",
                                                 {
                                                     {"format", "zlib"}
        },
                                                 {});
        QCOMPARE(invoke(*automatic, gzip, 0).output.first().bytes, QByteArray("gzip peer"));
        auto       strict   = registry.create("native.compression-codec",
                                              {
                                                  {              "format", "zlib"},
                                                  {"uncompressAutoDetect",  false}
        },
                                              {});
        const auto rejected = invoke(*strict, gzip, 0);
        QVERIFY(!rejected.error.isEmpty());
        QVERIFY(rejected.output.isEmpty());
        QVERIFY(!registry.create("native.uncompress",
                                 {
                                     {"format", "auto"}
        },
                                 {}));
        QVERIFY(!registry.create("native.compression-codec",
                                 {
                                     {"uncompressAutoDetect", "true"}
        },
                                 {}));
    }
};
QTEST_MAIN(ProcessorFamilyTests)
#include "ProcessorFamilyTests.moc"
