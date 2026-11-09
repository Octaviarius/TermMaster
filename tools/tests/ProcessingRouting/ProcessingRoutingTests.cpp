#include "AdvancedSender.h"
#include "ProcessingRouter.h"

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>

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

class DuplexProbe final : public Processor
{
public:
    int                   cancelled = 0;
    bool                  deferred  = false;
    std::function<void()> acknowledgement;
    int                   inputCount() const override
    {
        return 2;
    }
    int outputCount() const override
    {
        return 2;
    }
    QVariantMap configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk chunk, Callbacks callbacks) override
    {
        if (chunk.port == 0)
        {
            callbacks.output({QByteArray("ACK"), Boundary::Packet, 1});
        }
        callbacks.output(std::move(chunk));
        callbacks.completed({});
        return AcceptResult::Accepted;
    }
    void unsolicited(int port, QByteArray bytes)
    {
        publishOutput({std::move(bytes), Boundary::StreamChunk, port});
    }
    void cancel(std::function<void()> done) override
    {
        ++cancelled;
        setOutputHandler({});
        setErrorHandler({});
        if (deferred)
        {
            acknowledgement = std::move(done);
        }
        else if (done)
        {
            done();
        }
    }
};

class RoutingDevice final : public SerialDevice
{
public:
    QByteArray received;
    qint64     allowance = 65536;
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
        const qint64 accepted  = qMin<qint64>(allowance, bytes.size());
        received              += bytes.first(accepted);
        return accepted;
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
    void duplexCrossPortAndIdleRouting()
    {
        ProcessingRouter router;
        QByteArray       received;
        QByteArray       transmitted;
        QList<bool>      retention;
        router.setOutput(ProcessingRouter::Direction::Rx, [&](const auto& chunk, const auto&, QString*) {
            if (chunk.port != 0)
            {
                return Processor::AcceptResult::Rejected;
            }
            received += chunk.bytes;
            return Processor::AcceptResult::Accepted;
        });
        router.setOutput(ProcessingRouter::Direction::Tx, [&](const auto& chunk, const auto& options, QString*) {
            transmitted += chunk.bytes;
            retention.append(options.retainOnDisconnect);
            return Processor::AcceptResult::Accepted;
        });
        auto protocol = std::make_shared<DuplexProbe>();
        router.replaceDuplex(protocol);
        QTRY_VERIFY(router.duplex());
        TerminalSendQueue::Options options;
        options.retainOnDisconnect = true;
        QCOMPARE(router.submit(ProcessingRouter::Direction::Rx, {QByteArray("R\0X", 3)}, options),
                 Processor::AcceptResult::Accepted);
        QTRY_COMPARE(received, QByteArray("R\0X", 3));
        QTRY_COMPARE(transmitted, QByteArray("ACK"));
        QCOMPARE(retention, QList<bool> {false});
        QCOMPARE(router.submit(ProcessingRouter::Direction::Tx, {QByteArray("command")}, options),
                 Processor::AcceptResult::Accepted);
        QTRY_COMPARE(transmitted, QByteArray("ACKcommand"));
        QVERIFY(retention.last());
        protocol->unsolicited(1, "timer");
        QTRY_COMPARE(transmitted, QByteArray("ACKcommandtimer"));
        QVERIFY(!retention.last());
        router.cancel(ProcessingRouter::Direction::Tx);
        QTRY_COMPARE(protocol->cancelled, 1);
        QVERIFY(!router.duplex());
        protocol->unsolicited(1, "late");
        QTest::qWait(20);
        QCOMPARE(transmitted, QByteArray("ACKcommandtimer"));
    }

    void duplexReplacementWaitsForBothDirectionsAndCoalesces()
    {
        ProcessingRouter router;
        auto             rx = std::make_shared<DelayedProcessor>();
        auto             tx = std::make_shared<DelayedProcessor>();
        router.replace(ProcessingRouter::Direction::Rx, rx);
        router.replace(ProcessingRouter::Direction::Tx, tx);
        QByteArray output;
        router.setOutput(ProcessingRouter::Direction::Rx, [&](const auto& chunk, const auto&, QString*) {
            output += chunk.bytes;
            return Processor::AcceptResult::Accepted;
        });
        auto first  = std::make_shared<DuplexProbe>();
        auto latest = std::make_shared<DuplexProbe>();
        int  ready  = 0;
        router.replaceDuplex(first, [&] { ++ready; });
        QVERIFY(rx->acknowledgement && tx->acknowledgement);
        router.replaceDuplex(latest, [&] { ++ready; });
        rx->acknowledgement();
        QTest::qWait(10);
        QVERIFY(!router.duplex());
        QCOMPARE(router.submit(ProcessingRouter::Direction::Rx, {QByteArray("queued")}),
                 Processor::AcceptResult::Accepted);
        QTest::qWait(15);
        QVERIFY(output.isEmpty());
        tx->acknowledgement();
        QTRY_VERIFY(router.duplex());
        QTRY_COMPARE(output, QByteArray("queued"));
        QCOMPARE(ready, 2);
        latest->deferred = true;
        router.cancel(ProcessingRouter::Direction::Rx);
        QCOMPARE(latest->cancelled, 1);
        QVERIFY(latest->acknowledgement);
        latest->acknowledgement();
        QTRY_COMPARE(router.diagnostics(ProcessingRouter::Direction::Rx).state, ProcessingRouter::State::Bypass);
        QCOMPARE(first->cancelled, 1);
    }

    void sharedToIndependentWaitsForSharedAcknowledgement()
    {
        ProcessingRouter router;
        auto             protocol = std::make_shared<DuplexProbe>();
        protocol->deferred        = true;
        router.replaceDuplex(protocol);
        QTRY_VERIFY(router.duplex());
        auto rx    = std::make_shared<DelayedProcessor>();
        auto tx    = std::make_shared<DelayedProcessor>();
        int  ready = 0;
        router.replace(ProcessingRouter::Direction::Rx, rx, [&] { ++ready; });
        QSignalSpy rejected(&router, &ProcessingRouter::error);
        router.replace(ProcessingRouter::Direction::Tx, rx);
        QCOMPARE(rejected.size(), 1);
        router.replace(ProcessingRouter::Direction::Tx, tx, [&] { ++ready; });
        QCOMPARE(protocol->cancelled, 1);
        QCOMPARE(router.submit(ProcessingRouter::Direction::Rx, {QByteArray("rx")}), Processor::AcceptResult::Accepted);
        QCOMPARE(router.submit(ProcessingRouter::Direction::Tx, {QByteArray("tx")}), Processor::AcceptResult::Accepted);
        QTest::qWait(20);
        QVERIFY(!rx->callbacks.completed && !tx->callbacks.completed);
        QCOMPARE(ready, 0);
        QVERIFY(protocol->acknowledgement);
        protocol->acknowledgement();
        QTRY_COMPARE(ready, 2);
        QTRY_VERIFY(rx->callbacks.completed && tx->callbacks.completed);
        QCOMPARE(rx->input.bytes, QByteArray("rx"));
        QCOMPARE(tx->input.bytes, QByteArray("tx"));
        router.cancel(ProcessingRouter::Direction::Rx);
        rx->acknowledgement();
        router.cancel(ProcessingRouter::Direction::Tx);
        tx->acknowledgement();
    }

    void duplexIdleBoundsAndInvalidPortStopBothDirections()
    {
        ProcessingRouter router({4, 2, 4, 1});
        auto             protocol = std::make_shared<DuplexProbe>();
        router.replaceDuplex(protocol);
        QTRY_VERIFY(router.duplex());
        protocol->unsolicited(1, "12345");
        QCOMPARE(router.diagnostics(ProcessingRouter::Direction::Rx).state, ProcessingRouter::State::Failed);
        QCOMPARE(router.diagnostics(ProcessingRouter::Direction::Tx).state, ProcessingRouter::State::Failed);
        QTRY_COMPARE(protocol->cancelled, 1);
        auto invalid = std::make_shared<DuplexProbe>();
        router.replaceDuplex(invalid);
        QTRY_VERIFY(router.duplex());
        invalid->unsolicited(9, "x");
        QCOMPARE(router.diagnostics(ProcessingRouter::Direction::Tx).state, ProcessingRouter::State::Failed);
        QVERIFY(router.diagnostics(ProcessingRouter::Direction::Rx).lastError.contains("port"));
    }

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
        QList<bool>             retention;
        bool                    blocked = true;
        router.setOutput(direction, [&](const auto& chunk, const auto& options, QString*) {
            if (blocked)
            {
                return Processor::AcceptResult::WouldBlock;
            }
            received.append(chunk);
            retention.append(options.retainOnDisconnect);
            return Processor::AcceptResult::Accepted;
        });
        TerminalSendQueue::Options options;
        options.retainOnDisconnect = true;
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
        QCOMPARE(retention, QList<bool>({true, true}));
        router.cancel(direction);
        processor->acknowledgement();
        QCoreApplication::processEvents();
    }

    void streamFragmentsOwnBytesAndPreservePacketsAndOptions()
    {
        ProcessingRouter        router;
        const auto              tx = ProcessingRouter::Direction::Tx;
        QList<Processor::Chunk> received;
        QList<bool>             retention;
        router.setOutput(tx, [&](const auto& chunk, const auto& options, QString*) {
            received.append(chunk);
            retention.append(options.retainOnDisconnect);
            return Processor::AcceptResult::Accepted;
        });
        QByteArray storage(4096, '\0');
        for (qsizetype index = 0; index < storage.size(); ++index)
        {
            storage[index] = static_cast<char>(index % 251);
        }
        const QByteArray           expected(storage.constData(), storage.size());
        TerminalSendQueue::Options options;
        options.retainOnDisconnect = true;
        QCOMPARE(router.submit(tx, {QByteArray::fromRawData(storage.constData(), storage.size())}, options),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(router.pendingSubmissions(tx), 4);
        QCOMPARE(router.diagnostics(tx).acceptedBytes, quint64(4096));
        storage.fill('x');
        QTRY_COMPARE(received.size(), 4);
        QByteArray joined;
        for (const auto& chunk : received)
        {
            QCOMPARE(chunk.bytes.size(), 1024);
            QCOMPARE(chunk.boundary, Processor::Boundary::StreamChunk);
            joined += chunk.bytes;
        }
        QCOMPARE(joined, expected);
        QCOMPARE(retention, QList<bool>({true, true, true, true}));

        received.clear();
        retention.clear();
        storage = QByteArray(4096, 'p');
        QCOMPARE(router.submit(tx,
                               {QByteArray::fromRawData(storage.constData(), storage.size()),
                                Processor::Boundary::Packet}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(router.pendingSubmissions(tx), 1);
        storage.fill('z');
        QCOMPARE(router.submit(tx, {{}, Processor::Boundary::StreamChunk}), Processor::AcceptResult::Accepted);
        QCOMPARE(router.submit(tx, {{}, Processor::Boundary::Packet}), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(received.size(), 3);
        QCOMPARE(received[0].bytes, QByteArray(4096, 'p'));
        QCOMPARE(received[0].boundary, Processor::Boundary::Packet);
        QVERIFY(received[1].bytes.isEmpty());
        QCOMPARE(received[1].boundary, Processor::Boundary::StreamChunk);
        QVERIFY(received[2].bytes.isEmpty());
        QCOMPARE(received[2].boundary, Processor::Boundary::Packet);
    }

    void fragmentAdmissionIsAtomicAndCountsActiveInput()
    {
        ProcessingRouter::Limits limits;
        limits.maxChunks = 2;
        ProcessingRouter router(limits);
        const auto       tx        = ProcessingRouter::Direction::Tx;
        auto             processor = std::make_shared<DelayedProcessor>();
        router.replace(tx, processor);
        QString error;
        QCOMPARE(router.submit(tx, {QByteArray(2049, 'x')}, {}, &error), Processor::AcceptResult::Rejected);
        QVERIFY(error.contains("chunk limit"));
        QCOMPARE(router.pendingSubmissions(tx), 0);
        QCOMPARE(router.pendingBytes(tx), 0);
        QCOMPARE(router.diagnostics(tx).acceptedBytes, quint64(0));
        QCOMPARE(router.submit(tx, {QByteArray(2048, 'y')}), Processor::AcceptResult::Accepted);
        QTRY_VERIFY(bool(processor->callbacks.completed));
        QCOMPARE(processor->input.bytes.size(), 1024);
        QCOMPARE(router.pendingSubmissions(tx), 2);
        QCOMPARE(router.submit(tx, {QByteArray("late")}), Processor::AcceptResult::WouldBlock);
        QCOMPARE(router.pendingBytes(tx), 2048);
        QCOMPARE(router.diagnostics(tx).acceptedBytes, quint64(2048));
        router.cancel(tx);
        processor->acknowledgement();
        QTRY_COMPARE(router.pendingSubmissions(tx), 0);
    }

    void defaultIngressAdmitsFullByteBudget()
    {
        ProcessingRouter router;
        const auto       tx = ProcessingRouter::Direction::Tx;
        const QByteArray bytes(4 * 1024 * 1024, 'f');
        QCOMPARE(router.submit(tx, {bytes}), Processor::AcceptResult::Accepted);
        QCOMPARE(router.pendingSubmissions(tx), 4096);
        QCOMPARE(router.pendingBytes(tx), bytes.size());
        QCOMPARE(router.diagnostics(tx).acceptedBytes, quint64(bytes.size()));
        router.cancel(tx);
        QCOMPARE(router.pendingSubmissions(tx), 0);
    }

    void largeLineFilePassesThroughNativePacer()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        auto    pacer = registry.create("native.pacer", {}, {}, &error);
        QVERIFY2(pacer, qPrintable(error));

        RoutingDevice     device;
        TerminalSendQueue queue(device);
        ProcessingRouter  router;
        AdvancedSender    sender(queue, device);
        const auto        tx = ProcessingRouter::Direction::Tx;
        router.replace(tx, pacer);
        router.setOutput(tx, [&](const auto& chunk, const auto& options, QString* outputError) {
            return queue.enqueue(chunk.bytes, options, outputError) ? Processor::AcceptResult::Accepted :
                                                                      Processor::AcceptResult::Rejected;
        });
        sender.setProcessingRouter(&router);
        connect(&sender, &AdvancedSender::stopped, &router, [&] { router.cancel(tx); });
        QSignalSpy     failures(&sender, &AdvancedSender::failed);
        QTemporaryFile file;
        QVERIFY(file.open());
        QByteArray bytes(2 * 1024 * 1024, 'x');
        bytes[100] = '\0';
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        QVERIFY(file.flush());
        const QByteArray terminator("\0!", 2);
        QVERIFY(sender.startFile(file.fileName(), true, terminator));
        QTRY_VERIFY_WITH_TIMEOUT(!sender.active(), 10000);
        QCOMPARE(failures.count(), 0);
        QCOMPARE(router.diagnostics(tx).failures, quint64(0));
        QCOMPARE(device.received, bytes + terminator);
        QCOMPARE(router.diagnostics(tx).acceptedBytes, quint64(bytes.size() + terminator.size()));
        QCOMPARE(queue.pendingBytes(), 0);
        sender.stop();
        QTRY_COMPARE(router.diagnostics(tx).state, ProcessingRouter::State::Bypass);
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

    void nativePacerPeriodicBackpressureAndStop()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        auto    pacer = registry.create("native.pacer",
                                        {
                                            {"byteDelayMs", 30}
        },
                                        {},
                                        &error);
        QVERIFY2(pacer, qPrintable(error));

        RoutingDevice device;
        device.allowance = 1;
        TerminalSendQueue queue(device);
        ProcessingRouter  router;
        AdvancedSender    sender(queue, device);
        const auto        tx = ProcessingRouter::Direction::Tx;
        router.replace(tx, pacer);
        bool blocked = true;
        router.setOutput(tx, [&](const auto& chunk, const auto& options, QString* outputError) {
            if (blocked)
            {
                return Processor::AcceptResult::WouldBlock;
            }
            return queue.enqueue(chunk.bytes, options, outputError) ? Processor::AcceptResult::Accepted :
                                                                      Processor::AcceptResult::Rejected;
        });
        sender.setProcessingRouter(&router);
        connect(&sender, &AdvancedSender::stopped, &router, [&] { router.cancel(tx); });
        const QByteArray bytes("A\0B", 3);
        QVERIFY(sender.startPeriodic(bytes, 100));
        QTRY_COMPARE(router.diagnostics(tx).acceptedBytes, quint64(3));
        QTest::qWait(220);
        QCOMPARE(router.diagnostics(tx).acceptedBytes, quint64(3));
        QVERIFY(router.pendingSubmissions(tx) > 0);
        QVERIFY(device.received.isEmpty());
        QCOMPARE(queue.pendingSubmissions(), 0);

        blocked = false;
        router.wake(tx);
        QTRY_COMPARE(device.received, bytes);
        sender.stop();
        QTRY_COMPARE(router.pendingSubmissions(tx), 0);
        QVERIFY(!sender.active());
        QCOMPARE(queue.pendingSubmissions(), 0);
        QTest::qWait(200);
        QCOMPARE(device.received, bytes);
    }

    void nativePacerFileCancellationDiscardsScheduledBytes()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        auto    pacer = registry.create("native.pacer",
                                        {
                                            {"byteDelayMs", 1}
        },
                                        {},
                                        &error);
        QVERIFY2(pacer, qPrintable(error));

        RoutingDevice     device;
        TerminalSendQueue queue(device);
        ProcessingRouter  router;
        AdvancedSender    sender(queue, device);
        const auto        tx = ProcessingRouter::Direction::Tx;
        router.replace(tx, pacer);
        router.setOutput(tx, [&](const auto& chunk, const auto& options, QString* outputError) {
            return queue.enqueue(chunk.bytes, options, outputError) ? Processor::AcceptResult::Accepted :
                                                                      Processor::AcceptResult::Rejected;
        });
        sender.setProcessingRouter(&router);
        connect(&sender, &AdvancedSender::stopped, &router, [&] { router.cancel(tx); });
        QTemporaryFile file;
        QVERIFY(file.open());
        QByteArray bytes(4096, 'f');
        bytes[1] = '\0';
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        QVERIFY(file.flush());
        QVERIFY(sender.startFile(file.fileName(), false, {}));
        QTRY_VERIFY(!device.received.isEmpty());
        QCOMPARE(router.diagnostics(tx).failures, quint64(0));
        QCOMPARE(router.diagnostics(tx).acceptedBytes, quint64(4096));
        QVERIFY(router.pendingSubmissions(tx) > 0);
        QVERIFY(sender.active());
        const QByteArray prefix = device.received;
        QVERIFY(bytes.startsWith(prefix));
        QVERIFY(prefix.size() < bytes.size());
        sender.stop();
        QTRY_COMPARE(router.pendingSubmissions(tx), 0);
        QVERIFY(!sender.active());
        QTest::qWait(350);
        QCOMPARE(device.received, prefix);
        QCOMPARE(router.diagnostics(tx).failures, quint64(0));
        QCOMPARE(queue.pendingBytes(), 0);
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
