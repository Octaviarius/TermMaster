#include "../../../src/Core/src/Processing/Nodes/Framer.h"

#include <QTest>
#include <Termaster/Core/SettingsSchema.h>

namespace
{
struct Invocation
{
    Processor::AcceptResult accepted = Processor::AcceptResult::Rejected;
    QList<Processor::Chunk> outputs;
    int                     completions = 0;
    QString                 error;
};
Invocation invoke(Processor& processor, QByteArray bytes)
{
    Invocation result;
    result.accepted = processor.accept({std::move(bytes)},
                                       {[&](Processor::Chunk chunk) { result.outputs.append(std::move(chunk)); },
                                        [&](QString error) {
                                            ++result.completions;
                                            result.error = std::move(error);
                                        }});
    return result;
}
QList<QByteArray> payloads(const QList<Processor::Chunk>& chunks)
{
    QList<QByteArray> result;
    for (const auto& chunk : chunks)
    {
        result.append(chunk.bytes);
    }
    return result;
}
} // namespace

class FramerProcessorTests final : public QObject
{
    Q_OBJECT
private slots:
    void descriptorDefaultsAndIndependentInstances()
    {
        SettingsSchema schema(Framer::schema());
        QCOMPARE(schema.fields().size(), 4);
        auto first  = Framer::create({}, {});
        auto second = Framer::create({}, {});
        QVERIFY(first);
        QVERIFY(second);
        QCOMPARE(first->configuration().value("ending").toString(), QString("\\r"));
        QCOMPARE(first->configuration().value("frameSize").toInt(), 0);
        QVERIFY(schema.validate(first->configuration()).isEmpty());
        QCOMPARE(invoke(*first, "partial").completions, 1);
        QCOMPARE(payloads(invoke(*second, "b\r").outputs), QList<QByteArray>({"b"}));
        QCOMPARE(payloads(invoke(*first, "a\r").outputs), QList<QByteArray>({"partiala"}));
        // Incoming transport packets are ignored by the default boundary policy.
        QVERIFY(invoke(*second, {}).outputs.isEmpty());
    }

    void delimitersAtEverySplit_data()
    {
        QTest::addColumn<QString>("ending");
        QTest::addColumn<QByteArray>("input");
        QTest::addColumn<QList<QByteArray>>("expected");
        QTest::newRow("crlf") << QString("\\r\\n") << QByteArray("a\r\nb\r\n") << QList<QByteArray>({"a", "b"});
        QTest::newRow("binary") << QString("\\0\\xFF")
                                << QByteArray("a\0\xff"
                                              "b\0\xff",
                                              6)
                                << QList<QByteArray>({"a", "b"});
        QTest::newRow("overlap") << QString("aab") << QByteArray("aaaaabaaaab") << QList<QByteArray>({"aaa", "aa"});
        QTest::newRow("empty") << QString("abab") << QByteArray("xababababYabab") << QList<QByteArray>({"x", "", "Y"});
        QTest::newRow("backslash") << QString("\\\\") << QByteArray("a\\b\\") << QList<QByteArray>({"a", "b"});
        QTest::newRow("utf8") << QString::fromUtf8("й›Є") << QString::fromUtf8("aй›Єbй›Є").toUtf8()
                              << QList<QByteArray>({"a", "b"});
    }
    void delimitersAtEverySplit()
    {
        QFETCH(QString, ending);
        QFETCH(QByteArray, input);
        QFETCH(QList<QByteArray>, expected);
        for (qsizetype split = 0; split <= input.size(); ++split)
        {
            auto processor = Framer::create(
                {
                    {"ending", ending}
            },
                {});
            QVERIFY(processor);
            auto first  = invoke(*processor, input.first(split));
            auto second = invoke(*processor, input.mid(split));
            QCOMPARE(first.accepted, Processor::AcceptResult::Accepted);
            QCOMPARE(second.accepted, Processor::AcceptResult::Accepted);
            QCOMPARE(first.completions, 1);
            QCOMPARE(second.completions, 1);
            QVERIFY(first.error.isEmpty());
            QVERIFY(second.error.isEmpty());
            first.outputs.append(second.outputs);
            QCOMPARE(payloads(first.outputs), expected);
        }
        auto processor = Framer::create(
            {
                {"ending", ending}
        },
            {});
        QList<Processor::Chunk> output;
        for (const auto byte : input)
        {
            const auto result = invoke(*processor, QByteArray(1, byte));
            QCOMPARE(result.completions, 1);
            QVERIFY(result.error.isEmpty());
            output.append(result.outputs);
        }
        QCOMPARE(payloads(output), expected);
    }

    void sizeAndDelimiterTie()
    {
        auto sized = Framer::create(
            {
                {"frameSize", 3}
        },
            {});
        QVERIFY(sized);
        QVERIFY(invoke(*sized, QByteArray("a\0", 2)).outputs.isEmpty());
        QCOMPARE(payloads(invoke(*sized, QByteArray("xbcdef", 6)).outputs),
                 QList<QByteArray>({QByteArray("a\0x", 3), "bcd"}));
        auto either = Framer::create(
            {
                {"frameSize",        4},
                {   "ending", "\\r\\n"}
        },
            {});
        QVERIFY(either);
        QCOMPARE(payloads(invoke(*either, "AB\r\nCDEFZ\r\n").outputs), QList<QByteArray>({"AB", "CDEF", "Z"}));
        auto included = Framer::create(
            {
                {    "frameSize",        4},
                {       "ending", "\\r\\n"},
                {"includeEnding",     true}
        },
            {});
        QCOMPARE(payloads(invoke(*included, "AB\r\nCDEF").outputs), QList<QByteArray>({"AB\r\n", "CDEF"}));
        auto partial = Framer::create(
            {
                {"frameSize",        3},
                {   "ending", "\\r\\n"}
        },
            {});
        QCOMPARE(payloads(invoke(*partial, "AB\r\n\r\n").outputs), QList<QByteArray>({"AB\r", "\n"}));
    }

    void defaultsKeepLongPartialFramesAndEmptyPolicy()
    {
        auto processor = Framer::create({}, {});
        QVERIFY(invoke(*processor, QByteArray(128, 'x')).outputs.isEmpty());
        QVERIFY(invoke(*processor, {}).outputs.isEmpty());
        QCOMPARE(payloads(invoke(*processor, "\r\r").outputs), QList<QByteArray>({QByteArray(128, 'x'), ""}));
        auto noEmpty = Framer::create(
            {
                {"emitEmpty", false}
        },
            {});
        QCOMPARE(payloads(invoke(*noEmpty, "\r\ra\r\r").outputs), QList<QByteArray>({"a"}));
        auto included = Framer::create(
            {
                {"includeEnding",  true},
                {    "emitEmpty", false}
        },
            {});
        QCOMPARE(payloads(invoke(*included, "\r").outputs), QList<QByteArray>({"\r"}));
    }

    void boundsFailAtomicallyAndStop()
    {
        Processor::Limits limits;
        limits.maxBytes  = 8;
        limits.maxChunks = 2;
        auto limited     = Framer::create({}, limits);
        QVERIFY(limited);
        const auto rejected = invoke(*limited, QByteArray(9, 'x'));
        QCOMPARE(rejected.accepted, Processor::AcceptResult::Rejected);
        QCOMPARE(rejected.completions, 0);
        QCOMPARE(payloads(invoke(*limited, "ok\r").outputs), QList<QByteArray>({"ok"}));
        const auto tooMany = invoke(*limited, "\r\r\r");
        QCOMPARE(tooMany.completions, 1);
        QVERIFY(!tooMany.error.isEmpty());
        QVERIFY(tooMany.outputs.isEmpty());
        QCOMPARE(invoke(*limited, "\r").accepted, Processor::AcceptResult::Rejected);
        auto accumulated = Framer::create({}, limits);
        QVERIFY(invoke(*accumulated, "12345678").error.isEmpty());
        const auto overflow = invoke(*accumulated, "\r");
        QVERIFY(!overflow.error.isEmpty());
        QVERIFY(overflow.outputs.isEmpty());
        auto exact = Framer::create(
            {
                {"ending", "\\r\\n"}
        },
            limits);
        QVERIFY(invoke(*exact, "123456\r").error.isEmpty());
        QCOMPARE(payloads(invoke(*exact, "\n").outputs), QList<QByteArray>({"123456"}));
        auto atomic = Framer::create({}, limits);
        QVERIFY(invoke(*atomic, "123456").error.isEmpty());
        const auto result = invoke(*atomic, "\ra\rxyz");
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.outputs.isEmpty());
        auto combined = Framer::create({}, limits);
        QVERIFY(invoke(*combined, "123456").error.isEmpty());
        const auto combinedOverflow = invoke(*combined, "\rabc\r");
        QVERIFY(!combinedOverflow.error.isEmpty());
        QVERIFY(combinedOverflow.outputs.isEmpty());
    }

    void callbackReentryCancellationAndDeletion()
    {
        auto                    processor   = Framer::create({}, {});
        int                     outputs     = 0;
        int                     completions = 0;
        Processor::AcceptResult nested      = Processor::AcceptResult::Rejected;
        const auto              status      = processor->accept({"a\rb\r"},
                                                                {[&](Processor::Chunk) {
                                                   ++outputs;
                                                   nested = processor->accept({"nested\r"}, {});
                                                                 },
                                                                 [&](QString error) {
                                                   QVERIFY(error.isEmpty());
                                                   ++completions;
                                                                 }});
        QCOMPARE(status, Processor::AcceptResult::Accepted);
        QCOMPARE(outputs, 2);
        QCOMPARE(completions, 1);
        QCOMPARE(nested, Processor::AcceptResult::WouldBlock);
        int completedReentry = 0;
        processor->accept({"z\r"}, {{}, [&](QString error) {
                                        QVERIFY(error.isEmpty());
                                        const auto result = invoke(*processor, "after\r");
                                        QCOMPARE(result.accepted, Processor::AcceptResult::Accepted);
                                        ++completedReentry;
                                    }});
        QCOMPARE(completedReentry, 1);
        outputs              = 0;
        completions          = 0;
        int acknowledgements = 0;
        processor->accept({"a\rb\r"},
                          {[&](Processor::Chunk) {
                               ++outputs;
                               processor->cancel([&] { ++acknowledgements; });
                           },
                           [&](QString) { ++completions; }});
        QCOMPARE(outputs, 1);
        QCOMPARE(completions, 0);
        QCOMPARE(acknowledgements, 1);
        QCOMPARE(invoke(*processor, "new\r").accepted, Processor::AcceptResult::Rejected);
        processor->cancel([&] { ++acknowledgements; });
        QCOMPARE(acknowledgements, 2);
        auto dying  = Framer::create({}, {});
        outputs     = 0;
        completions = 0;
        auto* raw   = dying.get();
        QCOMPARE(raw->accept({"a\rb\r"},
                             {[&](Processor::Chunk) {
                                  ++outputs;
                                  dying.reset();
                              },
                              [&](QString) { ++completions; }}),
                 Processor::AcceptResult::Accepted);
        QVERIFY(!dying);
        QCOMPARE(outputs, 1);
        QCOMPARE(completions, 0);
        auto buffered = Framer::create({}, {});
        QVERIFY(invoke(*buffered, "pending").outputs.isEmpty());
        buffered->cancel({});
        QCOMPARE(invoke(*buffered, "\r").accepted, Processor::AcceptResult::Rejected);
    }

    void invalidConfigurationAndUnusedFields()
    {
        QString error;
        for (const auto& ending :
             {QString {}, QString("\\"), QString("\\x0"), QString("\\xGG"), QString("\\q"), QString(257, 'x')})
        {
            QVERIFY(!Framer::create(
                {
                    {"ending", ending}
            },
                {},
                &error));
            QVERIFY(!error.isEmpty());
        }
        for (const auto& value : {QVariant(-1), QVariant(2.5), QVariant(true), QVariant("3"), QVariant(2147483648LL)})
        {
            QVERIFY(!Framer::create(
                {
                    {"frameSize", value}
            },
                {},
                &error));
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(!Framer::create(
            {
                {"includeEnding", 1}
        },
            {},
            &error));
        auto unknown = Framer::create(
            {
                {        "mode", "other"},
                {  "packetTail", "error"},
                {"maxFrameSize",       1},
                {      "opaque",  "kept"}
        },
            {});
        QVERIFY(unknown);
        QCOMPARE(unknown->configuration().value("opaque").toString(), "kept");
        QVERIFY(invoke(*unknown, "long partial").outputs.isEmpty());
        QCOMPARE(payloads(invoke(*unknown, "\r").outputs), QList<QByteArray>({"long partial"}));
        QVERIFY(Framer::create(
            {
                {"frameSize", 2147483647}
        },
            {}));
        auto escaped = Framer::create(
            {
                {"ending", "\\r\\n\\0\\x41\\\\"}
        },
            {});
        QVERIFY(escaped);
        QCOMPARE(payloads(invoke(*escaped, QByteArray("x\r\n\0A\\", 6)).outputs), QList<QByteArray>({"x"}));
    }
};
QTEST_GUILESS_MAIN(FramerProcessorTests)
#include "FramerProcessorTests.moc"
