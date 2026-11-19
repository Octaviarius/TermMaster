#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <Termaster/Core/Processing/Codeflow.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ThreadedProcessor.h>
#include <atomic>
#include <functional>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#endif

namespace
{
bool waitUntil(const std::function<bool()>& condition, int milliseconds = 7000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds)
    {
        if (condition())
        {
            return true;
        }
        QTest::qWait(10);
    }
    return condition();
}

bool alive(qint64 pid)
{
    if (pid <= 0)
    {
        return false;
    }
#ifdef Q_OS_WIN
    const auto handle = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!handle)
    {
        return false;
    }
    const bool running = WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
    CloseHandle(handle);
    return running;
#else
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

QString source(const QString& body = "return data", const QString& declarations = {}, const QString& imports = {})
{
    return imports
         + "\nfrom termaster import Processor, Property, Input, Output, Node, handles\n"
           "class Codeflow(Processor):\n"
         + declarations
         + "    incoming = Input(bytes)\n    outgoing = Output(bytes)\n"
           "    @handles(incoming, output=outgoing)\n    async def process(self, data):\n        "
         + body + "\n";
}

struct Invocation
{
    QList<Processor::Chunk> outputs;
    QString                 error;
    bool                    completed = false;
    Processor::AcceptResult accepted  = Processor::AcceptResult::Rejected;

    QByteArray joined() const
    {
        QByteArray bytes;
        for (const auto& output : outputs)
        {
            bytes += output.bytes;
        }
        return bytes;
    }
};

std::shared_ptr<Invocation> invoke(Processor& processor, const QByteArray& data, int port = 0)
{
    auto result = std::make_shared<Invocation>();
    waitUntil([&] {
        result->accepted = processor.accept({data, port},
                                            {[result](Processor::Chunk output) {
                                                 result->outputs.append(std::move(output));
                                             },
                                             [result](QString error) {
                                                 result->error     = std::move(error);
                                                 result->completed = true;
                                             }});
        return result->accepted != Processor::AcceptResult::WouldBlock;
    });
    if (result->accepted == Processor::AcceptResult::Accepted)
    {
        waitUntil([&] { return result->completed; });
    }
    return result;
}

/** Synthetic backpressure consumes nothing until the second admission attempt. */
class BusyOnceProcessor final : public Processor
{
public:
    explicit BusyOnceProcessor(std::shared_ptr<std::atomic_int> attempts) : _attempts(std::move(attempts))
    {
    }
    QVariantMap configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        if (++*_attempts == 1)
        {
            return AcceptResult::WouldBlock;
        }
        callbacks.output(std::move(input));
        callbacks.completed({});
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> done) override
    {
        if (done)
        {
            done();
        }
    }

private:
    std::shared_ptr<std::atomic_int> _attempts;
};

class Fixture
{
public:
    ProcessorRegistry                 registry;
    QString                           error;
    QList<std::shared_ptr<Processor>> instances;

    Fixture()
    {
        registerNativeProcessors(registry, "native");
        Codeflow::registerProvider(registry);
    }

    ~Fixture()
    {
        for (const auto& processor : instances)
        {
            auto stopped = std::make_shared<bool>(false);
            processor->cancel([stopped] { *stopped = true; });
            waitUntil([stopped] { return *stopped; });
        }
        instances.clear();
        registry.shutdown(7000, &error);
    }

    QVariantMap definition(const QString& code, const QString& id = "script", QVariantMap options = {})
    {
        options.insert("code", code);
        options.insert("python", Codeflow::defaultPython());
        const auto description = Codeflow::inspect(options, &error);
        if (description.isEmpty())
        {
            return {};
        }
        return Codeflow::definition(id, id, options, description);
    }

    bool publish(const QVariantMap& definitions)
    {
        const auto parsed = ProcessorRegistry::parseDefinitions(definitions, &error);
        return parsed && registry.publishDefinitions("workspace", *parsed, &error);
    }

    std::shared_ptr<const ProcessorRegistry::Prepared> prepare(const QString& code, QVariantMap options = {})
    {
        const auto item = definition(code, "script", options);
        if (item.isEmpty()
            || !publish({
                {"script", item}
        }))
        {
            return {};
        }
        return registry.prepare("script", {}, {}, &error);
    }

    std::shared_ptr<Processor> instantiate(std::shared_ptr<const ProcessorRegistry::Prepared> prepared)
    {
        auto processor = ThreadedProcessor::create(std::move(prepared), {}, &error);
        if (processor)
        {
            instances.append(processor);
        }
        return processor;
    }
};
} // namespace

class CodeflowTests final : public QObject
{
    Q_OBJECT
private slots:
    void inlineNativeCatalogAndRoundtrip()
    {
        Fixture fixture;
        int     count = 0;
        for (const auto& type : fixture.registry.types())
        {
            if (Codeflow::isInline(type.id))
            {
                ++count;
                QCOMPARE(type.group, QString("General"));
                QCOMPARE(type.editor.value("inlineCode").toBool(), true);
                QCOMPARE(type.editor.value("channels").toInt(), type.inputCount);
                QCOMPARE(type.editor.value("helpTopic").toString(), QString("processing.codeflow"));
                const auto baseline = Codeflow::inlineDefinition("draft", "Custom", type.inputCount, type.defaults);
                QCOMPARE(baseline.value("kind").toString(), QString("codeflow"));
                QCOMPARE(baseline.value("source").toMap().value("description").toMap().value("inputs").toList().size(),
                         type.inputCount);
                const auto configuration = Codeflow::inlineConfiguration(baseline);
                QVERIFY(!configuration.value("source").toMap().contains("digest"));
                QCOMPARE(Codeflow::inlineConfiguration(
                             Codeflow::inlineDefinition("draft", "Custom", type.inputCount, configuration)),
                         configuration);
            }
        }
        QCOMPARE(count, 2);
        const auto embedded = Codeflow::inlineDefinition("draft",
                                                         "Custom",
                                                         1,
                                                         {
                                                             {"source",
                                                              QVariantMap {{"code", Codeflow::templateSource(1)},
                                                                           {"file", "missing.py"},
                                                                           {"digest", "old"}}}
        });
        QVERIFY(!embedded.value("source").toMap().contains("file"));
        QVERIFY(!embedded.value("source").toMap().contains("digest"));
        QVERIFY(!Codeflow::isInline("native.bypass"));
        QCOMPARE(fixture.registry.type("native.python-code")->title, QString::fromUtf8("Python Code (1×1)"));
        QCOMPARE(fixture.registry.type("native.duplex-python-code")->title, QString::fromUtf8("Python Code (2×2)"));
    }

    void inlineNativeBinaryAndPairedPackets()
    {
        Fixture fixture;
        for (const auto& id : QStringList {"native.python-code", "native.duplex-python-code"})
        {
            const auto prepared = fixture.registry.prepare(id,
                                                           {
                                                               {"parameters", QVariantMap {{"prefix", "P"}}}
            },
                                                           {},
                                                           &fixture.error);
            QVERIFY2(prepared, qPrintable(fixture.error));
            const auto processor = fixture.instantiate(prepared);
            QVERIFY2(processor, qPrintable(fixture.error));
            const auto rx = invoke(*processor, QByteArray("\0x", 2));
            QVERIFY2(rx->completed && rx->error.isEmpty(), qPrintable(rx->error));
            QCOMPARE(rx->joined(), QByteArray("P\0x", 3));
            QCOMPARE(rx->outputs.first().port, 0);
            if (processor->inputCount() == 2)
            {
                const auto tx = invoke(*processor, {}, 1);
                QVERIFY2(tx->completed && tx->error.isEmpty(), qPrintable(tx->error));
                QCOMPARE(tx->outputs.size(), 1);
                QVERIFY(tx->outputs.first().bytes.isEmpty());
                QCOMPARE(tx->outputs.first().port, 1);
            }
        }
    }

    void inlineNativeOccurrencesShareHostButNotState()
    {
        Fixture    fixture;
        const auto code = source(
            "self.count = getattr(self, 'count', 0) + 1\n        return data + str(os.getpid()).encode() + b':' + str(self.count).encode() + b'|'",
            {},
            "import os");
        const auto checked = fixture.definition(code);
        QVERIFY2(!checked.isEmpty(), qPrintable(fixture.error));
        const auto         configuration = Codeflow::inlineConfiguration(checked);
        const QVariantList nodes {
            QVariantMap { {"name", "first"}, {"type", "native.python-code"}, {"configuration", configuration}},
            QVariantMap {{"name", "second"}, {"type", "native.python-code"}, {"configuration", configuration}}
        };
        const QVariantMap graph {
            {    "id",                        "graph"},
            {  "name",                        "Graph"},
            {  "kind",                     "lineflow"},
            {"source", QVariantMap {{"nodes", nodes}}}
        };
        QVERIFY2(fixture.publish({
                     {"graph", graph}
        }),
                 qPrintable(fixture.error));
        const auto prepared = fixture.registry.prepare("graph", {}, {}, &fixture.error);
        QVERIFY2(prepared, qPrintable(fixture.error));
        const auto processor = fixture.instantiate(prepared);
        QVERIFY2(processor, qPrintable(fixture.error));
        for (int iteration = 1; iteration <= 2; ++iteration)
        {
            const auto result = invoke(*processor, {});
            QVERIFY2(result->completed && result->error.isEmpty(), qPrintable(result->error));
            const auto parts = result->joined().split('|');
            QCOMPARE(parts.size(), 3);
            QCOMPARE(parts[0], parts[1]);
            QCOMPARE(parts[0].split(':').last().toInt(), iteration);
            QVERIFY(parts[0].split(':').first().toLongLong() > 0);
        }
    }

    void inlineNativeChildrenRejectedExplicitly()
    {
        Fixture    fixture;
        const auto checked = fixture.definition(source("return data", "    child = Node('native.bypass')\n"));
        QVERIFY2(!checked.isEmpty(), qPrintable(fixture.error));
        const auto prepared = fixture.registry.prepare("native.python-code",
                                                       Codeflow::inlineConfiguration(checked),
                                                       {},
                                                       &fixture.error);
        QVERIFY2(prepared, qPrintable(fixture.error));
        const auto processor = fixture.instantiate(prepared);
        QVERIFY(!processor);
        QVERIFY2(fixture.error.contains("does not support declared Node children"), qPrintable(fixture.error));
    }

    void initTestCase()
    {
        QVERIFY2(!Codeflow::defaultPython().isEmpty(), "A selected CPython interpreter is required");
        QVERIFY2(QFile::exists(Codeflow::hostScript()), qPrintable(Codeflow::hostScript()));
    }

    void inheritedTypedMetadataAndConditions()
    {
        const auto code = QString::fromUtf8(R"PY(
from pathlib import Path
from enum import Enum
from termaster import *
class Mode(Enum):
    FIXED = 'fixed'
    VARIABLE = 'variable'
class Base(Processor):
    enabled = Property(bool, default=True)
    mode = Property(Mode, default=Mode.FIXED)
    path = Property(Path, default='packets.bin', mode='save')
    incoming = Input()
    outgoing = Output()
    @handles(incoming, output=outgoing)
    def process(self, data): return data
class Codeflow(Base):
    width = Property(int, default=2, minimum=1, maximum=8, when=Base.mode.is_('fixed'))
    ratio = Property(float, default=1.5)
)PY");
        QString    error;
        const auto description = Codeflow::inspect(
            {
                {"code", code}
        },
            &error);
        QVERIFY2(!description.isEmpty(), qPrintable(error));
        QCOMPARE(description.value("className").toString(), QString("Codeflow"));
        const auto defaults = description.value("defaults").toMap();
        QCOMPARE(defaults.value("enabled").toBool(), true);
        QCOMPARE(defaults.value("mode").toString(), QString("fixed"));
        QCOMPARE(defaults.value("width").toInt(), 2);
        QCOMPARE(defaults.value("ratio").toDouble(), 1.5);
        const auto schema = description.value("schema").toMap();
        QCOMPARE(schema.value("oneOf").toList().size(), 2);
        QCOMPARE(schema.value("properties").toMap().value("path").toMap().value("x-widget").toString(),
                 QString("filePath"));
    }

    void binaryPacketsManualEmissionAndStateIsolation()
    {
        Fixture    fixture;
        const auto prepared = fixture.prepare(
            source("self.count = getattr(self, 'count', 0) + 1\n        return [b'', data, bytes([self.count])]"));
        QVERIFY2(prepared, qPrintable(fixture.error));
        const auto first  = fixture.instantiate(prepared);
        const auto second = fixture.instantiate(prepared);
        QVERIFY2(first && second, qPrintable(fixture.error));
        auto result = invoke(*first, QByteArray("A\0\xff", 3));
        QVERIFY2(result->completed && result->error.isEmpty(), qPrintable(result->error));
        QCOMPARE(result->outputs.size(), 3);
        QCOMPARE(result->outputs[0].bytes, QByteArray());
        QCOMPARE(result->outputs[1].bytes, QByteArray("A\0\xff", 3));
        QCOMPARE(result->outputs[2].bytes, QByteArray(1, char(1)));
        result = invoke(*first, {});
        QCOMPARE(result->outputs[2].bytes, QByteArray(1, char(2)));
        result = invoke(*second, {});
        QCOMPARE(result->outputs[2].bytes, QByteArray(1, char(1)));

        Fixture    explicitFixture;
        const auto explicitPrepared = explicitFixture.prepare(source("self.outgoing.emit(data)\n        return None"));
        QVERIFY2(explicitPrepared, qPrintable(explicitFixture.error));
        const auto explicitProcessor = explicitFixture.instantiate(explicitPrepared);
        QVERIFY2(explicitProcessor, qPrintable(explicitFixture.error));
        result = invoke(*explicitProcessor, {});
        QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
        QCOMPARE(result->outputs.size(), 1);
        QVERIFY(result->outputs[0].bytes.isEmpty());
    }

    void pairedRxTxRouting()
    {
        Fixture    fixture;
        const auto code     = QString::fromUtf8(R"PY(
from termaster import *
class Codeflow(Processor):
    tx = Input(flow='tx')
    txout = Output(flow='tx')
    rx = Input(flow='rx')
    rxout = Output(flow='rx')
    @handles(tx, output=txout)
    def send(self, data): return b'tx:' + data
    @handles(rx, output=rxout)
    def receive(self, data): return b'rx:' + data
)PY");
        const auto prepared = fixture.prepare(code);
        QVERIFY2(prepared, qPrintable(fixture.error));
        const auto processor = fixture.instantiate(prepared);
        QVERIFY2(processor, qPrintable(fixture.error));
        const auto rx = invoke(*processor, QByteArray("\0", 1), 0);
        const auto tx = invoke(*processor, {}, 1);
        QVERIFY2(rx->error.isEmpty() && tx->error.isEmpty(), qPrintable(rx->error + tx->error));
        QCOMPARE(rx->joined(), QByteArray("rx:\0", 4));
        QCOMPARE(tx->joined(), QByteArray("tx:"));
        QCOMPARE(rx->outputs.first().port, 0);
        QCOMPARE(tx->outputs.first().port, 1);
    }

    void independentHostsAndSharedCompositionHost()
    {
        Fixture    fixture;
        const auto code     = source("return data + str(os.getpid()).encode() + b'|'", {}, "import os");
        const auto prepared = fixture.prepare(code);
        QVERIFY2(prepared, qPrintable(fixture.error));
        const auto first  = fixture.instantiate(prepared);
        const auto second = fixture.instantiate(prepared);
        QVERIFY2(first && second, qPrintable(fixture.error));
        const auto firstPid  = invoke(*first, {})->joined().split('|').first().toLongLong();
        const auto secondPid = invoke(*second, {})->joined().split('|').first().toLongLong();
        QVERIFY(firstPid > 0 && secondPid > 0);
        QVERIFY(firstPid != secondPid);

        Fixture    composition;
        const auto script = composition.definition(code);
        QVERIFY2(!script.isEmpty(), qPrintable(composition.error));
        const QVariantList nodes {
            QVariantMap { {"name", "first"}, {"type", "script"}},
            QVariantMap {{"name", "second"}, {"type", "script"}}
        };
        const QVariantMap graph {
            {    "id",                        "graph"},
            {  "name",                        "Graph"},
            {  "kind",                     "lineflow"},
            {"source", QVariantMap {{"nodes", nodes}}}
        };
        QVERIFY2(composition.publish({
                     {"script", script},
                     { "graph",  graph}
        }),
                 qPrintable(composition.error));
        const auto graphPrepared = composition.registry.prepare("graph", {}, {}, &composition.error);
        QVERIFY2(graphPrepared, qPrintable(composition.error));
        const auto runtime = composition.instantiate(graphPrepared);
        QVERIFY2(runtime, qPrintable(composition.error));
        const auto result = invoke(*runtime, {});
        QVERIFY2(result->completed && result->error.isEmpty(), qPrintable(result->error));
        const auto pids = result->joined().split('|');
        QCOMPARE(pids.size(), 3);
        QCOMPARE(pids[0], pids[1]);
        QVERIFY(pids[0].toLongLong() > 0);
    }

    void nativeChildRpcAndAsyncStartupTimer()
    {
        Fixture fixture;
        const auto
            code = source("encoded = await self.hex.process(data)\n        return self.seed + b''.join(encoded)",
                          "    pace = Node('native.pacer', byteDelayMs=20)\n"
                          "    hex = Node('native.hex-encoder', separator='')\n"
                          "    async def start(self):\n        self.seed = b''.join(await self.pace.process(b'ab'))\n");
        const auto prepared = fixture.prepare(code);
        QVERIFY2(prepared, qPrintable(fixture.error));
        const auto processor = fixture.instantiate(prepared);
        QVERIFY2(processor, qPrintable(fixture.error));
        const auto result = invoke(*processor, QByteArray("\0\xab", 2));
        QVERIFY2(result->completed && result->error.isEmpty(), qPrintable(result->error));
        QCOMPARE(result->joined(), QByteArray("ab00AB"));
    }

    void nativeChildRetriesTransientBackpressure()
    {
        Fixture                fixture;
        const auto             attempts = std::make_shared<std::atomic_int>(0);
        ProcessorCatalog::Type type;
        type.id    = "test.busy-once";
        type.title = "Synthetic transient backpressure";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Synthetic backpressure\ncontent:\n  type: object\n  properties: {}\n";
        type.create = [attempts](const QVariantMap&, const Processor::Limits&, QString*) {
            return std::make_unique<BusyOnceProcessor>(attempts);
        };
        fixture.registry.registerType("test", std::move(type));
        const auto prepared = fixture.prepare(
            source("return await self.child.process(data)", "    child = Node('test.busy-once')\n"));
        QVERIFY2(prepared, qPrintable(fixture.error));
        const auto processor = fixture.instantiate(prepared);
        QVERIFY2(processor, qPrintable(fixture.error));
        const QByteArray packet("A\0\xff", 3);
        const auto       result = invoke(*processor, packet);
        QVERIFY2(result->completed && result->error.isEmpty(), qPrintable(result->error));
        QCOMPARE(result->joined(), packet);
        QCOMPARE(attempts->load(), 2);
    }

    void nestedCodeflowRetriesUntilChildStartupCompletes()
    {
        Fixture    fixture;
        const auto child  = fixture.definition(source("return b'child:' + data",
                                                      "    async def start(self):\n        await asyncio.sleep(0.15)\n",
                                                      "import asyncio"),
                                               "slow-child");
        const auto parent = fixture.definition(
            source("return self.seed + data",
                   "    child = Node('slow-child')\n"
                   "    async def start(self):\n        self.seed = b''.join(await self.child.process(b'\\x00'))\n"),
            "parent");
        QVERIFY2(!child.isEmpty() && !parent.isEmpty(), qPrintable(fixture.error));
        QVERIFY2(fixture.publish({
                     {"slow-child",  child},
                     {    "parent", parent}
        }),
                 qPrintable(fixture.error));
        const auto prepared = fixture.registry.prepare("parent", {}, {}, &fixture.error);
        QVERIFY2(prepared, qPrintable(fixture.error));
        const auto processor = fixture.instantiate(prepared);
        QVERIFY2(processor, qPrintable(fixture.error));
        const auto result = invoke(*processor, QByteArray("\xff", 1));
        QVERIFY2(result->completed && result->error.isEmpty(), qPrintable(result->error));
        QCOMPARE(result->joined(), QByteArray("child:\0\xff", 8));
    }

    void stdoutContaminationCannotAlterFrames()
    {
        Fixture    fixture;
        const auto prepared = fixture.prepare(source(
            "print('packet diagnostic')\n        os.write(1, b'native packet diagnostic')\n        return data",
            {},
            "import os, sys, zlib, sqlite3\nprint('import diagnostic')\nos.write(1, b'native import diagnostic')\nassert sys.stdin.read() == ''"));
        QVERIFY2(prepared, qPrintable(fixture.error));
        const auto processor = fixture.instantiate(prepared);
        QVERIFY2(processor, qPrintable(fixture.error));
        const auto result = invoke(*processor, QByteArray("\0x", 2));
        QVERIFY2(result->completed && result->error.isEmpty(), qPrintable(result->error));
        QCOMPARE(result->joined(), QByteArray("\0x", 2));
    }

    void executionDeadlines_data()
    {
        QTest::addColumn<QString>("body");
        QTest::newRow("python-loop") << QString("while True: pass");
        QTest::newRow("blocked-native-call") << QString("time.sleep(60)\n        return data");
    }

    void executionDeadlines()
    {
        QFETCH(QString, body);
        Fixture    fixture;
        const auto prepared = fixture.prepare(source(body,
                                                     {
        },
                                                     "import time"),
                                              {{"timeoutMs", 150}});
        QVERIFY2(prepared, qPrintable(fixture.error));
        const auto processor = fixture.instantiate(prepared);
        QVERIFY2(processor, qPrintable(fixture.error));
        QElapsedTimer timer;
        timer.start();
        const auto result = invoke(*processor, {});
        QVERIFY(result->completed);
        QVERIFY(!result->error.isEmpty());
        QVERIFY2(timer.elapsed() < 5000, "The independent host watchdog must bound blocked Python/native calls");
    }

    void cancellationReapsHostAndOwnedChildren()
    {
        Fixture fixture;
        QString declarations;
        QString body = "return str(os.getpid()).encode()";
#ifdef Q_OS_WIN
        declarations
            = "    def start(self):\n        self.child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])\n";
        body += " + b'|' + str(self.child.pid).encode()";
#endif
        const auto prepared = fixture.prepare(source(body, declarations, "import os, subprocess, sys"));
        QVERIFY2(prepared, qPrintable(fixture.error));
        const auto processor = fixture.instantiate(prepared);
        QVERIFY2(processor, qPrintable(fixture.error));
        const auto result = invoke(*processor, {});
        QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
        const auto ids = result->joined().split('|');
        QVERIFY(!ids.isEmpty());
        QList<qint64> pids;
        for (const auto& id : ids)
        {
            const auto pid = id.toLongLong();
            QVERIFY(pid > 0);
            QVERIFY(alive(pid));
            pids.append(pid);
        }
        auto stopped = std::make_shared<bool>(false);
        processor->cancel([stopped] { *stopped = true; });
        QVERIFY(waitUntil([stopped] { return *stopped; }));
        for (const auto pid : pids)
        {
            QVERIFY2(waitUntil([pid] { return !alive(pid); }),
                     qPrintable(QString("Process %1 survived cancellation").arg(pid)));
        }
    }

    void snapshotMissingFileAndPublishedDigestChange()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto file = directory.filePath("missing.py");
        QString    error;
        const auto description = Codeflow::inspect(
            {
                {"code", source()},
                {"file",     file}
        },
            &error);
        QVERIFY2(!description.isEmpty(), qPrintable(error));

        QFile script(file);
        QVERIFY(script.open(QIODevice::WriteOnly));
        const auto code = source();
        QCOMPARE(script.write(code.toUtf8()), code.toUtf8().size());
        script.close();
        Fixture    fixture;
        const auto prepared = fixture.prepare(code,
                                              {
                                                  {"file", file}
        });
        QVERIFY2(prepared, qPrintable(fixture.error));
        QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(script.write("raise RuntimeError('changed source')\n") > 0);
        script.close();
        const auto processor = fixture.instantiate(prepared);
        if (!processor)
        {
            QVERIFY2(fixture.error.contains("changed", Qt::CaseInsensitive), qPrintable(fixture.error));
            return;
        }
        const auto result = invoke(*processor, {});
        QVERIFY2(!result->error.isEmpty(), "Changed published source must fail before execution");
    }

    void metadataPublicationDoesNotImportPython()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Fixture fixture;
        auto    definition = fixture.definition(source());
        QVERIFY2(!definition.isEmpty(), qPrintable(fixture.error));
        const auto marker        = directory.filePath("imported.txt");
        const auto markerLiteral = QString::fromUtf8("r'%1'").arg(QString(marker).replace("'", "\\'"));
        auto       stored        = definition.value("source").toMap();
        stored.insert("code",
                      "from pathlib import Path\nPath(" + markerLiteral + ").write_text('imported')\n" + source());
        definition.insert("source", stored);
        QVERIFY2(fixture.publish({
                     {"script", definition}
        }),
                 qPrintable(fixture.error));
        QVERIFY(!QFile::exists(marker));
        const auto properties = fixture.registry.properties("script", {}, {}, &fixture.error);
        QVERIFY2(properties, qPrintable(fixture.error));
        QVERIFY(!QFile::exists(marker));
        const auto prepared = fixture.registry.prepare("script", {}, {}, &fixture.error);
        QVERIFY2(prepared, qPrintable(fixture.error));
        QVERIFY(!QFile::exists(marker));
        QString error;
        QVERIFY2(!Codeflow::inspect(stored, &error).isEmpty(), qPrintable(error));
        QVERIFY(QFile::exists(marker));
    }
};

QTEST_GUILESS_MAIN(CodeflowTests)
#include "CodeflowTests.moc"
