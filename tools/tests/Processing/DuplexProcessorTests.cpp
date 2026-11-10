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
        _input = {std::move(bytes), port};
        _idle  = true;
        _timer.start(0);
    }
    void burstIdle(int count, int port)
    {
        for (int index = 0; index < count && !_stopped; ++index)
        {
            publishOutput({QByteArray("idle"), port});
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
                callbacks.output({QByteArray("ack") + QByteArray::number(_received), 1});
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
            txCallbacks.output({QByteArray("sent:") + _tx.bytes, 1});
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
        publishOutput({{}, port});
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
            callbacks.output({QByteArray("invalid"), 1});
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
    void namedPortsAreMetadataOwnedSnapshotsAndHaveUsefulDefaults()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QCOMPARE(registry.type("native.framer")->inputNames, QStringList {"Stream"});
        QCOMPARE(registry.type("native.framer")->outputNames, QStringList {"Frames"});
        QCOMPARE(registry.type("native.hex-encoder")->inputNames, QStringList {"Bytes"});
        QCOMPARE(registry.type("native.hex-encoder")->outputNames, QStringList {"HEX"});
        auto native = registry.create("native.pacer", {}, {});
        QCOMPARE(native->inputName(0), QString("Data"));
        QCOMPARE(native->outputName(0), QString("Data"));
        QVERIFY(native->inputName(-1).isEmpty());
        QVERIFY(native->outputName(1).isEmpty());
        const auto fixture = std::make_shared<Fixture>();
        registerFixtures(registry, fixture);
        QChar                  inputStorage[] {QChar('W'), QChar('i'), QChar('r'), QChar('e')};
        QChar                  outputStorage[] {QChar('D'), QChar('a'), QChar('t'), QChar('a')};
        ProcessorCatalog::Type type;
        type.id             = "test.named";
        type.title          = type.id;
        type.settingsSchema = emptySchema();
        type.inputCount     = 2;
        type.outputCount    = 2;
        type.inputNames     = {QString::fromRawData(inputStorage, 4), "Application"};
        type.outputNames    = {QString::fromRawData(outputStorage, 4), "Responses"};
        type.create         = [](const QVariantMap&, const Processor::Limits&, QString*) {
            return std::make_unique<Protocol>();
        };
        registry.registerType("named", type);
        inputStorage[0]  = 'Z';
        outputStorage[0] = 'Z';
        QCOMPARE(registry.type("test.named")->inputNames, QStringList({"Wire", "Application"}));
        QCOMPARE(registry.type("test.named")->outputNames, QStringList({"Data", "Responses"}));
        auto namedPrepared = registry.prepare("test.named", {}, {});
        QCOMPARE(namedPrepared->inputNames(), QStringList({"Wire", "Application"}));
        QCOMPARE(namedPrepared->outputNames(), QStringList({"Data", "Responses"}));
        auto named = namedPrepared->instantiate();
        QCOMPARE(named->inputName(0), QString("Wire"));
        QCOMPARE(named->outputName(1), QString("Responses"));
        QString error;
        auto    definition = block("named-flow", {node("test.named", "protocol", "duplex")});
        QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
        QCOMPARE(registry.type("named-flow")->inputNames, QStringList({"Device Rx", "Terminal Tx"}));
        QCOMPARE(registry.type("named-flow")->outputNames, QStringList({"Terminal Rx", "Device Tx"}));
        definition.inputNames  = {"Device", "Terminal"};
        definition.outputNames = {"Display", "Transport"};
        QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
        auto snapshot = registry.prepare("named-flow", {}, {}, &error);
        QVERIFY2(snapshot, qPrintable(error));
        definition.inputNames = {"New device", "New terminal"};
        QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
        QCOMPARE(snapshot->inputNames(), QStringList({"Device", "Terminal"}));
        auto runtime = snapshot->instantiate({}, &error);
        QVERIFY2(runtime, qPrintable(error));
        QCOMPARE(runtime->inputName(0), QString("Device"));
        QCOMPARE(runtime->outputName(1), QString("Transport"));
        auto unary = block("unary", {}, 1);
        QVERIFY2(registry.publishDefinitions("workspace", {unary}, &error), qPrintable(error));
        QCOMPARE(registry.type("unary")->inputNames, QStringList {"Input"});
        QCOMPARE(registry.type("unary")->outputNames, QStringList {"Output"});
        named->cancel({});
        runtime->cancel({});
        native->cancel({});
    }

    void rejectsMalformedPortNamesAndFlippedFlags()
    {
        ProcessorRegistry registry;
        const auto        fixture = std::make_shared<Fixture>();
        registerFixtures(registry, fixture);
        const QList<QStringList> invalid {
            {"only one"},
            {"same", "same"},
            {"", "other"},
            {" padded", "other"},
            {"line\nbreak", "other"},
            {"tab\tname", "other"},
            {QString(129, 'a'), "other"},
            {QString(QChar::Null), "other"},
            {QString(QChar(0x2028)), "other"}
        };
        QString error;
        for (const auto& names : invalid)
        {
            auto definition       = block("bad-names", {});
            definition.inputNames = names;
            QVERIFY(!registry.publishDefinitions("workspace", {definition}, &error));
            QVERIFY(error.contains("names"));
            ProcessorCatalog::Type type;
            type.id             = "test.invalid-name";
            type.title          = type.id;
            type.settingsSchema = emptySchema();
            type.inputCount     = 2;
            type.outputCount    = 2;
            type.inputNames     = names;
            type.create         = [](const QVariantMap&, const Processor::Limits&, QString*) {
                return std::make_unique<Protocol>();
            };
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("fixture", std::move(type)));
            QVariantMap standalone {
                {"root",
                 QVariantMap {{"id", "root"},
                              {"name", "Root"},
                              {"channels", 2},
                              {"inputNames", names},
                              {"nodes", QVariantList {}}}}
            };
            QVERIFY(!Lineflow::prepare(standalone, "root", registry, {}, &error));
        }
        for (const auto& flag : {QVariant(1), QVariant("true")})
        {
            auto entry = node("test.protocol", "protocol", "duplex");
            entry.insert("flipped", flag);
            QVERIFY(!registry.publishDefinitions("workspace", {block("invalid", {entry})}, &error));
        }
        auto unary = node("test.A", "single");
        unary.insert("flipped", true);
        QVERIFY(!registry.publishDefinitions("workspace", {block("invalid", {unary})}, &error));
        unary.insert("flipped", false);
        QVERIFY2(registry.publishDefinitions("workspace", {block("valid", {unary})}, &error), qPrintable(error));
        QCOMPARE(fixture->created, 0);
    }

    void flippedProtocolSwapsRoutingForSolicitedAndIdleOutputs()
    {
        ProcessorRegistry registry;
        const auto        fixture = std::make_shared<Fixture>();
        registerFixtures(registry, fixture);
        auto protocol = node("test.protocol", "protocol", "duplex");
        protocol.insert("flipped", true);
        const auto definition = block("flipped",
                                      {node("test.A", "device-tx", "tx"),
                                       node("test.R", "device-rx"),
                                       protocol,
                                       node("test.B", "terminal-tx", "tx"),
                                       node("test.S", "terminal-rx")});
        QString    error;
        QVERIFY2(registry.publishDefinitions("workspace", {definition}, &error), qPrintable(error));
        auto runtime = registry.create("flipped", {}, {}, &error);
        QVERIFY2(runtime, qPrintable(error));
        QList<Processor::Chunk> outputs;
        int                     completed = 0;
        Processor::Callbacks    callbacks {[&](Processor::Chunk output) { outputs.append(std::move(output)); },
                                           [&](QString error) {
                                            QVERIFY2(error.isEmpty(), qPrintable(error));
                                            ++completed;
                                           }};
        runtime->accept({QByteArray("r"), 0}, callbacks);
        QTRY_COMPARE(completed, 1);
        QCOMPARE(outputs.size(), 1);
        QCOMPARE(outputs[0].bytes, QByteArray("rR/tx0S"));
        QCOMPARE(outputs[0].port, 0);
        outputs.clear();
        runtime->accept({QByteArray("t"), 1}, callbacks);
        QTRY_COMPARE(completed, 2);
        QCOMPARE(outputs.size(), 2);
        QCOMPARE(outputs[0].bytes, QByteArray("tB/rx1A"));
        QCOMPARE(outputs[0].port, 1);
        QCOMPARE(outputs[1].bytes, QByteArray("ack1S"));
        QCOMPARE(outputs[1].port, 0);
        outputs.clear();
        runtime->setOutputHandler([&](Processor::Chunk output) { outputs.append(std::move(output)); });
        fixture->protocol->scheduleIdle("idle", 0);
        QTRY_COMPARE(outputs.size(), 1);
        QCOMPARE(outputs[0].bytes, QByteArray("idleA"));
        QCOMPARE(outputs[0].port, 1);
        outputs.clear();
        fixture->protocol->scheduleIdle("idle", 1);
        QTRY_COMPARE(outputs.size(), 1);
        QCOMPARE(outputs[0].bytes, QByteArray("idleS"));
        QCOMPARE(outputs[0].port, 0);
        QCOMPARE(fixture->created, 1);
        runtime->cancel({});
    }

    void nestedFlippedBlockMatchesDirectFlatteningAndPreservesNames()
    {
        ProcessorRegistry registry;
        const auto        fixture = std::make_shared<Fixture>();
        registerFixtures(registry, fixture);
        auto child       = block("child",
                                 {node("test.R", "receive"),
                                  node("test.protocol", "protocol", "duplex"),
                                  node("test.B", "send", "tx")});
        auto root        = block("root",
                                 {
                                     QVariantMap {{"name", "reverse"},
                                                  {"reference", "child"},
                                                  {"lane", "duplex"},
                                                  {"flipped", true}}
        });
        root.inputNames  = {"Incoming", "Outgoing"};
        root.outputNames = {"Received", "Sent"};
        QString error;
        QVERIFY2(registry.publishDefinitions("workspace", {child, root}, &error), qPrintable(error));
        QVariantMap directDocument;
        for (const auto& definition : {child, root})
        {
            auto source = definition.source;
            source.insert("id", definition.id);
            source.insert("name", definition.title);
            source.insert("inputNames", definition.inputNames);
            source.insert("outputNames", definition.outputNames);
            directDocument.insert(definition.id, source);
        }
        auto directPrepared = Lineflow::prepare(directDocument, "root", registry, {}, &error);
        QVERIFY2(directPrepared, qPrintable(error));
        QCOMPARE(directPrepared->inputNames(), root.inputNames);
        QCOMPARE(directPrepared->outputNames(), root.outputNames);
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
            QCOMPARE(runtime->inputName(0), QString("Incoming"));
            QCOMPARE(runtime->outputName(1), QString("Sent"));
            QList<Processor::Chunk> outputs;
            int                     completed = 0;
            Processor::Callbacks    callbacks {[&](Processor::Chunk output) { outputs.append(std::move(output)); },
                                               [&](QString error) {
                                                QVERIFY2(error.isEmpty(), qPrintable(error));
                                                ++completed;
                                               }};
            runtime->accept({QByteArray("r"), 0}, callbacks);
            QTRY_COMPARE(completed, 1);
            QCOMPARE(outputs.size(), 1);
            QCOMPARE(outputs[0].bytes, QByteArray("rB/tx0"));
            QCOMPARE(outputs[0].port, 0);
            outputs.clear();
            runtime->accept({QByteArray("t"), 1}, callbacks);
            QTRY_COMPARE(completed, 2);
            QCOMPARE(outputs.size(), 2);
            QCOMPARE(outputs[0].bytes, QByteArray("tR/rx1"));
            QCOMPARE(outputs[0].port, 1);
            QCOMPARE(outputs[1].bytes, QByteArray("ack1"));
            QCOMPARE(outputs[1].port, 0);
            runtime->cancel({});
        }
        QCOMPARE(fixture->created, 2);
    }

    void flippedSharedProtocolAcceptsAckOnOppositeLogicalInput()
    {
        ProcessorRegistry         registry;
        QPointer<WaitingProtocol> protocol;
        ProcessorCatalog::Type    type;
        type.id             = "test.waiting-flipped";
        type.title          = type.id;
        type.settingsSchema = emptySchema();
        type.inputCount     = 2;
        type.outputCount    = 2;
        type.inputNames     = {"Acknowledgements", "Commands"};
        type.outputNames    = {"Received", "Sent"};
        type.create         = [&](const QVariantMap&, const Processor::Limits&, QString*) {
            auto result = std::make_unique<WaitingProtocol>();
            protocol    = result.get();
            return result;
        };
        registry.registerType("fixture", std::move(type));
        auto entry = node("test.waiting-flipped", "protocol", "duplex");
        entry.insert("flipped", true);
        QString error;
        QVERIFY2(registry.publishDefinitions("workspace", {block("reverse-wait", {entry})}, &error), qPrintable(error));
        auto runtime = registry.create("reverse-wait", {}, {}, &error);
        QVERIFY2(runtime, qPrintable(error));
        int        commandCompleted = 0;
        int        ackCompleted     = 0;
        QByteArray sent;
        runtime->accept({QByteArray("command"), 0},
                        {[&](Processor::Chunk output) {
                             QCOMPARE(output.port, 0);
                             sent.append(output.bytes);
                         },
                         [&](QString error) {
                             QVERIFY(error.isEmpty());
                             ++commandCompleted;
                         }});
        QTRY_VERIFY(protocol->txWaiting);
        runtime->accept({QByteArray("ACK"), 1}, {{}, [&](QString error) {
                                                     QVERIFY(error.isEmpty());
                                                     ++ackCompleted;
                                                 }});
        QTRY_COMPARE_WITH_TIMEOUT(commandCompleted, 1, 1500);
        QTRY_COMPARE(ackCompleted, 1);
        QCOMPARE(sent, QByteArray("sent:command"));
        runtime->cancel({});
    }

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
        QCOMPARE(runtime->accept({QByteArray("q"), 0}, callbacks), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 1);
        QCOMPARE(outputs.size(), 2);
        QCOMPARE(outputs[0].bytes, QByteArray("qR/rx1S"));
        QCOMPARE(outputs[0].port, 0);
        QCOMPARE(outputs[1].bytes, QByteArray("ack1A"));
        QCOMPARE(outputs[1].port, 1);
        outputs.clear();
        QCOMPARE(runtime->accept({QByteArray("t"), 1}, callbacks), Processor::AcceptResult::Accepted);
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

        QCOMPARE(completed, 2);
        bool acknowledged = false;
        fixture->protocol->scheduleIdle("cancelled", 0);
        runtime->cancel([&] { acknowledged = true; });
        QVERIFY(acknowledged);
        QTest::qWait(30);
        QCOMPARE(idle.size(), 1);
        QCOMPARE(runtime->accept({QByteArray("late"), 0}, {}), Processor::AcceptResult::Rejected);
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
        QCOMPARE(runtime->accept({QByteArray("abc"), 0},
                                 {[&](Processor::Chunk output) {
                                      QCOMPARE(output.port, 0);
                                      rx.append(output.bytes);
                                  },
                                  [&](QString error) {
                                      QVERIFY(error.isEmpty());
                                      ++rxCompleted;
                                  }}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(runtime->accept({QByteArray("t"), 1},
                                 {[&](Processor::Chunk output) {
                                      QCOMPARE(output.port, 1);
                                      tx.append(output.bytes);
                                  },
                                  [&](QString error) {
                                      QVERIFY(error.isEmpty());
                                      ++txCompleted;
                                  }}),
                 Processor::AcceptResult::Accepted);
        QCOMPARE(runtime->accept({QByteArray("blocked"), 0}, {}), Processor::AcceptResult::WouldBlock);
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
        QCOMPARE(runtime->accept({QByteArray("r"), 0}, callbacks), Processor::AcceptResult::Accepted);
        QCOMPARE(runtime->accept({QByteArray("t"), 1}, callbacks), Processor::AcceptResult::Accepted);
        QTRY_COMPARE_WITH_TIMEOUT(completed, 2, 2500);
        QCOMPARE(output.size(), 3);
        QCOMPARE(output[0].bytes, QByteArray("r/rx1"));
        QCOMPARE(output[1].bytes, QByteArray("ack1"));
        QCOMPARE(output[2].bytes, QByteArray("t/tx1"));
        runtime->accept({QByteArray("r"), 0}, callbacks);
        runtime->accept({QByteArray("t"), 1}, callbacks);
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
            runtime->accept({QByteArray("r"), 0}, callbacks);
            QTRY_COMPARE(completed, 1);
            QCOMPARE(outputs.size(), 2);
            QCOMPARE(outputs[0].bytes, QByteArray("r/rx1S"));
            QCOMPARE(outputs[1].bytes, QByteArray("ack1AB"));
            outputs.clear();
            runtime->accept({QByteArray("t"), 1}, callbacks);
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
        QCOMPARE(single->accept({QByteArray("x"), 1},
                                {[&](Processor::Chunk) { ++callbacks; }, [&](QString) { ++callbacks; }}),
                 Processor::AcceptResult::Rejected);
        QCOMPARE(callbacks, 0);
        auto runtime                     = registry.create("test.protocol", {}, {});
        fixture->protocol->invalidOutput = true;
        QList<Processor::Chunk> output;
        QString                 failure;
        int                     completed = 0;
        QCOMPARE(runtime->accept({QByteArray("secret"), 0},
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
        QCOMPARE(runtime->accept({QByteArray("late"), 0}, {}), Processor::AcceptResult::Rejected);
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
        QCOMPARE(runtime->accept({QByteArray("command"), 1},
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
        QCOMPARE(runtime->accept({QByteArray("ACK"), 0},
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
        runtime->accept({{}, 0},
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
        QCOMPARE(runtime->accept({QByteArray("payload"), 2},
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
        QCOMPARE(runtime->accept({QByteArray("invalid"), 31}, {}), Processor::AcceptResult::Rejected);
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
