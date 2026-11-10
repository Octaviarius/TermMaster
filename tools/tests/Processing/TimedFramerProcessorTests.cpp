#include "../../../src/Core/src/Processing/Nodes/TimedFramer.h"

#include <QPointer>
#include <QTest>
#include <Termaster/Core/SettingsSchema.h>

namespace
{
Processor::AcceptResult send(Processor& processor, QByteArray bytes, QString* error = nullptr)
{
    const auto result = processor.accept({std::move(bytes)}, {{}, [&](QString message) {
                                                                  if (error)
                                                                  {
                                                                      *error = std::move(message);
                                                                  }
                                                              }});
    return result;
}
} // namespace

class TimedFramerProcessorTests final : public QObject
{
    Q_OBJECT
private slots:
    void binaryAndBorrowedInput()
    {
        auto processor = TimedFramer::create({}, {});
        QVERIFY(processor);
        QCOMPARE(processor->inputName(0), QString("Stream"));
        QCOMPARE(processor->outputName(0), QString("Frames"));
        SettingsSchema schema(TimedFramer::schema());
        QVERIFY(schema.validate(processor->configuration()).isEmpty());
        int completed         = 0;
        int invocationOutputs = 0;
        QCOMPARE(processor->accept({},
                                   {[&](Processor::Chunk) { ++invocationOutputs; },
                                    [&](QString error) {
                                        QVERIFY(error.isEmpty());
                                        ++completed;
                                    }}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(completed, 1);
        QCOMPARE(invocationOutputs, 0);
        QList<QByteArray> frames;
        processor->setOutputHandler([&](Processor::Chunk output) { frames.append(output.bytes); });
        QByteArray borrowed("A\0B\xff", 4);
        QCOMPARE(send(*processor, QByteArray::fromRawData(borrowed.data(), borrowed.size())),
                 Processor::AcceptResult::Accepted);
        borrowed.fill('x');
        send(*processor, QByteArray("C"));
        QVERIFY(frames.isEmpty());
        QTRY_COMPARE(frames.size(), 1);
        QCOMPARE(frames.front(), QByteArray("A\0B\xff", 4) + "C");
        send(*processor, {});
        QTest::qWait(40);
        QCOMPARE(frames.size(), 1);
    }

    void resetGapAndEmptyDoesNotReset()
    {
        auto processor = TimedFramer::create(
            {
                {"gapMs", 100}
        },
            {});
        QList<QByteArray> frames;
        processor->setOutputHandler([&](Processor::Chunk output) { frames.append(output.bytes); });
        send(*processor, "a");
        QTest::qSleep(60);
        send(*processor, "b");
        QTest::qSleep(60);
        QCoreApplication::processEvents();
        QVERIFY(frames.isEmpty());
        send(*processor, {});
        QTest::qSleep(50);
        QCoreApplication::processEvents();
        QCOMPARE(frames, QList<QByteArray>({"ab"}));
    }

    void overdueArrivalAndIndependentInstances()
    {
        auto first = TimedFramer::create(
            {
                {"gapMs", 20}
        },
            {});
        auto second = TimedFramer::create(
            {
                {"gapMs", 20}
        },
            {});
        QList<QByteArray> a;
        QList<QByteArray> b;
        first->setOutputHandler([&](Processor::Chunk output) { a.append(output.bytes); });
        second->setOutputHandler([&](Processor::Chunk output) { b.append(output.bytes); });
        send(*first, "one");
        send(*second, "other");
        QTest::qSleep(40);
        send(*first, "two");
        QCOMPARE(a, QList<QByteArray>({"one"}));
        QVERIFY(b.isEmpty());
        QTRY_COMPARE(a.size(), 2);
        QTRY_COMPARE(b.size(), 1);
        QCOMPARE(a.back(), QByteArray("two"));
        QCOMPARE(b.front(), QByteArray("other"));
    }

    void limitsAndCancellation()
    {
        Processor::Limits limits;
        limits.maxBytes = 3;
        auto processor  = TimedFramer::create({}, limits);
        int  outputs    = 0;
        processor->setOutputHandler([&](Processor::Chunk) { ++outputs; });
        send(*processor, "ab");
        QString error;
        QCOMPARE(send(*processor, "cd", &error), Processor::AcceptResult::Accepted);
        QVERIFY(!error.isEmpty());
        QCOMPARE(send(*processor, "x"), Processor::AcceptResult::Rejected);
        QTest::qWait(40);
        QCOMPARE(outputs, 0);

        processor = TimedFramer::create({}, {});
        processor->setOutputHandler([&](Processor::Chunk) { ++outputs; });
        send(*processor, "pending");
        int acknowledged = 0;
        processor->cancel([&] { ++acknowledged; });
        processor->cancel([&] { ++acknowledged; });
        QCOMPARE(acknowledged, 2);
        QTest::qWait(40);
        QCOMPARE(outputs, 0);
    }

    void callbacksMayReenterCancelAndDestroy()
    {
        auto processor = TimedFramer::create({}, {});
        int  outputs   = 0;
        processor->setOutputHandler([&](Processor::Chunk) {
            ++outputs;
            QCOMPARE(send(*processor, "nested"), Processor::AcceptResult::WouldBlock);
            processor->cancel({});
        });
        send(*processor, "a");
        QTRY_COMPARE(outputs, 1);
        QCOMPARE(send(*processor, "later"), Processor::AcceptResult::Rejected);

        processor = TimedFramer::create({}, {});
        QPointer<Processor> guard(processor.get());
        processor->setOutputHandler([&](Processor::Chunk) { processor.reset(); });
        send(*processor, "a");
        QTRY_VERIFY(!guard);
        QTest::qWait(40);

        processor = TimedFramer::create({}, {});
        processor->setOutputHandler([&](Processor::Chunk) { processor.reset(); });
        send(*processor, "a");
        QTest::qSleep(40);
        const auto result = send(*processor, "b");
        QCOMPARE(result, Processor::AcceptResult::Accepted);
        QVERIFY(!processor);
    }

    void completionCanRunNestedEventLoop()
    {
        auto processor = TimedFramer::create(
            {
                {"gapMs", 10}
        },
            {});
        int outputs = 0;
        processor->setOutputHandler([&](Processor::Chunk output) {
            QCOMPARE(output.bytes, QByteArray("pending"));
            ++outputs;
        });
        QCOMPARE(processor->accept({"pending"},
                                   {{},
                                    [&](QString error) {
                                        QVERIFY(error.isEmpty());
                                        QTest::qWait(40);
                                        QCOMPARE(outputs, 1);
                                    }}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(outputs, 1);
    }

    void invalidConfiguration()
    {
        for (const auto& value :
             {QVariant(0), QVariant(-1), QVariant(60001), QVariant(1.5), QVariant("20"), QVariant(true)})
        {
            QString error;
            QVERIFY(!TimedFramer::create(
                {
                    {"gapMs", value}
            },
                {},
                &error));
            QVERIFY(!error.isEmpty());
        }
        Processor::Limits limits;
        limits.maxBytes = 0;
        QVERIFY(!TimedFramer::create({}, limits));
        auto processor = TimedFramer::create(
            {
                {"opaque", "retained"}
        },
            {});
        QCOMPARE(processor->configuration().value("opaque").toString(), QString("retained"));
    }
};

QTEST_GUILESS_MAIN(TimedFramerProcessorTests)
#include "TimedFramerProcessorTests.moc"
