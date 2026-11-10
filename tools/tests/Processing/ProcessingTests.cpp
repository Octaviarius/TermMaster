#include <QTimer>
#include <QtTest>
#include <Termaster/Core/Processing/Lineflow.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <stdexcept>

namespace
{
QVariantMap blocks(QVariantList nodes)
{
    for (qsizetype index = 0; index < nodes.size(); ++index)
    {
        auto node = nodes[index].toMap();
        if (!node.contains("name") || node.value("name").toString() == "leaf")
        {
            node.insert("name", QString("node%1").arg(index));
        }
        nodes[index] = node;
    }

    return {
        {"root", QVariantMap {{"id", "root"}, {"name", "Root"}, {"nodes", nodes}}}
    };
}

QVariantMap native(const QString& type, const QString& name = "leaf")
{
    return {
        {         "name",           name},
        {         "type",           type},
        {"configuration", QVariantMap {}}
    };
}

class DeferredProcessor final : public Processor
{
public:
    explicit DeferredProcessor(bool* acknowledgement) : _acknowledgement(acknowledgement)
    {
    }

    QVariantMap configuration() const override
    {
        return {};
    }

    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        if (_first)
        {
            _first = false;
            return AcceptResult::WouldBlock;
        }

        QTimer::singleShot(0, this, [this, input = std::move(input), callbacks = std::move(callbacks)]() mutable {
            if (_cancelled)
            {
                return;
            }
            if (!input.bytes.isEmpty())
            {
                callbacks.output(input);
                callbacks.output({{}});
            }
            callbacks.completed({});
        });
        return AcceptResult::Accepted;
    }

    void cancel(std::function<void()> callback) override
    {
        _cancelled = true;
        QTimer::singleShot(0, this, [this, callback = std::move(callback)] {
            *_acknowledgement = true;
            callback();
        });
    }

private:
    bool* _acknowledgement;
    bool  _cancelled = false;
    bool  _first     = true;
};
class ConfigProcessor final : public Processor
{
public:
    explicit ConfigProcessor(QVariantMap configuration) : _configuration(std::move(configuration))
    {
    }

    QVariantMap configuration() const override
    {
        return _configuration;
    }
    AcceptResult accept(Chunk, Callbacks) override
    {
        return AcceptResult::Rejected;
    }
    void cancel(std::function<void()> callback) override
    {
        if (callback)
        {
            callback();
        }
    }

private:
    QVariantMap _configuration;
};

class FailingProcessor final : public Processor
{
public:
    explicit FailingProcessor(bool* acknowledged) : _acknowledged(acknowledged)
    {
    }
    QVariantMap configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk, Callbacks callbacks) override
    {
        callbacks.completed(QString::fromLatin1("Synthetic processing failure"));
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> callback) override
    {
        QTimer::singleShot(0, this, [this, callback = std::move(callback)] {
            *_acknowledged = true;
            if (callback)
            {
                callback();
            }
        });
    }

private:
    bool* _acknowledged;
};

class SyncCancelProcessor final : public Processor
{
public:
    explicit SyncCancelProcessor(bool* returned) : _returned(returned)
    {
    }

    QVariantMap configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk, Callbacks callbacks) override
    {
        if (callbacks.completed)
        {
            callbacks.completed({});
        }
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> callback) override
    {
        callback();
        *_returned = true;
    }

private:
    bool* _returned;
};
} // namespace
class ProcessingTests : public QObject
{
    Q_OBJECT
private slots:
    void timedFramerContinuesAfterInvocationCompletion()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto framing = native("native.timed-framer");
        framing.insert("configuration",
                       QVariantMap {
                           {"gapMs", 250}
        });
        auto encode = native("native.hex-encoder");
        encode.insert("configuration",
                      QVariantMap {
                          {"separator", ""}
        });
        QString    error;
        const auto plan = Lineflow::prepare(blocks({framing, encode}), "root", registry, {}, &error);
        QVERIFY2(plan, qPrintable(error));
        auto runtime = plan->instantiate(registry, &error);
        QVERIFY2(runtime, qPrintable(error));
        QList<QByteArray> output;
        runtime->setOutputHandler([&](Processor::Chunk chunk) { output.append(chunk.bytes); });
        runtime->setErrorHandler([&](QString message) { error = message; });
        int                        completed = 0;
        const Processor::Callbacks callbacks {[&](Processor::Chunk) {
                                                  QFAIL("Timed frames must use the lifecycle output channel");
                                              },
                                              [&](QString message) {
                                                  QVERIFY2(message.isEmpty(), qPrintable(message));
                                                  ++completed;
                                              }};
        QCOMPARE(runtime->accept({"A"}, callbacks), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 1);
        QCOMPARE(runtime->accept({QByteArray("\0B", 2)}, callbacks), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 2);
        QTRY_COMPARE(output, QList<QByteArray> {"410042"});
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(runtime->accept({"tail"}, callbacks), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 3);
        runtime->cancel({});
        QTest::qWait(300);
        QCOMPARE(output.size(), 1);
    }

    void patternFramerBuildsBinaryOutputAcrossPackets()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto pattern = native("native.pattern-framer");
        pattern.insert("configuration",
                       QVariantMap {
                           {       "pattern", "<(?<header>[A-Z]):(?<payload>[^>]+)>"},
                           {"outputTemplate",  "${payload}abcdef${header}\\0${0}END"}
        });
        QString    error;
        const auto plan = Lineflow::prepare(blocks({pattern, native("native.gate")}), "root", registry, {}, &error);
        QVERIFY2(plan, qPrintable(error));
        auto runtime = plan->instantiate(registry, &error);
        QVERIFY2(runtime, qPrintable(error));
        QList<QByteArray>          output;
        int                        completed = 0;
        const Processor::Callbacks callbacks {[&](Processor::Chunk chunk) { output.append(chunk.bytes); },
                                              [&](QString message) {
                                                  QVERIFY2(message.isEmpty(), qPrintable(message));
                                                  ++completed;
                                              }};
        QCOMPARE(runtime->accept({"noise<H:"}, callbacks), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 1);
        QVERIFY(output.isEmpty());
        QCOMPARE(runtime->accept({QByteArray("A\0B>", 4)}, callbacks), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 2);
        QByteArray expected("A\0B", 3);
        expected.append("abcdefH");
        expected.append('\0');
        expected.append(QByteArray("<H:A\0B>END", 10));
        QCOMPARE(output, QList<QByteArray> {expected});
        runtime->cancel({});
    }

    void timestampFollowsFrameBoundaries()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto timestamp = native("native.timestamp");
        timestamp.insert("configuration",
                         QVariantMap {
                             {  "format",                            "'TIME'"},
                             {"template", R"(\e[90m{datetime}\e[0m {packet})"}
        });
        QString    error;
        const auto plan = Lineflow::prepare(blocks({native("native.framer"), timestamp}), "root", registry, {}, &error);
        QVERIFY2(plan, qPrintable(error));
        auto runtime = plan->instantiate(registry, &error);
        QVERIFY2(runtime, qPrintable(error));
        QList<QByteArray>          output;
        int                        completed = 0;
        const Processor::Callbacks callbacks {[&](Processor::Chunk chunk) { output.append(chunk.bytes); },
                                              [&](QString message) {
                                                  QVERIFY2(message.isEmpty(), qPrintable(message));
                                                  ++completed;
                                              }};
        QCOMPARE(runtime->accept({"fir"}, callbacks), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 1);
        QVERIFY(output.isEmpty());
        QCOMPARE(runtime->accept({"st\rsecond\r"}, callbacks), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 2);
        QCOMPARE(output, QList<QByteArray>({"\x1b[90mTIME\x1b[0m first", "\x1b[90mTIME\x1b[0m second"}));
        runtime->cancel({});
    }

    void nativeStreamsAndIndependentState()
    {
        ProcessorRegistry registry;
        QString           error;
        registerNativeProcessors(registry, "native");
        const auto plan = Lineflow::prepare(blocks({native("native.framer")}), "root", registry, {}, &error);
        QVERIFY2(plan, qPrintable(error));
        auto                    first  = plan->instantiate(registry, &error);
        auto                    second = plan->instantiate(registry, &error);
        QList<Processor::Chunk> output;
        int                     completed = 0;
        auto                    callbacks = [&] {
            return Processor::Callbacks {[&](auto chunk) { output.push_back(chunk); },
                                         [&](QString e) {
                                             QVERIFY2(e.isEmpty(), qPrintable(e));
                                             ++completed;
                                         }};
        };
        QCOMPARE(first->accept({QByteArray("a\0", 2)}, callbacks()), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 1);
        QVERIFY(output.isEmpty());
        QCOMPARE(second->accept({"\r"}, callbacks()), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 2);
        QCOMPARE(output.size(), 1);
        QVERIFY(output[0].bytes.isEmpty());
        first->accept({"b\r\r"}, callbacks());
        QTRY_COMPARE(completed, 3);
        QCOMPARE(output.size(), 3);
        QCOMPARE(output[1].bytes, QByteArray("a\0b", 3));
        QVERIFY(output[2].bytes.isEmpty());
        bool stopped = false;
        first->cancel([&] { stopped = true; });
        QVERIFY(stopped);
        second->cancel({});
    }

    void limitsCountExpansionRatherThanPipelineHops()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        Processor::Limits limits;
        limits.maxBytes    = 4096;
        limits.maxChunks   = 64;
        limits.eventBudget = 2;
        auto document      = blocks({native("native.gate"),
                                     native("native.gate"),
                                     native("native.gate"),
                                     QVariantMap {{"reference", "child"}}});
        document.insert("child",
                        QVariantMap {
                            {   "id",                              "child"},
                            { "name",                              "Child"},
                            {"nodes", QVariantList {native("native.gate")}}
        });
        QString error;
        auto    plan = Lineflow::prepare(document, "root", registry, limits, &error);
        QVERIFY2(plan, qPrintable(error));
        auto             runtime = plan->instantiate(registry);
        const QByteArray input(3072, 'x');
        QByteArray       received;
        bool             completed = false;
        runtime->accept({input},
                        {[&](auto chunk) { received += chunk.bytes; },
                         [&](QString failure) {
                             error     = failure;
                             completed = true;
                         }});
        QTRY_VERIFY(completed);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(received, input);
        runtime->cancel({});

        auto root  = document.value("root").toMap();
        auto nodes = root.value("nodes").toList();
        nodes.prepend(native("native.framer"));
        root.insert("nodes", nodes);
        document.insert("root", root);
        plan = Lineflow::prepare(document, "root", registry, limits, &error);
        QVERIFY2(plan, qPrintable(error));
        runtime     = plan->instantiate(registry);
        int packets = 0;
        completed   = false;
        runtime->accept({QByteArray("x\r").repeated(64)},
                        {[&](auto chunk) {
                             QCOMPARE(chunk.bytes, QByteArray("x"));
                             ++packets;
                         },
                         [&](QString failure) {
                             error     = failure;
                             completed = true;
                         }});
        QTRY_VERIFY(completed);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(packets, 64);
        runtime->cancel({});
    }

    void hexBoundaries()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto                    decoder = registry.create("native.hex-decoder",
                                                          {
                                                              {"accumulate", true}
        },
                                                          {});
        QList<Processor::Chunk> output;
        QString                 error;
        auto                    callbacks = Processor::Callbacks {[&](auto chunk) { output.push_back(chunk); },
                                                                  [&](QString e) { error = e; }};
        decoder->accept({"0"}, callbacks);
        QVERIFY(output.isEmpty());
        decoder->accept({"041"}, callbacks);
        QCOMPARE(output.size(), 1);
        QCOMPARE(output[0].bytes, QByteArray("\0A", 2));
        decoder->accept({{}}, callbacks);
        QCOMPARE(output.size(), 2);
        QVERIFY(output[1].bytes.isEmpty());
        decoder->accept({"x"}, callbacks);
        QVERIFY(!error.isEmpty());
        decoder->cancel({});
    }

    void preparationValidation()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        auto    document = blocks({QVariantMap {{"reference", "child"}}});
        document.insert("child",
                        QVariantMap {
                            {   "id",                              "child"},
                            { "name",                              "Child"},
                            {"nodes", QVariantList {native("native.gate")}}
        });
        auto plan = Lineflow::prepare(document, "root", registry, {}, &error);
        QVERIFY2(plan, qPrintable(error));
        document.insert("child",
                        QVariantMap {
                            {   "id",                                                               "child"},
                            { "name",                                                               "Child"},
                            {"nodes", QVariantList {QVariantMap {{"name", "cycle"}, {"reference", "root"}}}}
        });
        QVERIFY(!Lineflow::prepare(document, "root", registry, {}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(plan->instantiate(registry));
        QVERIFY(!Lineflow::prepare(blocks({native("missing")}), "root", registry, {}, &error));
        Processor::Limits limits;
        limits.maxInstances = 1;
        QVERIFY(!Lineflow::prepare(blocks({native("native.gate"), native("native.gate")}),
                                   "root",
                                   registry,
                                   limits,
                                   &error));
        limits.maxDepth = 1;
        QVERIFY(!Lineflow::prepare(document, "root", registry, limits, &error));
    }

    void registrationRejectsInvalidDefaultsAndRollsBackNativeBatch()
    {
        ProcessorRegistry registry;
        auto              fixture = [] {
            ProcessorCatalog::Type type;
            type.id             = "native.framer";
            type.title          = "Configuration fixture";
            type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Required default fixture
content:
  type: object
  required: [label]
  properties:
    label:
      type: string
)";
            type.create         = [](const QVariantMap& configuration, const Processor::Limits&, QString*) {
                return std::make_unique<ConfigProcessor>(configuration);
            };
            return type;
        };
        auto invalid = fixture();
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("fixture", std::move(invalid)));
        QVERIFY(registry.types().isEmpty());
        auto valid            = fixture();
        valid.settingsSchema += "      default: ready\n";
        registry.registerType("fixture", valid);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("fixture", valid));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registerNativeProcessors(registry, "native"));
        QCOMPARE(registry.types().size(), 1);
        QVERIFY(!registry.type("native.gate"));
        QVERIFY(registry.type("native.framer"));
        auto instance = registry.create("native.framer", {}, {});
        QVERIFY(instance);
        QCOMPARE(instance->configuration().value("label").toString(), QString("ready"));
        instance.reset();
        QTRY_VERIFY(registry.canRemoveOwner("fixture"));
    }

    void stableOccurrenceOverrides()
    {
        ProcessorRegistry      registry;
        ProcessorCatalog::Type type;
        type.id             = "test.config";
        type.title          = "Configuration fixture";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Configuration fixture
content:
  type: object
  required: [label, nested]
  properties:
    label:
      type: string
      default: default-label
    nested:
      type: object
      properties:
        shared:
          type: integer
          default: 7
        private:
          type: integer
          default: 3
)";
        QList<QVariantMap> factorySettings;
        type.create = [&](const QVariantMap& configuration, const Processor::Limits&, QString*) {
            factorySettings.push_back(configuration);
            return std::make_unique<ConfigProcessor>(configuration);
        };
        QString error;
        registry.registerType("fixture", std::move(type));
        const auto left = QVariantMap {
            {     "name",  "left"},
            {"reference", "child"}
        };
        const auto right = QVariantMap {
            {     "name", "right"},
            {"reference", "child"}
        };
        auto document = blocks({left, right});
        auto leaf     = native("test.config", "leaf");
        leaf.insert("configuration",
                    QVariantMap {
                        {  "label",                    "common"},
                        { "nested", QVariantMap {{"shared", 9}}},
                        {"unknown",                        true}
        });
        document.insert("child",
                        QVariantMap {
                            {   "id",             "child"},
                            { "name",             "Child"},
                            {"nodes", QVariantList {leaf}}
        });
        auto plan = Lineflow::prepare(document, "root", registry, {}, &error);
        QVERIFY2(plan, qPrintable(error));
        const auto        common = plan->configuration();
        const QVariantMap overrides {
            { "left/leaf", QVariantMap {{"label", "A"}, {"nested", QVariantMap {{"private", 11}}}}},
            {"right/leaf",                                            QVariantMap {{"label", "B"}}}
        };
        auto first = plan->instantiate(registry, overrides, &error);
        QVERIFY2(first, qPrintable(error));
        auto second = plan->instantiate(registry,
                                        QVariantMap {
                                            {"left/leaf", QVariantMap {{"label", "C"}}}
        },
                                        &error);
        QVERIFY2(second, qPrintable(error));
        auto       effective    = first->configuration().value("effectiveNodes").toMap();
        const auto leftSettings = effective.value("left/leaf").toMap();
        QCOMPARE(leftSettings.value("label").toString(), QString("A"));
        QCOMPARE(leftSettings.value("nested").toMap().value("shared").toInt(), 9);
        QCOMPARE(leftSettings.value("nested").toMap().value("private").toInt(), 11);
        QVERIFY(leftSettings.value("unknown").toBool());
        QCOMPARE(effective.value("right/leaf").toMap().value("label").toString(), QString("B"));
        QCOMPARE(second->configuration()
                     .value("effectiveNodes")
                     .toMap()
                     .value("left/leaf")
                     .toMap()
                     .value("label")
                     .toString(),
                 QString("C"));
        QCOMPARE(factorySettings[0], leftSettings);
        QCOMPARE(factorySettings[1].value("nested").toMap().value("private").toInt(), 3);
        QCOMPARE(plan->configuration(), common);
        QCOMPARE(first->configuration().value("overrides").toMap(), overrides);

        document.insert("root", blocks({right, left}).value("root"));
        auto reordered = Lineflow::prepare(document, "root", registry, {}, &error);
        QVERIFY2(reordered, qPrintable(error));
        auto third = reordered->instantiate(registry, overrides, &error);
        QVERIFY2(third, qPrintable(error));
        QCOMPARE(third->configuration().value("effectiveNodes").toMap(), effective);
        QVERIFY(!plan->instantiate(registry,
                                   QVariantMap {
                                       {"unknown/leaf", QVariantMap {}}
        },
                                   &error));
        QVERIFY(!plan->instantiate(registry,
                                   QVariantMap {
                                       {"left/leaf", 1}
        },
                                   &error));
        QVERIFY(!plan->instantiate(registry,
                                   QVariantMap {
                                       {"left/leaf", QVariantMap {{"label", 1}}}
        },
                                   &error));
        auto renamedLeft = left;
        renamedLeft.insert("name", QString::fromUtf8("Приём данных"));
        document.insert("root", blocks({renamedLeft, right}).value("root"));
        auto renamedPlan = Lineflow::prepare(document, "root", registry, {}, &error);
        QVERIFY2(renamedPlan, qPrintable(error));
        QVERIFY(!renamedPlan->instantiate(registry, overrides, &error));
        auto renamedRuntime = renamedPlan->instantiate(registry,
                                                       QVariantMap {
                                                           {QString::fromUtf8("Приём данных/leaf"),
                                                            QVariantMap {{"label", "Renamed"}}}
        },
                                                       &error);
        QVERIFY2(renamedRuntime, qPrintable(error));
        renamedRuntime->cancel({});
        renamedRuntime.reset();
        document.insert("root", blocks({left, left}).value("root"));
        QVERIFY(!Lineflow::prepare(document, "root", registry, {}, &error));
        auto invalid = native("test.config", "bad/path");
        QVERIFY(!Lineflow::prepare(blocks({invalid}), "root", registry, {}, &error));
        for (const auto& name : QStringList {"", " padded ", QString(257, 'x'), QString(QChar::Null)})
        {
            invalid = native("test.config", name);
            QVERIFY(!Lineflow::prepare(blocks({invalid}), "root", registry, {}, &error));
        }
        first->cancel({});
        second->cancel({});
        third->cancel({});
        first.reset();
        second.reset();
        third.reset();
        QTRY_VERIFY(registry.canRemoveOwner("fixture"));
    }

    void nestedFailureNamesOccurrenceAndWaitsForQuiescence()
    {
        ProcessorRegistry      registry;
        bool                   acknowledged = false;
        ProcessorCatalog::Type type;
        type.id    = "test.failure";
        type.title = "Failure fixture";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Failure fixture\ncontent: {type: object, properties: {}}\n";
        type.create = [&](const QVariantMap&, const Processor::Limits&, QString*) {
            return std::make_unique<FailingProcessor>(&acknowledged);
        };
        registry.registerType("failure-owner", std::move(type));
        const QVariantList childNodes {native("test.failure", "decode")};
        const QVariantList parentNodes {
            QVariantMap {{"name", "branch"}, {"reference", "inner"}}
        };
        ProcessorRegistry::Definition inner {"inner", "Inner", "lineflow", {{"nodes", childNodes}}, {}, {}, {}, {}};
        ProcessorRegistry::Definition root {"outer", "Outer", "lineflow", {{"nodes", parentNodes}}, {}, {}, {}, {}};
        QString                       error;
        QVERIFY2(registry.publishDefinitions("workspace", {inner, root}, &error), qPrintable(error));
        auto prepared = registry.prepare("outer", {}, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        auto runtime = prepared->instantiate({}, &error);
        QVERIFY2(runtime, qPrintable(error));
        QString          failure;
        bool             completed = false;
        const QByteArray payload("secret-wire-content-92751");
        QCOMPARE(runtime->accept({payload},
                                 {{},
                                  [&](QString message) {
                                      QVERIFY(acknowledged);
                                      failure   = std::move(message);
                                      completed = true;
                                  }}),
                 Processor::AcceptResult::Accepted);
        QVERIFY(!completed);
        QTRY_VERIFY(completed);
        QVERIFY2(failure.contains("branch/decode"), qPrintable(failure));
        QVERIFY(failure.contains("Synthetic processing failure"));
        QVERIFY(!failure.contains(QString::fromLatin1(payload)));
        bool stopped = false;
        runtime->cancel([&] { stopped = true; });
        QVERIFY(stopped);
        QCOMPARE(runtime->accept({payload}, {}), Processor::AcceptResult::Rejected);
        runtime.reset();
        prepared.reset();
        QTRY_VERIFY(registry.canRemoveOwner("failure-owner"));

        // The standalone flattened preparation retains the same complete occurrence address.
        const QVariantMap flattened {
            {"inner",  QVariantMap {{"id", "inner"}, {"name", "Inner"}, {"nodes", childNodes}}},
            {"outer", QVariantMap {{"id", "outer"}, {"name", "Outer"}, {"nodes", parentNodes}}}
        };
        const auto plan = Lineflow::prepare(flattened, "outer", registry, {}, &error);
        QVERIFY2(plan, qPrintable(error));
        auto flatRuntime = plan->instantiate(registry, &error);
        QVERIFY2(flatRuntime, qPrintable(error));
        completed    = false;
        acknowledged = false;
        flatRuntime->accept({payload}, {{}, [&](QString message) {
                                            QVERIFY(acknowledged);
                                            failure   = std::move(message);
                                            completed = true;
                                        }});
        QTRY_VERIFY(completed);
        QVERIFY2(failure.contains("branch/decode"), qPrintable(failure));
        QVERIFY(!failure.contains(QString::fromLatin1(payload)));
        flatRuntime.reset();
        QTRY_VERIFY(registry.canRemoveOwner("failure-owner"));
    }

    void savedPacerStreamsThroughBusyStagesBeforeCompletion()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto first = native("native.pacer", "first");
        first.insert("configuration",
                     QVariantMap {
                         {"byteDelayMs", 90}
        });
        auto second = native("native.pacer", "second");
        second.insert("configuration",
                      QVariantMap {
                          {"byteDelayMs", 130}
        });
        ProcessorRegistry::Definition definition {"paced-chain",
                                                  "Paced chain",
                                                  "lineflow",
                                                  {{"nodes",
                                                    QVariantList {first, second, native("native.bypass", "output")}}},
                                                  {},
                                                  {},
                                                  {},
                                                  {}};
        QString                       error;
        QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
        auto prepared = registry.prepare("paced-chain", {}, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        auto runtime = prepared->instantiate({}, &error);
        QVERIFY2(runtime, qPrintable(error));
        QByteArray    output;
        QList<qint64> times;
        QElapsedTimer clock;
        clock.start();
        int completed = 0;
        QCOMPARE(runtime->accept({QByteArray("A\0B", 3)},
                                 {[&](Processor::Chunk chunk) {
                                      output.append(chunk.bytes);
                                      times.append(clock.elapsed());
                                      QCOMPARE(runtime->accept({"reentry"}, {}), Processor::AcceptResult::WouldBlock);
                                  },
                                  [&](QString message) {
                                      QVERIFY2(message.isEmpty(), qPrintable(message));
                                      ++completed;
                                  }}),
                 Processor::AcceptResult::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(!output.isEmpty(), 1500);
        QCOMPARE(output, QByteArray("A"));
        QCOMPARE(completed, 0);
        QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 3000);
        QCOMPARE(output, QByteArray("A\0B", 3));
        QCOMPARE(times.size(), 3);
        QVERIFY(times[1] - times[0] >= 95);
        QVERIFY(times[2] - times[1] >= 95);
        runtime->cancel({});
    }

    void savedHexPacerExceedsTotalChunkCountWithBoundedResidentQueue()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto encoder = native("native.hex-encoder", "encode");
        encoder.insert("configuration",
                       QVariantMap {
                           {"separator", ""}
        });
        auto pacer = native("native.pacer", "pace");
        pacer.insert("configuration",
                     QVariantMap {
                         {"bytesPerSecond", 1000000000},
                         {    "burstBytes",          1}
        });
        ProcessorRegistry::Definition definition {"hex-paced",
                                                  "HEX paced",
                                                  "lineflow",
                                                  {{"nodes", QVariantList {encoder, pacer}}},
                                                  {},
                                                  {},
                                                  {},
                                                  {}};
        QString                       error;
        QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
        Processor::Limits limits;
        limits.maxBytes    = 4096;
        limits.maxChunks   = 4;
        // Two final fragments per event turn keep the resident queue bounded without
        // requiring 1200 timer-resolution-dependent millisecond delays.
        limits.eventBudget = 2;
        auto prepared      = registry.prepare("hex-paced", {}, limits, &error);
        QVERIFY2(prepared, qPrintable(error));
        auto runtime = prepared->instantiate({}, &error);
        QVERIFY2(runtime, qPrintable(error));
        QByteArray input(600, char(0xab));
        QByteArray output;
        int        emissions = 0;
        int        completed = 0;
        QString    failure;
        runtime->accept({input},
                        {[&](Processor::Chunk chunk) {
                             QCOMPARE(completed, 0);
                             QCOMPARE(chunk.bytes.size(), 1);
                             output.append(chunk.bytes);
                             ++emissions;
                         },
                         [&](QString message) {
                             failure = std::move(message);
                             ++completed;
                         }});
        QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 5000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(output, input.toHex().toUpper());
        QCOMPARE(emissions, 1200);

        output.clear();
        bool acknowledged = false;
        runtime->accept({input},
                        {[&](Processor::Chunk chunk) {
                             output.append(chunk.bytes);
                             runtime->cancel([&] { acknowledged = true; });
                         },
                         [&](QString) { ++completed; }});
        QTRY_VERIFY_WITH_TIMEOUT(acknowledged, 1500);
        QCOMPARE(output.size(), 1);
        const auto prefix = output;
        QTest::qWait(100);
        QCOMPARE(output, prefix);
        QCOMPARE(completed, 1);
        QCOMPARE(runtime->accept({input}, {}), Processor::AcceptResult::Rejected);
    }

    void savedPacerPreservesPacketErrorsAndLineFence()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto pacer = native("native.pacer", "pace");
        pacer.insert("configuration",
                     QVariantMap {
                         {"lineDelayMs", 100}
        });
        ProcessorRegistry::Definition definition {"packet-paced",
                                                  "Packet paced",
                                                  "lineflow",
                                                  {{"nodes", QVariantList {pacer, native("native.bypass", "output")}}},
                                                  {},
                                                  {},
                                                  {},
                                                  {}};
        QString                       error;
        QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
        auto runtime = registry.create("packet-paced", {}, {}, &error);
        QVERIFY2(runtime, qPrintable(error));
        QList<Processor::Chunk> output;
        QList<qint64>           packetTimes;
        QElapsedTimer           packetClock;
        packetClock.start();
        int                  completed = 0;
        Processor::Callbacks callbacks {[&](Processor::Chunk chunk) {
                                            output.append(std::move(chunk));
                                            packetTimes.append(packetClock.elapsed());
                                        },
                                        [&](QString message) {
                                            QVERIFY2(message.isEmpty(), qPrintable(message));
                                            ++completed;
                                        }};
        runtime->accept({"A\nB\n"}, callbacks);
        QTRY_COMPARE(completed, 1);
        QCOMPARE(output.size(), 2);
        QCOMPARE(output[0].bytes, QByteArray("A\n"));
        QCOMPARE(output[1].bytes, QByteArray("B\n"));
        QVERIFY(packetTimes[1] - packetTimes[0] >= 80);
        runtime->accept({{}}, callbacks);
        QTest::qWait(50);
        QCOMPARE(completed, 1);
        QTRY_COMPARE_WITH_TIMEOUT(completed, 2, 2000);
        QVERIFY(packetTimes[2] - packetTimes[1] >= 80);
        QCOMPARE(output.size(), 3);
        QVERIFY(output.last().bytes.isEmpty());
        runtime->cancel({});

        auto node = native("native.pacer", "pace");
        node.insert("configuration",
                    QVariantMap {
                        {"byteDelayMs", 10}
        });
        definition.source = {
            {"nodes", QVariantList {node}}
        };
        QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
        runtime = registry.create("packet-paced", {}, {}, &error);
        QVERIFY2(runtime, qPrintable(error));
        output.clear();
        bool completedBytes = false;
        runtime->accept({"packet"},
                        {[&](Processor::Chunk chunk) {
                             QCOMPARE(chunk.bytes.size(), 1);
                             output.append(std::move(chunk));
                         },
                         [&](QString message) {
                             QVERIFY2(message.isEmpty(), qPrintable(message));
                             completedBytes = true;
                         }});
        QTRY_VERIFY(completedBytes);
        QByteArray combined;
        for (const auto& chunk : output)
        {
            combined.append(chunk.bytes);
        }
        QCOMPARE(combined, QByteArray("packet"));
    }

    void nativeRejectsReentrantAcceptance()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto processor = registry.create("native.framer", {}, {});
        int  outputs   = 0;
        bool completed = false;
        processor->accept({"a\rb\r"},
                          {[&](auto) {
                               ++outputs;
                               QCOMPARE(processor->accept({"nested"}, {}), Processor::AcceptResult::WouldBlock);
                           },
                           [&](QString error) {
                               QVERIFY(error.isEmpty());
                               completed = true;
                           }});
        QCOMPARE(outputs, 2);
        QVERIFY(completed);
        completed = false;
        QCOMPARE(processor->accept({"partial"},
                                   {{},
                                    [&](QString error) {
                                        QVERIFY(error.isEmpty());
                                        completed = true;
                                    }}),
                 Processor::AcceptResult::Accepted);
        QVERIFY(completed);
        processor->cancel({});
    }

    void synchronousCancellationRetainsFactoryLease()
    {
        ProcessorRegistry      registry;
        bool                   returned = false;
        ProcessorCatalog::Type type;
        type.id    = "test.sync-cancel";
        type.title = "Synchronous cancellation fixture";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.create = [&](const QVariantMap&, const Processor::Limits&, QString*) {
            return std::make_unique<SyncCancelProcessor>(&returned);
        };
        registry.registerType("fixture", std::move(type));
        auto instance = registry.create("test.sync-cancel", {}, {});
        QVERIFY(instance);
        instance.reset();
        QVERIFY(returned);
        QVERIFY(!registry.canRemoveOwner("fixture"));
        QTRY_VERIFY(registry.canRemoveOwner("fixture"));
        QVERIFY(registry.removeOwner("fixture"));
    }

    void deferredCompletionBoundsAndCancellation()
    {
        ProcessorRegistry      registry;
        bool                   acknowledged = false;
        ProcessorCatalog::Type type;
        type.id    = "test.deferred";
        type.title = "Deferred fixture";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.create = [&](const QVariantMap&, const Processor::Limits&, QString*) {
            return std::make_unique<DeferredProcessor>(&acknowledged);
        };
        registry.registerType("fixture", std::move(type));
        auto plan = Lineflow::prepare(blocks({native("test.deferred")}), "root", registry, {});
        QVERIFY(plan);
        auto runtime   = plan->instantiate(registry);
        int  completed = 0;
        int  outputs   = 0;
        auto callbacks = Processor::Callbacks {[&](auto) { ++outputs; },
                                               [&](QString e) {
                                                   QVERIFY(e.isEmpty());
                                                   ++completed;
                                               }};
        runtime->accept({{}}, callbacks);
        QTRY_COMPARE(completed, 1);
        QCOMPARE(outputs, 0);
        runtime->accept({"data"}, callbacks);
        QTRY_COMPARE(completed, 2);
        QCOMPARE(outputs, 2);
        runtime->accept({"pending"}, callbacks);
        bool stopped = false;
        runtime->cancel([&] {
            QVERIFY(acknowledged);
            stopped = true;
        });
        QVERIFY(!stopped);
        QTRY_VERIFY(stopped);
        QCOMPARE(completed, 2);
        QCOMPARE(runtime->accept({"later"}, callbacks), Processor::AcceptResult::Rejected);
        Processor::Limits limits;
        limits.maxChunks = 1;
        plan             = Lineflow::prepare(blocks({native("test.deferred")}), "root", registry, limits);
        runtime          = plan->instantiate(registry);
        QString failure;
        bool    done = false;
        runtime->accept({"a"}, {{}, [&](QString error) {
                                    failure = error;
                                    done    = true;
                                }});
        QTRY_VERIFY(done);
        QVERIFY(!failure.isEmpty());
        QVERIFY2(failure.contains("node0"), qPrintable(failure));
        runtime.reset();
        QTRY_VERIFY(registry.canRemoveOwner("fixture"));
    }
};
QTEST_GUILESS_MAIN(ProcessingTests)
#include "ProcessingTests.moc"
