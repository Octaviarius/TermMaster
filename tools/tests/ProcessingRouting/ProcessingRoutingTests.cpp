#include "AdvancedSender.h"
#include "ProcessingRouter.h"

#include <QTemporaryFile>
#include <QTest>

class DelayedProcessor final : public Processor
{
public:
    Callbacks             callbacks;
    Chunk                 input;
    std::function<void()> acknowledgement;
    bool                  block = false;
    QVariantMap           configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk chunk, Callbacks next) override
    {
        if (block)
        {
            return AcceptResult::WouldBlock;
        }
        input     = std::move(chunk);
        callbacks = std::move(next);
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> done) override
    {
        acknowledgement = std::move(done);
    }
};

class RoutingDevice final : public SerialDevice
{
public:
    QByteArray received;
    QObject*   object() override
    {
        return nullptr;
    }
    State state() const override
    {
        return State::Open;
    }
    void setCallbacks(Callbacks) override
    {
    }
    bool open(const QVariantMap&, QString*) override
    {
        return true;
    }
    void close() override
    {
    }
    qint64 write(const QByteArray& bytes, QString*) override
    {
        received += bytes;
        return bytes.size();
    }
    qint64 pendingBytes() const override
    {
        return 0;
    }
};

class ProcessingRoutingTests final : public QObject
{
    Q_OBJECT
private slots:
    void diagnosticsTrackAsyncReplacementAndFailure()
    {
        ProcessingRouter router;
        const auto       direction = ProcessingRouter::Direction::Rx;
        QCOMPARE(router.diagnostics(direction).state, ProcessingRouter::State::Bypass);
        router.setOutput(direction, [](const Processor::Chunk&, const TerminalSendQueue::Options&, QString*) {
            return Processor::AcceptResult::Accepted;
        });
        auto processor = std::make_shared<DelayedProcessor>();
        router.replace(direction, processor);
        QTRY_COMPARE(router.diagnostics(direction).state, ProcessingRouter::State::Idle);
        QCOMPARE(router.submit(direction, {QByteArray("abc"), Processor::Boundary::StreamChunk}),
                 Processor::AcceptResult::Accepted);
        QTRY_VERIFY(bool(processor->callbacks.completed));
        QCOMPARE(router.diagnostics(direction).state, ProcessingRouter::State::Busy);
        QCOMPARE(router.diagnostics(direction).acceptedBytes, quint64(3));
        QCOMPARE(router.diagnostics(direction).pendingBytes, qsizetype(3));
        processor->callbacks.output({QByteArray("ABCDE"), Processor::Boundary::Packet});
        processor->callbacks.completed({});
        QTRY_COMPARE(router.diagnostics(direction).forwardedBytes, quint64(5));
        QCOMPARE(router.diagnostics(direction).pendingChunks, qsizetype(0));
        router.cancel(direction);
        QCOMPARE(router.diagnostics(direction).state, ProcessingRouter::State::Cancelling);
        QVERIFY(bool(processor->acknowledgement));
        processor->acknowledgement();
        QTRY_COMPARE(router.diagnostics(direction).state, ProcessingRouter::State::Bypass);
        auto failed = std::make_shared<DelayedProcessor>();
        router.replace(direction, failed);
        QTRY_COMPARE(router.diagnostics(direction).state, ProcessingRouter::State::Idle);
        QCOMPARE(router.submit(direction, {QByteArray("x"), Processor::Boundary::StreamChunk}),
                 Processor::AcceptResult::Accepted);
        QTRY_VERIFY(bool(failed->callbacks.completed));
        failed->callbacks.completed("nested/node: synthetic failure");
        QCOMPARE(router.diagnostics(direction).state, ProcessingRouter::State::Failed);
        QCOMPARE(router.diagnostics(direction).failures, quint64(1));
        QCOMPARE(router.diagnostics(direction).lastError, QString("nested/node: synthetic failure"));
        failed->acknowledgement();
    }

    void packetsAndOptions()
    {
        ProcessingRouter router;
        const auto       direction = ProcessingRouter::Direction::Tx;
        auto             processor = std::make_shared<DelayedProcessor>();
        router.replace(direction, processor);
        QList<Processor::Chunk> received;
        QList<int>              delays;
        bool                    blocked = true;
        router.setOutput(direction, [&](const auto& chunk, const auto& options, QString*) {
            if (blocked)
            {
                return Processor::AcceptResult::WouldBlock;
            }
            received.append(chunk);
            delays.append(options.byteDelayMs);
            return Processor::AcceptResult::Accepted;
        });
        TerminalSendQueue::Options options;
        options.byteDelayMs = 13;
        QCOMPARE(router.submit(direction, {QByteArray("a\0b", 3), Processor::Boundary::Packet}, options),
                 Processor::AcceptResult::Accepted);
        QTRY_VERIFY(bool(processor->callbacks.output));
        QCOMPARE(processor->input.bytes, QByteArray("a\0b", 3));
        processor->callbacks.output({QByteArray("x\0", 2), Processor::Boundary::Packet});
        processor->callbacks.output({{}, Processor::Boundary::Packet});
        processor->callbacks.completed({});
        QTest::qWait(20);
        QVERIFY(received.isEmpty());
        blocked = false;
        router.wake(direction);
        QTRY_COMPARE(received.size(), 2);
        QCOMPARE(received[0].bytes, QByteArray("x\0", 2));
        QCOMPARE(received[1].boundary, Processor::Boundary::Packet);
        QVERIFY(received[1].bytes.isEmpty());
        QCOMPARE(delays, QList<int>({13, 13}));
        router.cancel(direction);
        processor->acknowledgement();
        QCoreApplication::processEvents();
    }

    void saturationAndIndependentDirections()
    {
        ProcessingRouter::Limits limits;
        limits.maxBytes  = 4;
        limits.maxChunks = 2;
        ProcessingRouter router(limits);
        const auto       rx        = ProcessingRouter::Direction::Rx;
        const auto       tx        = ProcessingRouter::Direction::Tx;
        auto             processor = std::make_shared<DelayedProcessor>();
        processor->block           = true;
        router.replace(rx, processor);
        QCOMPARE(router.submit(rx, {QByteArray("1234")}), Processor::AcceptResult::Accepted);
        QCOMPARE(router.submit(rx, {QByteArray("5")}), Processor::AcceptResult::WouldBlock);
        QCOMPARE(router.submit(rx, {QByteArray("12345")}), Processor::AcceptResult::Rejected);
        QByteArray transmitted;
        router.setOutput(tx, [&](const auto& chunk, const auto&, QString*) {
            transmitted += chunk.bytes;
            return Processor::AcceptResult::Accepted;
        });
        QCOMPARE(router.submit(tx, {QByteArray("tx")}), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(transmitted, QByteArray("tx"));
        processor->block = false;
        QTRY_VERIFY(bool(processor->callbacks.completed));
        processor->callbacks.completed({}); // Zero output is a successful invocation.
        QTRY_COMPARE(router.pendingBytes(rx), 0);
        router.cancel(rx);
        processor->acknowledgement();
        QCoreApplication::processEvents();
    }

    void replacementQuiescenceAndStaleCallbacks()
    {
        ProcessingRouter router;
        const auto       rx  = ProcessingRouter::Direction::Rx;
        auto             old = std::make_shared<DelayedProcessor>();
        router.replace(rx, old);
        QByteArray received;
        router.setOutput(rx, [&](const auto& chunk, const auto&, QString*) {
            received += chunk.bytes;
            return Processor::AcceptResult::Accepted;
        });
        router.submit(rx, {QByteArray("old")});
        QTRY_VERIFY(bool(old->callbacks.output));
        const auto stale = old->callbacks;
        bool       ready = false;
        router.replace(rx, {}, [&] { ready = true; });
        QVERIFY(!ready);
        QCOMPARE(router.submit(rx, {QByteArray("early")}), Processor::AcceptResult::Accepted);
        stale.output({QByteArray("stale")});
        stale.completed({});
        QVERIFY(received.isEmpty());
        old->acknowledgement();
        QTRY_VERIFY(ready);
        router.submit(rx, {QByteArray("new")});
        QTRY_COMPARE(received, QByteArray("earlynew"));
    }

    void coalescesReplacementAndPinsDestroyedRouter()
    {
        const auto rx     = ProcessingRouter::Direction::Rx;
        auto       old    = std::make_shared<DelayedProcessor>();
        auto       router = std::make_unique<ProcessingRouter>();
        router->replace(rx, old);
        auto superseded = std::make_shared<DelayedProcessor>();
        auto latest     = std::make_shared<DelayedProcessor>();
        int  ready      = 0;
        router->cancel(rx, [&] { ++ready; });
        router->replace(rx, superseded, [&] { ++ready; });
        router->replace(rx, latest, [&] { ++ready; });
        QVERIFY(bool(superseded->acknowledgement));
        superseded->acknowledgement();
        QCoreApplication::processEvents();
        QCOMPARE(ready, 0);
        old->acknowledgement();
        QTRY_COMPARE(ready, 3);
        router->submit(rx, {QByteArray("latest")});
        QTRY_COMPARE(latest->input.bytes, QByteArray("latest"));
        router.reset();
        QVERIFY(bool(latest->acknowledgement));
        std::weak_ptr<DelayedProcessor> lifetime    = latest;
        auto                            acknowledge = latest->acknowledgement;
        latest.reset();
        QVERIFY(!lifetime.expired());
        acknowledge();
        QTRY_VERIFY(lifetime.expired());
    }

    void senderStopDiscardsDelayedOutputAndDoesNotRefill()
    {
        RoutingDevice     device;
        TerminalSendQueue queue(device);
        ProcessingRouter  router;
        AdvancedSender    sender(queue, device);
        const auto        tx        = ProcessingRouter::Direction::Tx;
        auto              processor = std::make_shared<DelayedProcessor>();
        router.replace(tx, processor);
        router.setOutput(tx, [&](const auto& chunk, const auto& options, QString* error) {
            return queue.enqueue(chunk.bytes, options, error) ? Processor::AcceptResult::Accepted :
                                                                Processor::AcceptResult::Rejected;
        });
        sender.setProcessingRouter(&router);
        connect(&sender, &AdvancedSender::stopped, &router, [&] { router.cancel(tx); });
        QTemporaryFile file;
        QVERIFY(file.open());
        QVERIFY(file.write(QByteArray(150000, 'f')) == 150000);
        QVERIFY(file.flush());
        QVERIFY(sender.startFile(file.fileName(), false, {}));
        QTRY_VERIFY(bool(processor->callbacks.output));
        const auto stale = processor->callbacks;
        sender.stop();
        stale.output({QByteArray("stale")});
        stale.completed({});
        processor->acknowledgement();
        QTest::qWait(30);
        QVERIFY(!sender.active());
        QVERIFY(device.received.isEmpty());
        QCOMPARE(queue.pendingBytes(), 0);
        QCOMPARE(router.pendingSubmissions(tx), 0);
    }

    void expansionFailsAndRetainsUntilQuiescent()
    {
        ProcessingRouter::Limits limits;
        limits.maxBytes = 4;
        ProcessingRouter router(limits);
        const auto       rx        = ProcessingRouter::Direction::Rx;
        auto             processor = std::make_shared<DelayedProcessor>();
        router.replace(rx, processor);
        int errors = 0;
        connect(&router, &ProcessingRouter::error, &router, [&](auto, const auto&) { ++errors; });
        router.submit(rx, {QByteArray("x")});
        QTRY_VERIFY(bool(processor->callbacks.output));
        processor->callbacks.output({QByteArray("12345")});
        QCOMPARE(errors, 1);
        QVERIFY(bool(processor->acknowledgement));
        QCOMPARE(router.pendingBytes(rx), 0);
        QCOMPARE(router.submit(rx, {QByteArray("x")}), Processor::AcceptResult::Rejected);
        processor->acknowledgement();
        QCoreApplication::processEvents();
    }
};
QTEST_GUILESS_MAIN(ProcessingRoutingTests)
#include "ProcessingRoutingTests.moc"
