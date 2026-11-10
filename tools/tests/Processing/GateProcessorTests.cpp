#include "../../../src/Core/src/Processing/Nodes/Gate.h"

#include <QTest>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/SettingsSchema.h>

namespace
{
struct Invocation
{
    Processor::AcceptResult accepted = Processor::AcceptResult::Rejected;
    QList<Processor::Chunk> output;
    int                     completed = 0;
    QString                 error;
};
Invocation invoke(Processor& gate, Processor::Chunk chunk)
{
    Invocation result;
    result.accepted = gate.accept(std::move(chunk),
                                  {[&](Processor::Chunk output) { result.output.append(std::move(output)); },
                                   [&](QString error) {
                                       ++result.completed;
                                       result.error = std::move(error);
                                   }});
    return result;
}
} // namespace

class GateProcessorTests final : public QObject
{
    Q_OBJECT
private slots:
    void bypassAndStubAreNativeControlEntries()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        QCOMPARE(registry.type("native.bypass")->origin, QString("native"));
        QCOMPARE(registry.type("native.stub")->group, QString("Control"));
        QCOMPARE(registry.type("native.gate")->origin, QString("native"));
        for (const auto& id : {QString("native.bypass"), QString("native.stub")})
        {
            auto processor = registry.create(id, {}, {}, &error);
            QVERIFY2(processor, qPrintable(error));
            for (int iteration = 0; iteration < 2; ++iteration)
            {
                for (const auto& bytes : {QByteArray("A\0B", 3), QByteArray {}})
                {
                    const auto result = invoke(*processor, {bytes});
                    QCOMPARE(result.accepted, Processor::AcceptResult::Accepted);
                    QCOMPARE(result.completed, 1);
                    QVERIFY(result.error.isEmpty());
                    if (id == "native.stub")
                    {
                        QVERIFY(result.output.isEmpty());
                    }
                    else
                    {
                        QCOMPARE(result.output.size(), 1);
                        QCOMPARE(result.output.first().bytes, bytes);
                    }
                }
            }
            bool stopped = false;
            processor->cancel([&] { stopped = true; });
            QTRY_VERIFY(stopped);
            QCOMPARE(invoke(*processor, {"late"}).accepted, Processor::AcceptResult::Rejected);
        }
    }

    void enabledDefaultsOwnBinaryAndPreserveBoundaries()
    {
        auto gate = Gate::create({}, {});
        QVERIFY(gate);
        QCOMPARE(gate->configuration().value("enabled").typeId(), int(QMetaType::Bool));
        QCOMPARE(gate->configuration().value("enabled").toBool(), true);
        SettingsSchema schema(Gate::schema());
        QVERIFY(schema.validate(gate->configuration()).isEmpty());
        char source[] {'a', 0, char(255)};
        auto result = invoke(*gate, {QByteArray::fromRawData(source, 3)});
        QCOMPARE(result.accepted, Processor::AcceptResult::Accepted);
        QCOMPARE(result.completed, 1);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.output.size(), 1);
        source[0] = 'z';
        const char expected[] {'a', 0, char(255)};
        QCOMPARE(result.output[0].bytes, QByteArray(expected, 3));

        for (int iteration = 0; iteration < 2; ++iteration)
        {
            result = invoke(*gate, {{}});
            QCOMPARE(result.completed, 1);
            QCOMPARE(result.output.size(), 1);
            QVERIFY(result.output[0].bytes.isEmpty());
        }
    }

    void disabledCompletesEveryInputWithoutOutputOrBackpressure()
    {
        auto gate = Gate::create(
            {
                {"enabled", false}
        },
            {});
        QVERIFY(gate);
        for (int index = 0; index < 20; ++index)
        {
            const auto result = invoke(*gate, {QByteArray(index, 'x')});
            QCOMPARE(result.accepted, Processor::AcceptResult::Accepted);
            QCOMPARE(result.completed, 1);
            QVERIFY(result.output.isEmpty());
            QVERIFY(result.error.isEmpty());
        }
        const auto empty = invoke(*gate, {{}});
        QCOMPARE(empty.accepted, Processor::AcceptResult::Accepted);
        QCOMPARE(empty.completed, 1);
        QVERIFY(empty.output.isEmpty());
    }

    void callbackReentryAndCancellation()
    {
        auto                    gate      = Gate::create({}, {});
        Processor::AcceptResult nested    = Processor::AcceptResult::Rejected;
        int                     completed = 0;
        QCOMPARE(gate->accept({"a"},
                              {[&](Processor::Chunk) { nested = gate->accept({"nested"}, {}); },
                               [&](QString error) {
                                   QVERIFY(error.isEmpty());
                                   ++completed;
                               }}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(nested, Processor::AcceptResult::WouldBlock);
        QCOMPARE(completed, 1);
        int acknowledgements = 0;
        QCOMPARE(gate->accept({"cancel"},
                              {[&](Processor::Chunk) { gate->cancel([&] { ++acknowledgements; }); },
                               [&](QString) { ++completed; }}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(completed, 1);
        QCOMPARE(acknowledgements, 1);
        gate->cancel([&] { ++acknowledgements; });
        QCOMPARE(acknowledgements, 2);
        const auto stopped = invoke(*gate, {"late"});
        QCOMPARE(stopped.accepted, Processor::AcceptResult::Rejected);
        QCOMPARE(stopped.completed, 0);
        QVERIFY(stopped.output.isEmpty());

        auto disabled = Gate::create(
            {
                {"enabled", false}
        },
            {});
        int nextCompletions = 0;
        disabled->accept({"first"}, {{}, [&](QString error) {
                                         QVERIFY(error.isEmpty());
                                         const auto next = invoke(*disabled, {"second"});
                                         QCOMPARE(next.accepted, Processor::AcceptResult::Accepted);
                                         QCOMPARE(next.completed, 1);
                                         ++nextCompletions;
                                     }});
        QCOMPARE(nextCompletions, 1);
    }

    void deletionInsideCallbacks()
    {
        auto       gate      = Gate::create({}, {});
        auto*      raw       = gate.get();
        int        outputs   = 0;
        int        completed = 0;
        const auto result    = raw->accept({"bytes"},
                                           {[&](Processor::Chunk) {
                                             ++outputs;
                                             gate.reset();
                                            },
                                            [&](QString) { ++completed; }});
        QCOMPARE(result, Processor::AcceptResult::Accepted);
        QVERIFY(!gate);
        QCOMPARE(outputs, 1);
        QCOMPARE(completed, 0);
        auto disabled = Gate::create(
            {
                {"enabled", false}
        },
            {});
        raw = disabled.get();
        QCOMPARE(raw->accept({"bytes"},
                             {{},
                              [&](QString error) {
                                  QVERIFY(error.isEmpty());
                                  ++completed;
                                  disabled.reset();
                              }}),
                 Processor::AcceptResult::Accepted);
        QVERIFY(!disabled);
        QCOMPARE(completed, 1);
    }

    void limitsAndConfigurationValidation()
    {
        QString error;
        for (const QVariant& value : {QVariant(1), QVariant(QString("true")), QVariant(QVariantMap {})})
        {
            QVERIFY(!Gate::create(
                {
                    {"enabled", value}
            },
                {},
                &error));
            QVERIFY(!error.isEmpty());
        }
        for (int index = 0; index < 5; ++index)
        {
            Processor::Limits limits;
            if (index == 0)
            {
                limits.maxBytes = 0;
            }
            if (index == 1)
            {
                limits.maxChunks = 0;
            }
            if (index == 2)
            {
                limits.maxDepth = 0;
            }
            if (index == 3)
            {
                limits.maxInstances = 0;
            }
            if (index == 4)
            {
                limits.eventBudget = 0;
            }
            QVERIFY(!Gate::create({}, limits, &error));
            QVERIFY(!error.isEmpty());
        }
        Processor::Limits limits;
        limits.maxBytes  = 2;
        limits.maxChunks = 1;
        for (const bool enabled : {true, false})
        {
            auto gate = Gate::create(
                {
                    {"enabled",    enabled},
                    {"unknown", "retained"}
            },
                limits,
                &error);
            QVERIFY(gate);
            QVERIFY(error.isEmpty());
            QCOMPARE(gate->configuration().value("unknown").toString(), "retained");
            const auto rejected = invoke(*gate, {"abc"});
            QCOMPARE(rejected.accepted, Processor::AcceptResult::Rejected);
            QCOMPARE(rejected.completed, 0);
            const auto accepted = invoke(*gate, {"ab"});
            QCOMPARE(accepted.accepted, Processor::AcceptResult::Accepted);
            QCOMPARE(accepted.completed, 1);
            QCOMPARE(accepted.output.size(), enabled ? 1 : 0);
        }
    }

    void disabledStageCompletesLineflowAndPrivateEnableIsIndependent()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        const QVariantList nodes {
            QVariantMap {{"name", "incoming"}, {"type", "native.gate"}},
            QVariantMap {{"name", "blocked"},
                         {"type", "native.gate"},
                         {"configuration", QVariantMap {{"enabled", false}}}},
            QVariantMap {{"name", "encode"}, {"type", "native.hex-encoder"}}
        };
        ProcessorRegistry::Definition
                root {"pipeline", "Gate pipeline", "lineflow", {{"nodes", nodes}}, {}, {}, {}, {}};
        QString error;
        QVERIFY2(registry.publishDefinitions("workspace", {root}, &error), qPrintable(error));
        auto prepared = registry.prepare("pipeline", {}, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        auto blocked = prepared->instantiate({}, &error);
        QVERIFY2(blocked, qPrintable(error));
        int outputs     = 0;
        int completions = 0;
        for (int index = 0; index < 5; ++index)
        {
            QCOMPARE(blocked->accept({"AB"},
                                     {[&](Processor::Chunk) { ++outputs; },
                                      [&](QString message) {
                                          QVERIFY(message.isEmpty());
                                          ++completions;
                                      }}),
                     Processor::AcceptResult::Accepted);
            QTRY_COMPARE(completions, index + 1);
        }
        QCOMPARE(outputs, 0);
        auto enabled = prepared->instantiate(
            {
                {"blocked", QVariantMap {{"enabled", true}}}
        },
            &error);
        QVERIFY2(enabled, qPrintable(error));
        QByteArray encoded;
        bool       done = false;
        QCOMPARE(enabled->accept({"AB"},
                                 {[&](Processor::Chunk output) { encoded.append(output.bytes); },
                                  [&](QString message) {
                                      QVERIFY(message.isEmpty());
                                      done = true;
                                  }}),
                 Processor::AcceptResult::Accepted);
        QTRY_VERIFY(done);
        QCOMPARE(encoded, QByteArray("41 42"));
        blocked.reset();
        enabled.reset();
        prepared.reset();
        QTRY_VERIFY(registry.canRemoveOwner("native"));
    }
};
QTEST_GUILESS_MAIN(GateProcessorTests)
#include "GateProcessorTests.moc"
