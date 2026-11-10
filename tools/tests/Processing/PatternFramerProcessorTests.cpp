#include "../../../src/Core/src/Processing/Nodes/PatternFramer.h"

#include <QTest>
#include <Termaster/Core/SettingsSchema.h>

namespace
{
struct Invocation
{
    Processor::AcceptResult accepted = Processor::AcceptResult::Rejected;
    QList<QByteArray>       output;
    QString                 error;
    int                     completed = 0;
};
Invocation invoke(Processor& processor, QByteArray input)
{
    Invocation result;
    result.accepted = processor.accept({std::move(input)},
                                       {[&](Processor::Chunk chunk) { result.output.append(std::move(chunk.bytes)); },
                                        [&](QString error) {
                                            result.error = std::move(error);
                                            ++result.completed;
                                        }});
    return result;
}
} // namespace

class PatternFramerProcessorTests final : public QObject
{
    Q_OBJECT
private slots:
    void defaultsAndSchema()
    {
        auto processor = PatternFramer::create({}, {});
        QVERIFY(processor);
        SettingsSchema schema(PatternFramer::schema());
        QCOMPARE(schema.fields().size(), 2);
        QVERIFY(schema.validate(processor->configuration()).isEmpty());
        QCOMPARE(invoke(*processor, "a\r\nb\n").output, QList<QByteArray>({"a\r\n", "b\n"}));
    }
    void binaryEverySplit()
    {
        const QByteArray        input("noise<\0\xff>\n<x>\n", 14);
        const QList<QByteArray> expected {QByteArray("\0\xff", 2), "x"};
        for (qsizetype split = 0; split <= input.size(); ++split)
        {
            auto processor = PatternFramer::create(
                {
                    {       "pattern", "<(?<data>.*?)>\\n"},
                    {"outputTemplate",           "${data}"}
            },
                {});
            QVERIFY(processor);
            auto first  = invoke(*processor, input.first(split));
            auto second = invoke(*processor, input.mid(split));
            QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
            QVERIFY2(second.error.isEmpty(), qPrintable(second.error));
            first.output.append(second.output);
            QCOMPARE(first.output, expected);
            QCOMPARE(first.completed, 1);
            QCOMPARE(second.completed, 1);
        }
        auto processor = PatternFramer::create(
            {
                {       "pattern", "<(?<data>.*?)>\\n"},
                {"outputTemplate",           "${data}"}
        },
            {});
        QList<QByteArray> outputs;
        for (char byte : input)
        {
            auto result = invoke(*processor, QByteArray(1, byte));
            QVERIFY(result.error.isEmpty());
            outputs.append(result.output);
        }
        QCOMPARE(outputs, expected);
    }
    void borrowedInputAndCaptureNames()
    {
        auto processor = PatternFramer::create(
            {
                {       "pattern", "<(?<data>[^>]+)>;"},
                {"outputTemplate",           "${data}"}
        },
            {});
        QVERIFY(processor);
        QCOMPARE(processor->inputName(0), QString("Stream"));
        QCOMPARE(processor->outputName(0), QString("Frames"));
        QByteArray borrowed("<A");
        QVERIFY(invoke(*processor, QByteArray::fromRawData(borrowed.data(), borrowed.size())).output.isEmpty());
        borrowed.fill('x');
        QCOMPARE(invoke(*processor, "B>;").output, QList<QByteArray> {"AB"});
        QString name = "${data";
        name.append(QChar(0));
        name.append("ignored}");
        QString error;
        QVERIFY(!PatternFramer::create(
            {
                {       "pattern", "(?<data>a);"},
                {"outputTemplate",          name}
        },
            {},
            &error));
        QVERIFY(error.contains("NUL"));
    }

    void nestedCompositionAndEscapes()
    {
        auto processor = PatternFramer::create(
            {
                {       "pattern",                                 "((?<inner>AB)C)(D)?;"},
                {"outputTemplate", "${2}abcdef${1}END${inner}${3}$$\\\\\\r\\n\\t\\0\\xFF"}
        },
            {});
        QVERIFY(processor);
        QByteArray expected = "ABabcdefABCENDAB$\\\r\n\t";
        expected.append('\0');
        expected.append(char(0xff));
        QCOMPARE(invoke(*processor, "ABC;").output, QList<QByteArray>({expected}));
        auto whole = PatternFramer::create(
            {
                {"pattern", "<(a)>;"}
        },
            {});
        QCOMPARE(invoke(*whole, "noise<a>;tail").output, QList<QByteArray>({"<a>;"}));
    }
    void historyAndFreshSubjects()
    {
        auto lookbehind = PatternFramer::create(
            {
                {"pattern", "(?<=ABC)D;"}
        },
            {});
        QVERIFY(lookbehind);
        QVERIFY(invoke(*lookbehind, "ABC").output.isEmpty());
        QCOMPARE(invoke(*lookbehind, "D;").output, QList<QByteArray>({"D;"}));
        auto anchor = PatternFramer::create(
            {
                {"pattern", "^a;"}
        },
            {});
        QCOMPARE(invoke(*anchor, "a;a;").output, QList<QByteArray>({"a;", "a;"}));
        auto noPrior = PatternFramer::create(
            {
                {"pattern", "a;|(?<=;)b;"}
        },
            {});
        QCOMPARE(invoke(*noPrior, "a;b;").output, QList<QByteArray>({"a;"}));
    }
    void startOffsetAnchorKeepsPacketIndependentMeaning()
    {
        auto processor = PatternFramer::create(
            {
                {"pattern", R"(A(?=Q)|\GA;)"}
        },
            {});
        QVERIFY(processor);
        QVERIFY(invoke(*processor, "zA").output.isEmpty());
        QVERIFY(invoke(*processor, ";").output.isEmpty());
        auto whole = PatternFramer::create(
            {
                {"pattern", R"(A(?=Q)|\GA;)"}
        },
            {});
        QVERIFY(invoke(*whole, "zA;").output.isEmpty());
    }

    void invalidConfiguration()
    {
        QString error;
        for (const auto& pattern :
             {QString("["), QString("a*"), QString("(?=a)"), QString("(*UTF)a;"), QString(4097, 'a')})
        {
            QVERIFY(!PatternFramer::create(
                {
                    {"pattern", pattern}
            },
                {},
                &error));
            QVERIFY(!error.isEmpty());
        }
        for (const auto& text : {"${1}", "${missing}", "${}", "${0", "$", "\\", "\\q", "\\xF", "\\xGG"})
        {
            QVERIFY(!PatternFramer::create(
                {
                    {       "pattern", "a;"},
                    {"outputTemplate", text}
            },
                {},
                &error));
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(!PatternFramer::create(
            {
                {"pattern", 3}
        },
            {},
            &error));
        QVERIFY(!PatternFramer::create(
            {
                {"outputTemplate", false}
        },
            {},
            &error));
    }
    void boundedAtomicOutput()
    {
        Processor::Limits limits;
        limits.maxBytes  = 16;
        limits.maxChunks = 2;
        auto processor   = PatternFramer::create(
            {
                {"pattern", "a;"}
        },
            limits);
        QCOMPARE(invoke(*processor, QByteArray(17, 'x')).accepted, Processor::AcceptResult::Rejected);
        auto result = invoke(*processor, "a;a;a;");
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.output.isEmpty());
        QCOMPARE(result.completed, 1);
        QCOMPARE(invoke(*processor, "a;").accepted, Processor::AcceptResult::Rejected);
        auto pending = PatternFramer::create(
            {
                {"pattern", "a;"}
        },
            limits);
        QVERIFY(invoke(*pending, QByteArray(16, 'x')).error.isEmpty());
        QVERIFY(!invoke(*pending, "x").error.isEmpty());
        auto expansion = PatternFramer::create(
            {
                {       "pattern",        "(a+);"},
                {"outputTemplate", "${1}${1}${1}"}
        },
            limits);
        result = invoke(*expansion, "aaaaaa;");
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.output.isEmpty());
        limits.maxSteps = 1;
        auto budget     = PatternFramer::create(
            {
                {"pattern", "a;"}
        },
            limits);
        result = invoke(*budget, "a;a;");
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.output.isEmpty());
    }
    void callbacksAndLifetime()
    {
        auto processor = PatternFramer::create(
            {
                {"pattern", "a;"}
        },
            {});
        int outputs     = 0;
        int completions = 0;
        processor->accept({"a;a;"},
                          {[&](Processor::Chunk) {
                               ++outputs;
                               QCOMPARE(processor->accept({"a;"}, {}), Processor::AcceptResult::WouldBlock);
                           },
                           [&](QString error) {
                               QVERIFY(error.isEmpty());
                               ++completions;
                           }});
        QCOMPARE(outputs, 2);
        QCOMPARE(completions, 1);
        processor->accept({"a;"}, {{}, [&](QString) {
                                       QCOMPARE(invoke(*processor, "a;").accepted, Processor::AcceptResult::Accepted);
                                   }});
        outputs          = 0;
        completions      = 0;
        int acknowledged = 0;
        processor->accept({"a;a;"},
                          {[&](Processor::Chunk) {
                               ++outputs;
                               processor->cancel([&] { ++acknowledged; });
                           },
                           [&](QString) { ++completions; }});
        QCOMPARE(outputs, 1);
        QCOMPARE(completions, 0);
        QCOMPARE(acknowledged, 1);
        processor->cancel([&] { ++acknowledged; });
        QCOMPARE(acknowledged, 2);
        auto dying = PatternFramer::create(
            {
                {"pattern", "a;"}
        },
            {});
        auto* raw = dying.get();
        outputs   = 0;
        raw->accept({"a;a;"},
                    {[&](Processor::Chunk) {
                         ++outputs;
                         dying.reset();
                     },
                     [&](QString) { ++completions; }});
        QVERIFY(!dying);
        QCOMPARE(outputs, 1);
        QCOMPARE(completions, 0);
    }
};
QTEST_GUILESS_MAIN(PatternFramerProcessorTests)
#include "PatternFramerProcessorTests.moc"
