#include "../../../src/Core/src/Processing/Nodes/Pacer.h"

#include <QPointer>
#include <QTest>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/SettingsSchema.h>

namespace
{
struct Invocation
{
    Processor::AcceptResult accepted = Processor::AcceptResult::Rejected;
    QList<Processor::Chunk> outputs;
    QList<qint64>           times;
    QElapsedTimer           clock;
    QString                 error;
    int                     completed = 0;

    Invocation()
    {
        clock.start();
    }
};

std::shared_ptr<Invocation> invoke(Processor&          processor,
                                   QByteArray          bytes,
                                   Processor::Boundary boundary = Processor::Boundary::StreamChunk)
{
    auto result      = std::make_shared<Invocation>();
    result->accepted = processor.accept({std::move(bytes), boundary},
                                        {[result](Processor::Chunk chunk) {
                                             result->outputs.append(std::move(chunk));
                                             result->times.append(result->clock.elapsed());
                                         },
                                         [result](QString error) {
                                             ++result->completed;
                                             result->error = std::move(error);
                                         }});
    return result;
}

QByteArray joined(const std::shared_ptr<Invocation>& result)
{
    QByteArray bytes;
    for (const auto& chunk : result->outputs)
    {
        bytes.append(chunk.bytes);
    }
    return bytes;
}
} // namespace

class PacerProcessorTests final : public QObject
{
    Q_OBJECT
private slots:
    void nativeDescriptorAndUnpacedBinary()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QCOMPARE(registry.type("native.pacer")->group, QString("Timing"));
        QString error;
        auto    pacer = registry.create("native.pacer",
                                        {
                                            {"opaque", "kept"}
        },
                                        {},
                                        &error);
        QVERIFY2(pacer, qPrintable(error));
        QCOMPARE(pacer->configuration().value("opaque").toString(), QString("kept"));
        SettingsSchema schema(Pacer::schema());
        QVERIFY(schema.validate(pacer->configuration()).isEmpty());
        const auto data   = QByteArray("A\0B\xff", 4);
        auto       result = invoke(*pacer, data);
        QCOMPARE(result->accepted, Processor::AcceptResult::Accepted);
        QCOMPARE(result->completed, 1);
        QVERIFY(result->error.isEmpty());
        QCOMPARE(joined(result), data);
        QCOMPARE(result->outputs.size(), 1);
        QCOMPARE(result->outputs.first().boundary, Processor::Boundary::StreamChunk);
        result = invoke(*pacer, {});
        QCOMPARE(result->completed, 1);
        QVERIFY(result->outputs.isEmpty());
        result = invoke(*pacer, {}, Processor::Boundary::Packet);
        QCOMPARE(result->completed, 1);
        QCOMPARE(result->outputs.size(), 1);
        QCOMPARE(result->outputs.first().boundary, Processor::Boundary::Packet);
    }

    void bytePacingOwnsInputAndBackpressures()
    {
        auto pacer = Pacer::create(
            {
                {"byteDelayMs", 100}
        },
            {});
        QVERIFY(pacer);
        char       source[] {'A', 0, 'B'};
        const auto result = invoke(*pacer, QByteArray::fromRawData(source, 3));
        QCOMPARE(result->outputs.size(), 1);
        QCOMPARE(result->completed, 0);
        source[1]          = 'x';
        source[2]          = 'z';
        const auto blocked = invoke(*pacer, "late");
        QCOMPARE(blocked->accepted, Processor::AcceptResult::WouldBlock);
        QCOMPARE(blocked->completed, 0);
        QVERIFY(blocked->outputs.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(result->completed, 1, 2000);
        QCOMPARE(joined(result), QByteArray("A\0B", 3));
        QCOMPARE(result->outputs.size(), 3);
        QVERIFY(result->times[1] >= 75);
        QVERIFY(result->times[2] - result->times[1] >= 75);
        QVERIFY(result->error.isEmpty());
        QTest::qWait(130);
        QCOMPARE(result->completed, 1);
    }

    void splitEndingAndFinalByteFence()
    {
        auto pacer = Pacer::create(
            {
                {"lineDelayMs",      140},
                {     "ending", "\\r\\n"}
        },
            {});
        QVERIFY(pacer);
        const auto first = invoke(*pacer, "A\r");
        QCOMPARE(first->completed, 1);
        const auto second = invoke(*pacer, "\nB\r\n");
        QCOMPARE(second->outputs.size(), 1);
        QCOMPARE(joined(second), QByteArray("\n"));
        QTRY_COMPARE_WITH_TIMEOUT(second->completed, 1, 2000);
        QCOMPARE(joined(second), QByteArray("\nB\r\n"));
        QVERIFY(second->times.last() >= 110);
        const auto third = invoke(*pacer, "C");
        QCOMPARE(third->completed, 0);
        QVERIFY(third->outputs.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(third->completed, 1, 2000);
        QVERIFY(third->times.first() >= 100);
        QCOMPARE(joined(third), QByteArray("C"));
    }

    void binaryEscapesAndOverlappingPrefix()
    {
        auto pacer = Pacer::create(
            {
                {"lineDelayMs",            100},
                {     "ending", "\\0\\x41\\\\"}
        },
            {});
        QVERIFY(pacer);
        const auto first = invoke(*pacer, QByteArray("x\0", 2));
        QCOMPARE(first->completed, 1);
        const auto second = invoke(*pacer, "A\\Z");
        QCOMPARE(joined(second), QByteArray("A\\"));
        QTRY_COMPARE_WITH_TIMEOUT(second->completed, 1, 2000);
        QVERIFY(second->times.last() >= 75);
        QCOMPARE(joined(second), QByteArray("A\\Z"));

        pacer = Pacer::create(
            {
                {"lineDelayMs",   100},
                {     "ending", "aab"}
        },
            {});
        QVERIFY(pacer);
        const auto overlap = invoke(*pacer, "aaabZ");
        QCOMPARE(joined(overlap), QByteArray("aaab"));
        QTRY_COMPARE_WITH_TIMEOUT(overlap->completed, 1, 2000);
        QCOMPARE(joined(overlap), QByteArray("aaabZ"));
        QVERIFY(overlap->times.last() >= 75);
    }

    void rateBoundsBurstsAcrossReadsAndIdle()
    {
        auto pacer = Pacer::create(
            {
                {"bytesPerSecond", 20},
                {    "burstBytes",  2}
        },
            {});
        QVERIFY(pacer);
        const auto first = invoke(*pacer, "ab");
        QCOMPARE(first->completed, 1);
        const auto second = invoke(*pacer, "cdef");
        QCOMPARE(second->completed, 0);
        QVERIFY(second->outputs.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(second->completed, 1, 2000);
        QCOMPARE(joined(second), QByteArray("cdef"));
        QCOMPARE(second->outputs.size(), 2);
        QVERIFY(second->times.first() >= 70);
        QVERIFY(second->times.last() >= 165);
        for (const auto& chunk : second->outputs)
        {
            QVERIFY(chunk.bytes.size() <= 2);
        }
        QTest::qWait(250);
        const auto idle = invoke(*pacer, "ghij");
        QCOMPARE(idle->outputs.size(), 1);
        QCOMPARE(joined(idle), QByteArray("gh"));
        QTRY_COMPARE_WITH_TIMEOUT(idle->completed, 1, 2000);
        QVERIFY(idle->times.last() >= 70);
        QCOMPARE(joined(idle), QByteArray("ghij"));
    }

    void packetsStayAtomicIncludingEmptyAndRatePrecharge()
    {
        auto pacer = Pacer::create(
            {
                {"bytesPerSecond", 10},
                {    "burstBytes",  2}
        },
            {});
        QVERIFY(pacer);
        const auto packet = invoke(*pacer, QByteArray("A\0BCD", 5), Processor::Boundary::Packet);
        QVERIFY(packet->outputs.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(packet->completed, 1, 2500);
        QCOMPARE(packet->outputs.size(), 1);
        QCOMPARE(packet->outputs.first().bytes, QByteArray("A\0BCD", 5));
        QCOMPARE(packet->outputs.first().boundary, Processor::Boundary::Packet);
        QVERIFY(packet->times.first() >= 250);
        const auto empty = invoke(*pacer, {}, Processor::Boundary::Packet);
        QCOMPARE(empty->completed, 1);
        QCOMPARE(empty->outputs.size(), 1);
        QVERIFY(empty->outputs.first().bytes.isEmpty());

        pacer = Pacer::create(
            {
                {"byteDelayMs", 10}
        },
            {});
        const auto unsupported = invoke(*pacer, "abc", Processor::Boundary::Packet);
        QCOMPARE(unsupported->accepted, Processor::AcceptResult::Accepted);
        QCOMPARE(unsupported->completed, 1);
        QVERIFY(!unsupported->error.isEmpty());
        QVERIFY(unsupported->outputs.isEmpty());
        QCOMPARE(invoke(*pacer, "late")->accepted, Processor::AcceptResult::Rejected);
    }

    void packetLinesAggregateAfterPacketAndDoNotMatchAcrossPackets()
    {
        auto pacer = Pacer::create(
            {
                {"lineDelayMs",       90},
                {     "ending", "\\r\\n"}
        },
            {});
        QVERIFY(pacer);
        QCOMPARE(invoke(*pacer, "\r", Processor::Boundary::Packet)->completed, 1);
        const auto unmatched = invoke(*pacer, "\nX", Processor::Boundary::Packet);
        QCOMPARE(unmatched->completed, 1);
        const auto lines = invoke(*pacer, "A\r\nB\r\n", Processor::Boundary::Packet);
        QCOMPARE(lines->completed, 1);
        QCOMPARE(lines->outputs.size(), 1);
        const auto next = invoke(*pacer, "C", Processor::Boundary::Packet);
        QCOMPARE(next->completed, 0);
        QTRY_COMPARE_WITH_TIMEOUT(next->completed, 1, 2000);
        QVERIFY(next->times.first() >= 145);
        QCOMPARE(next->outputs.size(), 1);
    }

    void limitsBoundOwnedBytesAndResidentChunks()
    {
        Processor::Limits limits;
        limits.maxBytes  = 3;
        limits.maxChunks = 2;
        auto pacer       = Pacer::create(
            {
                {"byteDelayMs", 1}
        },
            limits);
        QVERIFY(pacer);
        const auto oversized = invoke(*pacer, "abcd");
        QCOMPARE(oversized->accepted, Processor::AcceptResult::Rejected);
        QCOMPARE(oversized->completed, 0);
        const auto expanded = invoke(*pacer, "abc");
        QCOMPARE(expanded->accepted, Processor::AcceptResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(expanded->completed, 1, 1500);
        QVERIFY(expanded->error.isEmpty());
        QCOMPARE(joined(expanded), QByteArray("abc"));
        QCOMPARE(expanded->outputs.size(), 3);

        limits.maxBytes = 10;
        pacer           = Pacer::create(
            {
                {"lineDelayMs", 1}
        },
            limits);
        const auto lines = invoke(*pacer, "a\nb\nc\n");
        QTRY_COMPARE_WITH_TIMEOUT(lines->completed, 1, 1500);
        QVERIFY(lines->error.isEmpty());
        QCOMPARE(joined(lines), QByteArray("a\nb\nc\n"));
        QCOMPARE(lines->outputs.size(), 3);

        limits.maxBytes  = 2;
        limits.maxChunks = 1;
        pacer            = Pacer::create(
            {
                {"bytesPerSecond",     1},
                {    "burstBytes", 65536}
        },
            limits);
        QVERIFY(pacer);
        const auto clamped = invoke(*pacer, "ab");
        QCOMPARE(clamped->completed, 1);
        QCOMPARE(clamped->outputs.size(), 1);
        QCOMPARE(joined(clamped), QByteArray("ab"));
    }

    void cancellationSuppressesCallbacksAndIsQuiescent()
    {
        auto pacer = Pacer::create(
            {
                {"byteDelayMs", 100}
        },
            {});
        const auto result = invoke(*pacer, "abc");
        QCOMPARE(result->outputs.size(), 1);
        int acknowledgements = 0;
        pacer->cancel([&] { ++acknowledgements; });
        pacer->cancel([&] { ++acknowledgements; });
        QCOMPARE(acknowledgements, 2);
        QCOMPARE(invoke(*pacer, "later")->accepted, Processor::AcceptResult::Rejected);
        QTest::qWait(250);
        QCOMPARE(result->outputs.size(), 1);
        QCOMPARE(result->completed, 0);
    }

    void residentChunkBoundAllowsLargePacedInvocation()
    {
        auto pacer = Pacer::create(
            {
                {"byteDelayMs", 1}
        },
            {});
        QVERIFY(pacer);
        const auto result = invoke(*pacer, QByteArray(1024, 'A'));
        QCOMPARE(result->accepted, Processor::AcceptResult::Accepted);
        QCOMPARE(result->outputs.size(), 1);
        QCOMPARE(result->completed, 0);
        bool acknowledged = false;
        pacer->cancel([&] { acknowledged = true; });
        QVERIFY(acknowledged);
        QCOMPARE(result->completed, 0);

        Processor::Limits limits;
        limits.maxChunks = 1;
        pacer            = Pacer::create(
            {
                {"byteDelayMs", 1}
        },
            limits);
        const auto large = invoke(*pacer, QByteArray(65536, 'B'));
        QCOMPARE(large->accepted, Processor::AcceptResult::Accepted);
        QCOMPARE(large->completed, 0);
        QCOMPARE(large->outputs.size(), 1);
        pacer->cancel({});
        QCOMPARE(large->completed, 0);
    }

    void callbacksAllowReentryCancellationAndDeletion()
    {
        auto pacer = Pacer::create(
            {
                {"byteDelayMs", 100}
        },
            {});
        int  outputs      = 0;
        int  completed    = 0;
        bool acknowledged = false;
        QCOMPARE(pacer->accept({"abc", Processor::Boundary::StreamChunk},
                               {[&](Processor::Chunk) {
                                    ++outputs;
                                    QCOMPARE(pacer->accept({"x", Processor::Boundary::StreamChunk}, {}),
                                             Processor::AcceptResult::WouldBlock);
                                    pacer->cancel([&] { acknowledged = true; });
                                },
                                [&](QString) { ++completed; }}),
                 Processor::AcceptResult::Accepted);
        QVERIFY(acknowledged);
        QTest::qWait(150);
        QCOMPARE(outputs, 1);
        QCOMPARE(completed, 0);

        pacer         = Pacer::create({}, {});
        int reentries = 0;
        QCOMPARE(pacer->accept({"a", Processor::Boundary::StreamChunk},
                               {{},
                                [&](QString) {
                                    ++reentries;
                                    QCOMPARE(invoke(*pacer, "b")->completed, 1);
                                }}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(reentries, 1);

        pacer = Pacer::create(
            {
                {"byteDelayMs", 80}
        },
            {});
        QPointer<Processor> guard(pacer.get());
        outputs = 0;
        QCOMPARE(pacer->accept({"ab", Processor::Boundary::StreamChunk},
                               {[&](Processor::Chunk) {
                                    if (++outputs == 2)
                                    {
                                        pacer.reset();
                                    }
                                },
                                [&](QString) { ++completed; }}),
                 Processor::AcceptResult::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 1500);
        QCOMPARE(outputs, 2);
        QCOMPARE(completed, 0);

        pacer = Pacer::create({}, {});
        guard = pacer.get();
        QCOMPARE(pacer->accept({"x", Processor::Boundary::StreamChunk}, {{}, [&](QString) { pacer.reset(); }}),
                 Processor::AcceptResult::Accepted);
        QVERIFY(guard.isNull());
    }

    void validatesConfigurationAndEscapes()
    {
        const QList<QVariantMap> invalid {{{"byteDelayMs", -1}},
                                          {{"byteDelayMs", 60001}},
                                          {{"lineDelayMs", 0.5}},
                                          {{"lineDelayMs", true}},
                                          {{"bytesPerSecond", 1000000001}},
                                          {{"bytesPerSecond", "2"}},
                                          {{"burstBytes", 0}},
                                          {{"burstBytes", 65537}},
                                          {{"ending", 1}},
                                          {{"ending", ""}},
                                          {{"ending", "\\"}},
                                          {{"ending", "\\q"}},
                                          {{"ending", "\\x0"}},
                                          {{"ending", "\\xGG"}},
                                          {{"ending", QString(257, 'A')}},
                                          {{"ending", QString(129, QChar(0x0416))}}};
        for (const auto& config : invalid)
        {
            QString error;
            QVERIFY(!Pacer::create(config, {}, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(error.size() < 256);
        }
        QVERIFY(Pacer::create(
            {
                {"ending", "\\r\\n\\0\\x41\\\\"}
        },
            {}));
        QVERIFY(Pacer::create(
            {
                {"ending", QString(128, QChar(0x0416))}
        },
            {}));
        Processor::Limits limits;
        limits.maxChunks = 0;
        QVERIFY(!Pacer::create({}, limits));
        limits             = {};
        limits.eventBudget = 0;
        QVERIFY(!Pacer::create({}, limits));
    }
};

QTEST_GUILESS_MAIN(PacerProcessorTests)
#include "PacerProcessorTests.moc"
