#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <Termaster/Core/Processing/Lineflow.h>
#include <Termaster/Core/Processing/LineflowEndpoint.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRefactoring.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/SettingsSchema.h>
#include <algorithm>

namespace
{
QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray {};
}
QVariantMap recording(const QString& path)
{
    return {
        {"enabled", true},
        {   "file", path}
    };
}
QVariantMap definition(const QString& id, const QVariantMap& source)
{
    return {
        {    "id",         id},
        {  "name",         id},
        {  "kind", "lineflow"},
        {"source",     source}
    };
}
class Cross final : public Processor
{
public:
    QVariantMap configuration() const override
    {
        return {};
    }
    int inputCount() const override
    {
        return 2;
    }
    int outputCount() const override
    {
        return 2;
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        input.port = 1 - input.port;
        callbacks.output(std::move(input));
        callbacks.completed({});
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> done) override
    {
        if (done)
        {
            done();
        }
    }
};
} // namespace

class LineflowEndpointTests final : public QObject
{
    Q_OBJECT
private slots:
    void descriptorSelectsPacketAndLineFields()
    {
        const SettingsSchema schema(LineflowEndpoint::settingsSchema(2));
        QCOMPARE(schema.discriminator(), QString("recordingMode"));
        const QVariantMap packets {
            {"recordingMode", "packets"}
        };
        const QVariantMap lines {
            {"recordingMode", "lines"}
        };
        QVERIFY(schema.isActive("format", packets));
        QVERIFY(schema.isActive("direction", packets));
        QVERIFY(!schema.isActive("lineEnding", packets));
        QVERIFY(!schema.isActive("format", lines));
        QVERIFY(!schema.isActive("direction", lines));
        QVERIFY(schema.isActive("lineEnding", lines));
        QVERIFY(schema.isActive("trimTrailingSpaces", lines));
        QVERIFY(schema.isActive("flushScreen", lines));
        const SettingsSchema initial(LineflowEndpoint::settingsSchema(2, false));
        QVERIFY(initial.discriminator().isEmpty());
        QVERIFY(!initial.validate(LineflowEndpoint::defaults(2)).size());
    }

    void disabledMetadataAndValidation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("unused.jsonl");
        QString    error;
        auto       processor = LineflowEndpoint::create(
            {
                {   "file",        path},
                {"unknown", "preserved"}
        },
            1,
            {},
            &error);
        QVERIFY2(processor, qPrintable(error));
        QCOMPARE(processor->configuration().value("unknown").toString(), QString("preserved"));
        QCOMPARE(processor->accept({"bytes"}, {}), Processor::AcceptResult::Accepted);
        QVERIFY(!QFile::exists(path));
        processor->cancel({});
        const auto relative = LineflowEndpoint::resolveConfiguration(
            {
                {"enabled",       true},
                {   "file", "relative"}
        },
            {},
            1,
            &error);
        QVERIFY(relative);
        QCOMPARE(relative->value("file").toString(), QString("relative"));
        QVERIFY(!LineflowEndpoint::create(*relative, 1, {}, &error));
        QVERIFY(error.contains("workspace"));
        QVERIFY(!LineflowEndpoint::resolveConfiguration(
            {
                {"backups", 21}
        },
            {},
            1,
            &error));
        QVERIFY(!LineflowEndpoint::resolveConfiguration(
            {
                {"flushEvery", 0}
        },
            {},
            1,
            &error));
        QVERIFY(!LineflowEndpoint::resolveConfiguration(
            {
                {"enabled",     true},
                {   "file",     path},
                { "format", "binary"}
        },
            {},
            2,
            &error));
        QVERIFY(error.contains("{direction}"));
        QVERIFY(LineflowEndpoint::resolveConfiguration(
            {
                {  "enabled",     true},
                {     "file",     path},
                {   "format", "binary"},
                {"direction",     "rx"}
        },
            {},
            2,
            &error));
        QVERIFY(QDir(directory.path()).entryList(QDir::Files).isEmpty());
    }

    void jsonRecordsPreserveWholeBinaryAndEmptyPackets()
    {
        QTemporaryDir directory;
        const auto    path = directory.filePath("packets.jsonl");
        QString       error;
        auto          processor = LineflowEndpoint::create(recording(path), 1, {}, &error);
        QVERIFY2(processor, qPrintable(error));
        const QByteArray           bytes("A\0\xff\n", 4);
        QList<QByteArray>          outputs;
        int                        completions = 0;
        const Processor::Callbacks callbacks {[&](Processor::Chunk output) { outputs.append(output.bytes); },
                                              [&](QString failure) {
                                                  error = failure;
                                                  ++completions;
                                              }};
        QCOMPARE(processor->accept({bytes}, callbacks), Processor::AcceptResult::Accepted);
        QCOMPARE(processor->accept({{}}, callbacks), Processor::AcceptResult::Accepted);
        QCOMPARE(completions, 2);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(outputs, QList<QByteArray>({bytes, {}}));
        const auto lines = read(path).trimmed().split('\n');
        QCOMPARE(lines.size(), 2);
        const auto first = QJsonDocument::fromJson(lines[0]).object();
        QCOMPARE(QByteArray::fromBase64(first.value("bytes").toString().toLatin1()), bytes);
        QCOMPARE(first.value("length").toInt(), 4);
        QCOMPARE(first.value("direction").toString(), QString("stream"));
        QCOMPARE(first.value("index").toString(), QString("1"));
        QVERIFY(first.value("timestamp").toString().endsWith('Z'));
        QCOMPARE(QJsonDocument::fromJson(lines[1]).object().value("length").toInt(), 0);
        processor->cancel({});
    }

    void directionsRotationAndWriterLease()
    {
        QTemporaryDir directory;
        auto          configuration = recording(directory.filePath("capture-{direction}.bin"));
        configuration.insert("format", "binary");
        configuration.insert("maxFileBytes", 3);
        configuration.insert("backups", 1);
        QString error;
        auto    first = LineflowEndpoint::create(configuration, 2, {}, &error);
        QVERIFY2(first, qPrintable(error));
        const auto callbacks = Processor::Callbacks {{}, [&](QString failure) { error = failure; }};
        first->accept({"aa", 0}, callbacks);
        first->accept({"bb", 0}, callbacks);
        first->accept({QByteArray("\0x", 2), 1}, callbacks);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(read(directory.filePath("capture-rx.bin")), QByteArray("bb"));
        QCOMPARE(read(directory.filePath("capture-rx.bin.1")), QByteArray("aa"));
        QCOMPARE(read(directory.filePath("capture-tx.bin")), QByteArray("\0x", 2));
        auto second = LineflowEndpoint::create(configuration, 2, {}, &error);
        QVERIFY(second);
        second->accept({"x", 0}, callbacks);
        QVERIFY(error.contains("lock") || error.contains("in use"));
        second->cancel({});
        bool cancelled = false;
        first->cancel([&] { cancelled = true; });
        QVERIFY(cancelled);
        QCOMPARE(first->accept({"x"}, {}), Processor::AcceptResult::Rejected);
        error.clear();
        auto third = LineflowEndpoint::create(configuration, 2, {}, &error);
        third->accept({"c", 0}, callbacks);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(read(directory.filePath("capture-rx.bin")), QByteArray("bbc"));
        third->cancel({});
    }

    void oversizeAndMissingDirectoryFailWithoutForwarding()
    {
        QTemporaryDir directory;
        auto          configuration = recording(directory.filePath("small.bin"));
        configuration.insert("format", "binary");
        configuration.insert("maxFileBytes", 1);
        QString error;
        int     outputs   = 0;
        auto    processor = LineflowEndpoint::create(configuration, 1, {}, &error);
        processor->accept({"ab"}, {[&](auto) { ++outputs; }, [&](QString failure) { error = failure; }});
        QCOMPARE(outputs, 0);
        QVERIFY(error.contains("size limit"));
        QCOMPARE(read(directory.filePath("small.bin")), QByteArray {});
        processor->cancel({});
        configuration.insert("file", directory.filePath("missing/out.bin"));
        error.clear();
        processor = LineflowEndpoint::create(configuration, 1, {}, &error);
        QVERIFY(processor);
        processor->accept({"x"}, {{}, [&](QString failure) { error = failure; }});
        QVERIFY(error.contains("directory"));
        processor->cancel({});
    }

    void recordingDoesNotOutrunPacedFragmentQueue()
    {
        QTemporaryDir     directory;
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto configuration = recording(directory.filePath("paced.bin"));
        configuration.insert("format", "binary");
        const QVariantList nodes {
            QVariantMap {{"name", "encode"},
                         {"type", "native.hex-encoder"},
                         {"configuration", QVariantMap {{"separator", ""}}}                                },
            QVariantMap {  {"name", "pace"},
                         {"type", "native.pacer"},
                         {"configuration", QVariantMap {{"bytesPerSecond", 1000000000}, {"burstBytes", 1}}}}
        };
        const auto parsed = ProcessorRegistry::parseDefinitions({
            {"flow", definition("flow", {{"nodes", nodes}, {"final", configuration}})}
        });
        QVERIFY(parsed);
        QVERIFY(registry.publishDefinitions("workspace", *parsed));
        Processor::Limits limits;
        limits.maxBytes    = 4096;
        limits.maxChunks   = 4;
        limits.eventBudget = 2;
        QString error;
        auto    runtime = registry.create("flow", {}, limits, &error);
        QVERIFY2(runtime, qPrintable(error));
        const QByteArray input(600, char(0xab));
        QByteArray       output;
        bool             completed = false;
        int              packets   = 0;
        runtime->accept({input},
                        {[&](auto chunk) {
                             QCOMPARE(chunk.bytes.size(), 1);
                             output.append(chunk.bytes);
                             ++packets;
                         },
                         [&](QString failure) {
                             error     = failure;
                             completed = true;
                         }});
        QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(packets, 1200);
        QCOMPARE(output, input.toHex().toUpper());
        QCOMPARE(read(directory.filePath("paced.bin")), output);
        runtime->cancel({});
    }

    void physicalBoundariesFollowCrossPortOutputs()
    {
        QTemporaryDir          directory;
        ProcessorRegistry      registry;
        ProcessorCatalog::Type type {"cross", "Cross", {}, [](const auto&, const auto&, QString*) {
                                         return std::make_unique<Cross>();
                                     }};
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Fixture\ncontent: {type: object}\n";
        type.inputCount  = 2;
        type.outputCount = 2;
        registry.registerType("fixture", std::move(type));
        const auto        top    = directory.filePath("device.jsonl");
        const auto        bottom = directory.filePath("terminal.jsonl");
        const QVariantMap source {
            {"channels",                                                                                    2},
            { "initial",                                                                       recording(top)},
            {   "final",                                                                    recording(bottom)},
            {   "nodes", QVariantList {QVariantMap {{"name", "turn"}, {"type", "cross"}, {"lane", "duplex"}}}}
        };
        QString    error;
        const auto parsed = ProcessorRegistry::parseDefinitions(
            {
                {"flow", definition("flow", source)}
        },
            &error);
        QVERIFY2(parsed, qPrintable(error));
        QVERIFY2(registry.publishDefinitions("workspace", *parsed, &error), qPrintable(error));
        auto runtime = registry.create("flow", {}, {}, &error);
        QVERIFY2(runtime, qPrintable(error));
        bool                    completed = false;
        QList<Processor::Chunk> output;
        runtime->accept({"packet", 0},
                        {[&](auto packet) { output.append(packet); },
                         [&](QString failure) {
                             error     = failure;
                             completed = true;
                         }});
        QTRY_VERIFY(completed);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(output.size(), 1);
        QCOMPARE(output[0].port, 1);
        QCOMPARE(output[0].bytes, QByteArray("packet"));
        const auto lines = read(top).trimmed().split('\n');
        QCOMPARE(lines.size(), 2);
        QCOMPARE(QJsonDocument::fromJson(lines[0]).object().value("direction").toString(), QString("rx"));
        QCOMPARE(QJsonDocument::fromJson(lines[1]).object().value("direction").toString(), QString("tx"));
        QVERIFY(!QFile::exists(bottom));
        runtime->cancel({});
    }

    void propertiesOverridesAndRefactoringKeepBoundaryIdentity()
    {
        ProcessorRegistry registry;
        const QVariantMap child = definition("child",
                                             {
                                                 {  "nodes",              QVariantList {}},
                                                 {"initial", QVariantMap {{"backups", 2}}}
        });
        const QVariantMap root  = definition("root",
                                             {
                                                 {"nodes",
                                                  QVariantList {QVariantMap {{"name", "nested"}, {"type", "child"}}}}
        });
        const QVariantMap definitions {
            {"child", child},
            { "root",  root}
        };
        QString    error;
        const auto parsed = ProcessorRegistry::parseDefinitions(definitions, &error);
        QVERIFY2(parsed, qPrintable(error));
        QVERIFY(registry.publishDefinitions("workspace", *parsed, &error));
        const QVariantMap overrides {
            {"nested/@initial", QVariantMap {{"backups", 4}, {"unknown", "kept"}}}
        };
        const auto properties = registry.properties("root", {}, overrides, &error);
        QVERIFY2(properties, qPrintable(error));
        QCOMPARE(properties->size(), 4);
        const auto found = std::find_if(properties->cbegin(), properties->cend(), [](const auto& value) {
            return value.path == "nested/@initial";
        });
        QVERIFY(found != properties->cend());
        QCOMPARE(found->inheritedConfiguration.value("backups").toInt(), 2);
        QCOMPARE(found->configuration.value("backups").toInt(), 4);
        QVERIFY(!registry.type("@initial"));
        auto prepared = registry.prepare("root", {}, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        QVERIFY2(prepared->instantiate(overrides, &error), qPrintable(error));
        QVERIFY(!prepared->instantiate(
            {
                {"@initial/bogus", QVariantMap {}}
        },
            &error));
        auto finalDefinitions = definitions;
        auto finalRoot        = root;
        finalRoot.insert("source",
                         QVariantMap {
                             {"nodes", QVariantList {QVariantMap {{"name", "renamed"}, {"type", "child"}}}}
        });
        finalDefinitions.insert("root", finalRoot);
        const QVariantMap objects {
            {"terminal",
             QVariantMap {{"settings",
                           QVariantMap {{"processing",
                                         QVariantMap {{"lineflow",
                                                       QVariantMap {{"type", "root"}, {"overrides", overrides}}}}}}}}}
        };
        const auto transformed = ProcessorRefactoring::transform(definitions,
                                                                 finalDefinitions,
                                                                 {
                                                                     {"root", "nested", "renamed"}
        },
                                                                 objects,
                                                                 &error);
        QVERIFY2(transformed, qPrintable(error));
        const auto rewritten = transformed->sessionObjects.value("terminal")
                                   .toMap()
                                   .value("settings")
                                   .toMap()
                                   .value("processing")
                                   .toMap()
                                   .value("lineflow")
                                   .toMap()
                                   .value("overrides")
                                   .toMap();
        QCOMPARE(rewritten.value("renamed/@initial").toMap(), overrides.value("nested/@initial").toMap());
    }

    void renderedLinesRequireTerminalRootSuppressionAndExplicitFlush()
    {
        QTemporaryDir directory;
        auto          configuration = recording(directory.filePath("lines.jsonl"));
        configuration.insert("recordingMode", "lines");
        QString error;
        QVERIFY(LineflowEndpoint::resolveConfiguration(configuration, {}, 2, &error));
        QVERIFY(!LineflowEndpoint::create(configuration, 2, {}, &error));
        QVERIFY(error.contains("terminal root"));
        const QVariantMap source {
            {"channels",               2},
            {   "nodes", QVariantList {}},
            {   "final",   configuration}
        };
        const auto parsed = ProcessorRegistry::parseDefinitions(
            {
                {"flow", definition("flow", source)}
        },
            &error);
        QVERIFY2(parsed, qPrintable(error));
        ProcessorRegistry registry;
        QVERIFY(registry.publishDefinitions("workspace", *parsed, &error));
        QVERIFY(!registry.prepare("flow", {}, {}, &error));
        QVERIFY(error.contains("terminal root"));
        auto prepared = registry.prepare("flow",
                                         {
                                             {"overrides", QVariantMap {{"@final", QVariantMap {{"enabled", false}}}}}
        },
                                         {},
                                         &error);
        QVERIFY2(prepared, qPrintable(error));
        QVERIFY(!prepared->instantiate(
            {
                {"@final", QVariantMap {{"enabled", true}}}
        },
            &error));
        auto initialSource = source;
        initialSource.insert("initial", configuration);
        QVERIFY(!ProcessorRegistry::parseDefinitions(
            {
                {"flow", definition("flow", initialSource)}
        },
            &error));
        configuration.insert("recordingMode", "packets");
        configuration.insert("flushEvery", 1024);
        configuration.insert("format", "binary");
        configuration.insert("direction", "rx");
        auto writer = LineflowEndpoint::create(configuration, 2, {}, &error);
        QVERIFY2(writer, qPrintable(error));
        writer->accept({"rendered line\n", 0}, {{}, [&](QString failure) { error = failure; }});
        QVERIFY2(LineflowEndpoint::flush(*writer, &error), qPrintable(error));
        QCOMPARE(read(directory.filePath("lines.jsonl")), QByteArray("rendered line\n"));
        writer->cancel({});
    }

    void standaloneParityAndPreparationBudgets()
    {
        QTemporaryDir     directory;
        ProcessorRegistry registry;
        const auto        path = directory.filePath("standalone.jsonl");
        const QVariantMap block {
            {     "id",          "flow"},
            {   "name",          "Flow"},
            {  "nodes", QVariantList {}},
            {"initial", recording(path)}
        };
        QString    error;
        const auto prepared = Lineflow::prepare(
            {
                {"flow", block}
        },
            "flow",
            registry,
            {},
            &error);
        QVERIFY2(prepared, qPrintable(error));
        QVERIFY(!QFile::exists(path));
        auto runtime = prepared->instantiate(registry,
                                             {
                                                 {"@initial", QVariantMap {{"enabled", false}}}
        },
                                             &error);
        QVERIFY2(runtime, qPrintable(error));
        bool completed = false;
        runtime->accept({"unchanged"}, {{}, [&](QString failure) {
                                            error     = failure;
                                            completed = true;
                                        }});
        QTRY_VERIFY(completed);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!QFile::exists(path));
        runtime->cancel({});
        const auto parsed = ProcessorRegistry::parseDefinitions(
            {
                {"flow", definition("flow", {{"nodes", QVariantList {}}})}
        },
            &error);
        QVERIFY(parsed);
        QVERIFY(registry.publishDefinitions("workspace", *parsed, &error));
        Processor::Limits limits;
        limits.maxInstances = 2;
        QVERIFY(!registry.prepare("flow", {}, limits, &error));
        limits.maxInstances = 3;
        QVERIFY2(registry.prepare("flow", {}, limits, &error), qPrintable(error));
        auto invalid = block;
        invalid.insert("nodes",
                       QVariantList {
                           QVariantMap {{"name", "@initial"}, {"type", "unknown"}}
        });
        QVERIFY(!Lineflow::prepare(
            {
                {"flow", invalid}
        },
            "flow",
            registry,
            {},
            &error));
    }
};

QTEST_GUILESS_MAIN(LineflowEndpointTests)
#include "LineflowEndpointTests.moc"
