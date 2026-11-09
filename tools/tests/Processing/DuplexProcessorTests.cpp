#include <QPointer>
#include <QTest>
#include <QTimer>
#include <Termaster/Core/Processing/Lineflow.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <memory>
#include <stdexcept>

namespace
{
QByteArray emptySchema()
{
    return "version: 1\ntype: termaster.settings-schema\ndescription: Synthetic processor\ncontent: {type: object, properties: {}}\n";
}
QVariantMap node(const QString& type, const QString& name, const QString& lane = "rx")
{
    return {
        {         "name",           name},
        {         "type",           type},
        {         "lane",           lane},
        {"configuration", QVariantMap {}}
    };
}
ProcessorRegistry::Definition block(const QString& id, QVariantList nodes, int channels = 2)
{
    return {
        id,
        id,
        "lineflow",
        {{"channels", channels}, {"nodes", nodes}},
        {},
        {},
        {},
        {}
    };
}
class Mark final : public Processor
{
public:
    explicit Mark(QByteArray marker) : _marker(std::move(marker))
    {
    }
    QVariantMap configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        if (_stopped)
        {
            return AcceptResult::Rejected;
        }
        input.bytes.append(_marker);
        QPointer<Mark> guard(this);
        if (callbacks.output)
        {
            callbacks.output(std::move(input));
        }
        if (guard && !_stopped && callbacks.completed)
        {
            callbacks.completed({});
        }
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> ready) override
    {
        _stopped = true;
        if (ready)
        {
            ready();
        }
    }

private:
    QByteArray _marker;
    bool       _stopped = false;
};
class Protocol final : public Processor
{
public:
    explicit Protocol(int delay = 0) : _delay(delay), _timer(this)
    {
        _timer.setSingleShot(true);
        connect(&_timer, &QTimer::timeout, this, [this] {
            if (_idle)
            {
                _idle = false;
                publishOutput(std::move(_input));
                return;
            }
            _finish();
        });
    }
    int inputCount() const override
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
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        if (_stopped)
        {
            return AcceptResult::Rejected;
        }
        if (_busy)
        {
            return AcceptResult::WouldBlock;
        }
        _busy      = true;
        _input     = std::move(input);
        _callbacks = std::move(callbacks);
        if (_delay > 0)
        {
            _timer.start(_delay);
        }
        else
        {
            _finish();
        }
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> ready) override
    {
        _stopped = true;
        _timer.stop();
        _callbacks = {};
        _input     = {};
        _busy      = false;
        setOutputHandler({});
        setErrorHandler({});
        if (ready)
        {
            ready();
        }
    }
    void scheduleIdle(QByteArray bytes, int port)
    {
        _input = {std::move(bytes), Boundary::Packet, port};
        _idle  = true;
        _timer.start(0);
    }
    void burstIdle(int count, int port)
    {
        for (int index = 0; index < count && !_stopped; ++index)
        {
            publishOutput({QByteArray("idle"), Boundary::Packet, port});
        }
    }
    bool invalidOutput = false;

private:
    int       _delay;
    QTimer    _timer;
    Chunk     _input;
    Callbacks _callbacks;
    int       _received = 0;
    bool      _busy     = false;
    bool      _idle     = false;
    bool      _stopped  = false;

    void _finish()
    {
        auto               input     = std::move(_input);
        auto               callbacks = std::move(_callbacks);
        QPointer<Protocol> guard(this);
        if (input.port == 0)
        {
            ++_received;
        }
        const auto suffix = QByteArray(input.port == 0 ? "/rx" : "/tx") + QByteArray::number(_received);
        input.bytes.append(suffix);
        if (invalidOutput)
        {
            input.port = 2;
        }
        if (callbacks.output)
        {
            const auto inputPort = input.port;
            callbacks.output(std::move(input));
            if (!guard || _stopped)
            {
                return;
            }
            if (inputPort == 0)
            {
                callbacks.output({QByteArray("ack") + QByteArray::number(_received), Boundary::Packet, 1});
                if (!guard || _stopped)
                {
                    return;
                }
            }
        }
        _busy = false;
        if (callbacks.completed)
        {
            callbacks.completed({});
        }
    }
};
class WaitingProtocol final : public Processor
{
public:
    int inputCount() const override
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
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        if (_stopped)
        {
            return AcceptResult::Rejected;
        }
        if (input.port == 1)
        {
            if (txWaiting)
            {
                return AcceptResult::WouldBlock;
            }
            txWaiting  = true;
            _tx        = std::move(input);
            _callbacks = std::move(callbacks);
            return AcceptResult::Accepted;
        }
        auto txCallbacks = std::move(_callbacks);
        txWaiting        = false;
        QPointer<WaitingProtocol> guard(this);
        if (txCallbacks.output)
        {
            txCallbacks.output({QByteArray("sent:") + _tx.bytes, Boundary::Packet, 1});
        }
        if (!guard || _stopped)
        {
            return AcceptResult::Accepted;
        }
        if (txCallbacks.completed)
        {
            txCallbacks.completed({});
        }
        if (!guard || _stopped)
        {
            return AcceptResult::Accepted;
        }
        if (callbacks.completed)
        {
            callbacks.completed({});
        }
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> ready) override
    {
        _stopped   = true;
        txWaiting  = false;
        _callbacks = {};
        _tx        = {};
        if (ready)
        {
            ready();
        }
    }
    bool txWaiting = false;

private:
    Chunk     _tx;
    Callbacks _callbacks;
    bool      _stopped = false;
};
class Flip final : public Processor
{
public:
    explicit Flip(int swapInput, std::shared_ptr<int> work) : _swapInput(swapInput), _work(std::move(work))
    {
    }
    int inputCount() const override
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
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        if (_stopped)
        {
            return AcceptResult::Rejected;
        }
        ++*_work;
        if (input.port == _swapInput)
        {
            input.port = 1 - input.port;
        }
        QPointer<Flip> guard(this);
        if (callbacks.output)
        {
            callbacks.output(std::move(input));
        }
        if (guard && !_stopped && callbacks.completed)
        {
            callbacks.completed({});
        }
        return AcceptResult::Accepted;
    }
    void pulse(int port)
    {
        publishOutput({{}, Boundary::Packet, port});
    }
    void cancel(std::function<void()> ready) override
    {
        _stopped = true;
        setOutputHandler({});
        setErrorHandler({});
        if (ready)
        {
            ready();
        }
    }

private:
    int                  _swapInput;
    std::shared_ptr<int> _work;
    bool                 _stopped = false;
};
class DeferredInvalid final : public Processor
{
public:
    QVariantMap configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk, Callbacks callbacks) override
    {
        if (callbacks.output)
        {
            callbacks.output({QByteArray("invalid"), Boundary::Packet, 1});
        }
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> ready) override
    {
        if (_quiescent)
        {
            if (ready)
            {
                ready();
            }
        }
        else if (ready)
        {
            _acknowledgements.append(std::move(ready));
        }
    }
    void acknowledge()
    {
        _quiescent     = true;
        auto callbacks = std::move(_acknowledgements);
        for (auto& callback : callbacks)
        {
            callback();
        }
    }

private:
    QList<std::function<void()>> _acknowledgements;
    bool                         _quiescent = false;
};
class Shape final : public Processor
{
public:
    Shape(int inputs, int outputs) : inputs(inputs), outputs(outputs)
    {
    }
    int inputCount() const override
    {
        return inputs;
    }
    int outputCount() const override
    {
        return outputs;
    }
    QVariantMap configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        input.port = outputs - 1;
        if (callbacks.output)
        {
            callbacks.output(std::move(input));
        }
        if (callbacks.completed)
        {
            callbacks.completed({});
        }
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> ready) override
    {
        if (ready)
        {
            ready();
        }
    }
    int inputs;
    int outputs;
};
struct Fixture
{
    int                created = 0;
    QPointer<Protocol> protocol;
};
void registerFixtures(ProcessorRegistry& registry, const std::shared_ptr<Fixture>& fixture, int delay = 0)
{
    for (const auto& marker : {"A", "B", "R", "S"})
    {
        ProcessorCatalog::Type type;
        type.id             = QString("test.%1").arg(marker);
        type.title          = marker;
        type.settingsSchema = emptySchema();
        type.create         = [marker](const QVariantMap&, const Processor::Limits&, QString*) {
            return std::make_unique<Mark>(QByteArray(marker));
        };
        registry.registerType("fixture", std::move(type));
    }
    ProcessorCatalog::Type protocol;
    protocol.id             = "test.protocol";
    protocol.title          = "Protocol";
    protocol.settingsSchema = emptySchema();
    protocol.inputCount     = 2;
    protocol.outputCount    = 2;
    protocol.create         = [fixture, delay](const QVariantMap&, const Processor::Limits&, QString*) {
        auto result = std::make_unique<Protocol>(delay);
        ++fixture->created;
        fixture->protocol = result.get();
        return result;
    };
    registry.registerType("fixture", std::move(protocol));
}
} // namespace

class DuplexProcessorTests final : public QObject
{
    Q_OBJECT
private slots:
    void metadataSharedStateCrossPortRoutingAndIdleOutput()
    {
        ProcessorRegistry registry;
        const auto        fixture = std::make_shared<Fixture>();
        registerFixtures(registry, fixture);
        const auto definition = block("duplex",
                                      {node("test.A", "device-tx", "tx"),
                                       node("test.R", "device-rx"),
                                       node("test.protocol", "protocol", "duplex"),
                                       node("test.B", "terminal-tx", "tx"),
                                       node("test.S", "terminal-rx")});
        QString    error;
        QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
        QCOMPARE(registry.type("duplex")->inputCount, 2);
        QCOMPARE(registry.type("duplex")->outputCount, 2);
        auto prepared = registry.prepare("duplex", {}, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        QCOMPARE(prepared->inputCount(), 2);
        QCOMPARE(prepared->outputCount(), 2);
        QCOMPARE(fixture->created, 0);
        auto runtime = prepared->instantiate({}, &error);
        QVERIFY2(runtime, qPrintable(error));
        QCOMPARE(fixture->created, 1);
        QCOMPARE(runtime->inputCount(), 2);
        QList<Processor::Chunk> outputs;
        int                     completed = 0;
        Processor::Callbacks    callbacks {[&](Processor::Chunk output) { outputs.append(std::move(output)); },
                                           [&](QString message) {
                                            QVERIFY2(message.isEmpty(), qPrintable(message));
                                            ++completed;
                                           }};
        QCOMPARE(runtime->accept({QByteArray("q"), Processor::Boundary::StreamChunk, 0}, callbacks),
                 Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 1);
        QCOMPARE(outputs.size(), 2);
        QCOMPARE(outputs[0].bytes, QByteArray("qR/rx1S"));
        QCOMPARE(outputs[0].port, 0);
        QCOMPARE(outputs[1].bytes, QByteArray("ack1A"));
        QCOMPARE(outputs[1].port, 1);
        outputs.clear();
        QCOMPARE(runtime->accept({QByteArray("t"), Processor::Boundary::StreamChunk, 1}, callbacks),
                 Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 2);
        QCOMPARE(outputs.size(), 1);
        QCOMPARE(outputs[0].bytes, QByteArray("tB/tx1A"));
        QCOMPARE(outputs[0].port, 1);
        QList<Processor::Chunk> idle;
        runtime->setOutputHandler([&](Processor::Chunk output) { idle.append(std::move(output)); });
        fixture->protocol->scheduleIdle("idle", 1);
        QTRY_COMPARE(idle.size(), 1);
        QCOMPARE(idle[0].bytes, QByteArray("idleA"));
        QCOMPARE(idle[0].port, 1);
        QCOMPARE(idle[0].boundary, Processor::Boundary::Packet);
        QCOMPARE(completed, 2);
        bool acknowledged = false;
        fixture->protocol->scheduleIdle("cancelled", 0);
        runtime->cancel([&] { acknowledged = true; });
        QVERIFY(acknowledged);
        QTest::qWait(30);
        QCOMPARE(idle.size(), 1);
        QCOMPARE(runtime->accept({QByteArray("late"), Processor::Boundary::StreamChunk, 0}, {}),
                 Processor::AcceptResult::Rejected);
    }

    void independentInputSlotsAllowTxWhileRxIsPaced()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        const auto fixture = std::make_shared<Fixture>();
        registerFixtures(registry, fixture);
        auto pace = node("native.pacer", "rx-pace");
        pace.insert("configuration",
                    QVariantMap {
                        {"byteDelayMs", 130}
        });
        QString error;
        QVERIFY2(registry.publishDefinitions("workspace", {block("split", {pace, node("test.A", "tx", "tx")})}, &error),
                 qPrintable(error));
        auto runtime = registry.create("split", {}, {}, &error);
        QVERIFY2(runtime, qPrintable(error));
        int        rxCompleted = 0;
        int        txCompleted = 0;
        QByteArray rx;
        QByteArray tx;
        QCOMPARE(runtime->accept({QByteArray("abc"), Processor::Boundary::StreamChunk, 0},
                                 {[&](Processor::Chunk output) {
                                      QCOMPARE(output.port, 0);
                                      rx.append(output.bytes);
                                  },
                                  [&](QString error) {
                                      QVERIFY(error.isEmpty());
                                      ++rxCompleted;
                                  }}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(runtime->accept({QByteArray("t"), Processor::Boundary::StreamChunk, 1},
                                 {[&](Processor::Chunk output) {
                                      QCOMPARE(output.port, 1);
                                      tx.append(output.bytes);
                                  },
                                  [&](QString error) {
                                      QVERIFY(error.isEmpty());
                                      ++txCompleted;
                                  }}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(runtime->accept({QByteArray("blocked"), Processor::Boundary::StreamChunk, 0}, {}),
                 Processor::AcceptResult::WouldBlock);
        QTRY_COMPARE_WITH_TIMEOUT(txCompleted, 1, 1500);
        QCOMPARE(rxCompleted, 0);
        QCOMPARE(tx, QByteArray("tA"));
        QTRY_COMPARE_WITH_TIMEOUT(rxCompleted, 1, 2500);
        QCOMPARE(rx, QByteArray("abc"));
        runtime->cancel({});
    }

    void sharedAsyncStageQueuesBothPortsAndCancelsQuiescent()
    {
        ProcessorRegistry registry;
        const auto        fixture = std::make_shared<Fixture>();
        registerFixtures(registry, fixture, 120);
        QString error;
        QVERIFY2(registry.publishDefinitions("workspace",
                                             {block("async", {node("test.protocol", "protocol", "duplex")})},
                                             &error),
                 qPrintable(error));
        auto runtime = registry.create("async", {}, {}, &error);
        QVERIFY2(runtime, qPrintable(error));
        QList<Processor::Chunk> output;
        int                     completed = 0;
        Processor::Callbacks    callbacks {[&](Processor::Chunk chunk) { output.append(std::move(chunk)); },
                                           [&](QString error) {
                                            QVERIFY(error.isEmpty());
                                            ++completed;
                                           }};
        QCOMPARE(runtime->accept({QByteArray("r"), Processor::Boundary::Packet, 0}, callbacks),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(runtime->accept({QByteArray("t"), Processor::Boundary::Packet, 1}, callbacks),
                 Processor::AcceptResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(completed, 2, 2500);
        QCOMPARE(output.size(), 3);
        QCOMPARE(output[0].bytes, QByteArray("r/rx1"));
        QCOMPARE(output[1].bytes, QByteArray("ack1"));
        QCOMPARE(output[2].bytes, QByteArray("t/tx1"));
        runtime->accept({QByteArray("r"), Processor::Boundary::Packet, 0}, callbacks);
        runtime->accept({QByteArray("t"), Processor::Boundary::Packet, 1}, callbacks);
        QCoreApplication::processEvents();
        bool acknowledged = false;
        runtime->cancel([&] { acknowledged = true; });
        QVERIFY(acknowledged);
        QTest::qWait(180);
        QCOMPARE(completed, 2);
        QCOMPARE(output.size(), 3);
    }

    void nestedDuplexAndSingleTxMatchStandalonePreparation()
    {
        ProcessorRegistry registry;
        const auto        fixture = std::make_shared<Fixture>();
        registerFixtures(registry, fixture);
        const auto        single = block("single", {node("test.A", "a"), node("test.B", "b")}, 1);
        const auto        child  = block("child", {node("test.protocol", "protocol", "duplex")});
        const QVariantMap reference {
            {     "name", "single-reference"},
            {"reference",           "single"},
            {     "lane",               "tx"}
        };
        const QVariantMap duplexReference {
            {     "name", "shared"},
            {"reference",  "child"},
            {     "lane", "duplex"}
        };
        const auto root = block("root", {reference, duplexReference, node("test.S", "s")});
        QString    error;
        QVERIFY2(registry.publishDefinitions("workspace", {single, child, root}, &error), qPrintable(error));
        QVariantMap standalone;
        for (const auto& definition : {single, child, root})
        {
            auto source = definition.source;
            source.insert("id", definition.id);
            source.insert("name", definition.title);
            standalone.insert(definition.id, source);
        }
        auto directPrepared = Lineflow::prepare(standalone, "root", registry, {}, &error);
        QVERIFY2(directPrepared, qPrintable(error));
        QCOMPARE(directPrepared->inputCount(), 2);
        for (bool direct : {false, true})
        {
            std::shared_ptr<Processor> runtime;
            if (direct)
            {
                runtime = directPrepared->instantiate(registry, &error);
            }
            else
            {
                runtime = registry.create("root", {}, {}, &error);
            }
            QVERIFY2(runtime, qPrintable(error));
            QList<Processor::Chunk> outputs;
            int                     completed = 0;
            Processor::Callbacks    callbacks {[&](Processor::Chunk output) { outputs.append(std::move(output)); },
                                               [&](QString error) {
                                                QVERIFY2(error.isEmpty(), qPrintable(error));
                                                ++completed;
                                               }};
            runtime->accept({QByteArray("r"), Processor::Boundary::Packet, 0}, callbacks);
            QTRY_COMPARE(completed, 1);
            QCOMPARE(outputs.size(), 2);
            QCOMPARE(outputs[0].bytes, QByteArray("r/rx1S"));
            QCOMPARE(outputs[1].bytes, QByteArray("ack1AB"));
            outputs.clear();
            runtime->accept({QByteArray("t"), Processor::Boundary::Packet, 1}, callbacks);
            QTRY_COMPARE(completed, 2);
            QCOMPARE(outputs.size(), 1);
            QCOMPARE(outputs[0].bytes, QByteArray("t/tx1AB"));
            runtime->cancel({});
        }
        QCOMPARE(fixture->created, 2);
    }

    void validatesCountsTopologyAndPortsWithoutConstructingFactories()
    {
        ProcessorRegistry registry;
        const auto        fixture = std::make_shared<Fixture>();
        registerFixtures(registry, fixture);
        QString error;
        auto    bad = block("bad", {node("test.protocol", "protocol", "rx")});
        QVERIFY(!registry.publishDefinitions("workspace", {bad}, &error));
        QVERIFY(error.contains("shape"));
        bad = block("bad", {node("test.A", "single", "duplex")});
        QVERIFY(!registry.publishDefinitions("workspace", {bad}, &error));
        bad = block("bad", {node("test.A", "single", "tx")}, 1);
        QVERIFY(!registry.publishDefinitions("workspace", {bad}, &error));
        bad = block("bad", {}, 3);
        QVERIFY(!registry.publishDefinitions("workspace", {bad}, &error));
        QCOMPARE(fixture->created, 0);
        ProcessorCatalog::Type invalid;
        invalid.id             = "test.invalid";
        invalid.title          = "Invalid";
        invalid.settingsSchema = emptySchema();
        invalid.create         = [](const QVariantMap&, const Processor::Limits&, QString*) {
            return std::make_unique<Mark>(QByteArray());
        };
        invalid.inputCount = 0;
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("fixture", invalid));
        invalid.inputCount = 33;
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("fixture", invalid));
        invalid.inputCount  = 2;
        invalid.outputCount = 2;
        registry.registerType("fixture", invalid);
        QVERIFY(!registry.create("test.invalid", {}, {}, &error));
        QVERIFY(error.contains("port counts"));
        auto single    = registry.create("test.A", {}, {});
        int  callbacks = 0;
        QCOMPARE(single->accept({QByteArray("x"), Processor::Boundary::Packet, 1},
                                {[&](Processor::Chunk) { ++callbacks; }, [&](QString) { ++callbacks; }}),
                 Processor::AcceptResult::Rejected);
        QCOMPARE(callbacks, 0);
        auto runtime                     = registry.create("test.protocol", {}, {});
        fixture->protocol->invalidOutput = true;
        QList<Processor::Chunk> output;
        QString                 failure;
        int                     completed = 0;
        QCOMPARE(runtime->accept({QByteArray("secret"), Processor::Boundary::Packet, 0},
                                 {[&](Processor::Chunk chunk) { output.append(std::move(chunk)); },
                                  [&](QString error) {
                                      failure = std::move(error);
                                      ++completed;
                                  }}),
                 Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 1);
        QVERIFY(!failure.isEmpty());
        QVERIFY(!failure.contains("secret"));
        QVERIFY(output.isEmpty());
        QCOMPARE(runtime->accept({QByteArray("late"), Processor::Boundary::Packet, 0}, {}),
                 Processor::AcceptResult::Rejected);
    }

    void sharedProtocolCanReceiveAckWhileTxInvocationIsPending()
    {
        ProcessorRegistry         registry;
        QPointer<WaitingProtocol> protocol;
        ProcessorCatalog::Type    type;
        type.id             = "test.waiting";
        type.title          = "Waiting protocol";
        type.settingsSchema = emptySchema();
        type.inputCount     = 2;
        type.outputCount    = 2;
        type.create         = [&](const QVariantMap&, const Processor::Limits&, QString*) {
            auto result = std::make_unique<WaitingProtocol>();
            protocol    = result.get();
            return result;
        };
        registry.registerType("fixture", std::move(type));
        QString error;
        QVERIFY2(registry.publishDefinitions("workspace",
                                             {block("waiting", {node("test.waiting", "protocol", "duplex")})},
                                             &error),
                 qPrintable(error));
        auto runtime = registry.create("waiting", {}, {}, &error);
        QVERIFY2(runtime, qPrintable(error));
        int        txCompleted = 0;
        int        rxCompleted = 0;
        QByteArray sent;
        QCOMPARE(runtime->accept({QByteArray("command"), Processor::Boundary::Packet, 1},
                                 {[&](Processor::Chunk output) {
                                      QCOMPARE(output.port, 1);
                                      sent.append(output.bytes);
                                  },
                                  [&](QString error) {
                                      QVERIFY(error.isEmpty());
                                      ++txCompleted;
                                  }}),
                 Processor::AcceptResult::Accepted);
        QTRY_VERIFY(protocol->txWaiting);
        QCOMPARE(txCompleted, 0);
        QCOMPARE(runtime->accept({QByteArray("ACK"), Processor::Boundary::Packet, 0},
                                 {{},
                                  [&](QString error) {
                                      QVERIFY(error.isEmpty());
                                      ++rxCompleted;
                                  }}),
                 Processor::AcceptResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(txCompleted, 1, 1500);
        QTRY_COMPARE_WITH_TIMEOUT(rxCompleted, 1, 1500);
        QCOMPARE(sent, QByteArray("sent:command"));
        runtime->cancel({});
    }

    void crossPortCyclesHavePerLineageStepBoundsIncludingIdleAndEmptyPackets()
    {
        ProcessorRegistry registry;
        const auto        work = std::make_shared<int>(0);
        QPointer<Flip>    first;
        for (int index = 0; index < 2; ++index)
        {
            ProcessorCatalog::Type type;
            type.id             = QString("test.flip%1").arg(index);
            type.title          = type.id;
            type.settingsSchema = emptySchema();
            type.inputCount     = 2;
            type.outputCount    = 2;
            type.create         = [&, index](const QVariantMap&, const Processor::Limits&, QString*) {
                auto result = std::make_unique<Flip>(index == 0 ? 1 : 0, work);
                if (index == 0)
                {
                    first = result.get();
                }
                return result;
            };
            registry.registerType("fixture", std::move(type));
        }
        QString    error;
        const auto definition = block("bounce", {node("test.flip0", "a", "duplex"), node("test.flip1", "b", "duplex")});
        QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
        Processor::Limits limits;
        limits.maxSteps = 32;
        auto runtime    = registry.create("bounce", {}, limits, &error);
        QVERIFY2(runtime, qPrintable(error));
        QString failure;
        int     completed = 0;
        int     outputs   = 0;
        runtime->accept({{}, Processor::Boundary::Packet, 0},
                        {[&](Processor::Chunk) { ++outputs; },
                         [&](QString error) {
                             failure = std::move(error);
                             ++completed;
                         }});
        QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 1500);
        QVERIFY(failure.contains("step limit"));
        QCOMPARE(outputs, 0);
        QCOMPARE(*work, 32);
        runtime = registry.create("bounce", {}, limits, &error);
        QVERIFY2(runtime, qPrintable(error));
        failure.clear();
        *work = 0;
        runtime->setOutputHandler([&](Processor::Chunk) { ++outputs; });
        runtime->setErrorHandler([&](QString error) { failure = std::move(error); });
        first->pulse(0);
        QTRY_VERIFY_WITH_TIMEOUT(!failure.isEmpty(), 1500);
        QVERIFY(failure.contains("step limit"));
        QCOMPARE(outputs, 0);
        QCOMPARE(*work, 32);
        runtime->cancel({});

        QVERIFY2(registry.publishDefinitions("workspace",
                                             {block("idle-once", {node("test.flip0", "a", "duplex")})},
                                             &error),
                 qPrintable(error));
        limits.maxSteps = 1;
        runtime         = registry.create("idle-once", {}, limits, &error);
        QVERIFY2(runtime, qPrintable(error));
        failure.clear();
        runtime->setOutputHandler([&](Processor::Chunk) { ++outputs; });
        runtime->setErrorHandler([&](QString error) { failure = std::move(error); });
        for (int index = 0; index < 10; ++index)
        {
            first->pulse(1);
        }
        QTRY_COMPARE(outputs, 10);
        QVERIFY(failure.isEmpty());
        runtime->cancel({});
    }

    void explicitCancellationSuppressesDeferredValidationFailureCallbacks()
    {
        ProcessorRegistry         registry;
        QPointer<DeferredInvalid> instance;
        ProcessorCatalog::Type    type;
        type.id             = "test.deferred-invalid";
        type.title          = type.id;
        type.settingsSchema = emptySchema();
        type.create         = [&](const QVariantMap&, const Processor::Limits&, QString*) {
            auto result = std::make_unique<DeferredInvalid>();
            instance    = result.get();
            return result;
        };
        registry.registerType("fixture", std::move(type));
        auto runtime = registry.create("test.deferred-invalid", {}, {});
        QVERIFY(runtime);
        int completed        = 0;
        int idleErrors       = 0;
        int acknowledgements = 0;
        runtime->setErrorHandler([&](QString) { ++idleErrors; });
        QCOMPARE(runtime->accept({QByteArray("input")}, {{}, [&](QString) { ++completed; }}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(completed, 0);
        runtime->cancel([&] { ++acknowledgements; });
        runtime->cancel([&] { ++acknowledgements; });
        QCOMPARE(acknowledgements, 0);
        instance->acknowledge();
        QCOMPARE(acknowledgements, 2);
        QCOMPARE(completed, 0);
        QCOMPARE(idleErrors, 0);
        runtime.reset();
        QTRY_VERIFY(registry.canRemoveOwner("fixture"));
    }

    void generalPortCountsAreMetadataAndRuntimeCountsCannotGrow()
    {
        ProcessorRegistry      registry;
        QPointer<Shape>        instance;
        ProcessorCatalog::Type type;
        type.id             = "test.shape";
        type.title          = type.id;
        type.settingsSchema = emptySchema();
        type.inputCount     = 3;
        type.outputCount    = 5;
        type.create         = [&](const QVariantMap&, const Processor::Limits&, QString*) {
            auto result = std::make_unique<Shape>(3, 5);
            instance    = result.get();
            return result;
        };
        registry.registerType("fixture", std::move(type));
        auto prepared = registry.prepare("test.shape", {}, {});
        QVERIFY(prepared);
        QCOMPARE(prepared->inputCount(), 3);
        QCOMPARE(prepared->outputCount(), 5);
        QVERIFY(!instance);
        auto runtime = prepared->instantiate();
        QVERIFY(runtime);
        int outputPort = -1;
        int completed  = 0;
        QCOMPARE(runtime->accept({QByteArray("payload"), Processor::Boundary::Packet, 2},
                                 {[&](Processor::Chunk output) { outputPort = output.port; },
                                  [&](QString error) {
                                      QVERIFY(error.isEmpty());
                                      ++completed;
                                  }}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(outputPort, 4);
        QCOMPARE(completed, 1);
        instance->inputs = 32;
        QCOMPARE(runtime->inputCount(), 3);
        QString failure;
        runtime->setErrorHandler([&](QString error) { failure = std::move(error); });
        QCOMPARE(runtime->accept({QByteArray("invalid"), Processor::Boundary::Packet, 31}, {}),
                 Processor::AcceptResult::Rejected);
        QVERIFY(failure.contains("counts changed"));
        QString error;
        QVERIFY(!registry.publishDefinitions("workspace",
                                             {block("invalid", {node("test.shape", "shape", "duplex")})},
                                             &error));
        QVERIFY(error.contains("shape"));
    }

    void directPreparedShapeRejectsAReplacedCatalogType()
    {
        ProcessorRegistry registry;
        const auto        fixture = std::make_shared<Fixture>();
        registerFixtures(registry, fixture);
        const QVariantMap document {
            {"root", QVariantMap {{"id", "root"}, {"name", "Root"}, {"nodes", QVariantList {node("test.A", "leaf")}}}}
        };
        QString    error;
        const auto prepared = Lineflow::prepare(document, "root", registry, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        QVERIFY(registry.unregisterType("fixture", "test.A", &error));
        ProcessorCatalog::Type replacement;
        replacement.id             = "test.A";
        replacement.title          = "Replacement";
        replacement.settingsSchema = emptySchema();
        replacement.inputCount     = 2;
        replacement.outputCount    = 2;
        replacement.create         = [](const QVariantMap&, const Processor::Limits&, QString*) {
            return std::make_unique<Protocol>();
        };
        registry.registerType("fixture", std::move(replacement));
        QVERIFY(!prepared->instantiate(registry, &error));
        QVERIFY(error.contains("shape changed"));
    }

    void idleErrorsAndQueueBoundsAreReportedAndSuppressLaterOutput()
    {
        ProcessorRegistry registry;
        const auto        fixture = std::make_shared<Fixture>();
        registerFixtures(registry, fixture);
        auto    runtime = registry.create("test.protocol", {}, {});
        int     outputs = 0;
        QString failure;
        runtime->setOutputHandler([&](Processor::Chunk) { ++outputs; });
        runtime->setErrorHandler([&](QString error) { failure = std::move(error); });
        fixture->protocol->scheduleIdle("secret", 2);
        QTRY_VERIFY(!failure.isEmpty());
        QVERIFY(failure.contains("port"));
        QVERIFY(!failure.contains("secret"));
        QCOMPARE(outputs, 0);
        QString error;
        QVERIFY2(registry.publishDefinitions("workspace",
                                             {block("bounded", {node("test.protocol", "protocol", "duplex")})},
                                             &error),
                 qPrintable(error));
        Processor::Limits limits;
        limits.maxChunks = 1;
        runtime          = registry.create("bounded", {}, limits, &error);
        QVERIFY2(runtime, qPrintable(error));
        failure.clear();
        runtime->setOutputHandler([&](Processor::Chunk) { ++outputs; });
        runtime->setErrorHandler([&](QString error) { failure = std::move(error); });
        fixture->protocol->burstIdle(3, 1);
        QTRY_VERIFY(!failure.isEmpty());
        QVERIFY(failure.contains("protocol"));
        QVERIFY(failure.contains("limit"));
        QTest::qWait(30);
        QCOMPARE(outputs, 0);
    }
};

QTEST_GUILESS_MAIN(DuplexProcessorTests)
#include "DuplexProcessorTests.moc"
