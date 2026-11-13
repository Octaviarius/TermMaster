#include <QPointer>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ThreadedProcessor.h>
#include <atomic>
#include <stdexcept>

namespace
{
struct Observation
{
    QThread*              owner                 = nullptr;
    std::atomic<int>      constructed           = 0;
    std::atomic<int>      destroyed             = 0;
    std::atomic<bool>     workerOwned           = false;
    std::atomic<bool>     timerOwned            = false;
    std::atomic<QThread*> executionThread       = nullptr;
    std::atomic<bool>     sharedExecutionThread = true;
    std::atomic<bool>     timerCallbacksOwned   = true;
    std::atomic<bool>     packetCallsOwned      = true;
    std::atomic<int>      timedInstances        = 0;
    std::atomic<bool>     threadExitComplete    = false;
    QChar                 borrowedText[4] {QChar('s'), QChar('e'), QChar('e'), QChar('d')};
    QChar                 borrowedKey[4] {QChar('n'), QChar('a'), QChar('m'), QChar('e')};
    char                  borrowedBytes[4] {'a', 0, 'b', 1};
};

struct ThreadExitProbe
{
    std::shared_ptr<Observation> observation;
    ~ThreadExitProbe()
    {
        QThread::msleep(40);
        observation->threadExitComplete = true;
    }
};

struct FactoryRetirement
{
    QThread*          owner       = nullptr;
    std::atomic<int>  destroyed   = 0;
    std::atomic<bool> ownerThread = true;
};

struct FactoryCapture
{
    std::shared_ptr<FactoryRetirement> observation;
    ~FactoryCapture()
    {
        if (QThread::currentThread() != observation->owner)
        {
            observation->ownerThread = false;
        }
        ++observation->destroyed;
    }
};

class Fixture final : public Processor
{
public:
    Fixture(QVariantMap configuration, std::shared_ptr<Observation> observation) :
        _configuration(std::move(configuration)), _observation(std::move(observation)), _timer(this)
    {
        ++_observation->constructed;
        if (_configuration.value("threadExitProbe").toBool())
        {
            thread_local std::unique_ptr<ThreadExitProbe> probe;
            probe              = std::make_unique<ThreadExitProbe>();
            probe->observation = _observation;
        }
        QThread* expected = nullptr;
        if (!_observation->executionThread.compare_exchange_strong(expected, QThread::currentThread())
            && expected != QThread::currentThread())
        {
            _observation->sharedExecutionThread = false;
        }
        _observation->workerOwned = QThread::currentThread() != _observation->owner;
        _observation->timerOwned  = _timer.thread() == thread();
        _timer.setInterval(1);
        connect(&_timer, &QTimer::timeout, this, [this] {
            if (QThread::currentThread() != _observation->executionThread.load() || _timer.thread() != thread())
            {
                _observation->timerCallbacksOwned = false;
            }
            if (!_timerObserved)
            {
                _timerObserved = true;
                ++_observation->timedInstances;
            }
            publishOutput({QByteArray("tick"), 1});
        });
        if (_configuration.value("traffic").toBool())
        {
            _timer.start();
        }
    }
    ~Fixture() override
    {
        ++_observation->destroyed;
    }
    QVariantMap configuration() const override
    {
        if (_configuration.value("throwStartup").toBool())
        {
            throw std::runtime_error("fixture configuration failed");
        }
        auto configuration = _configuration;
        if (_configuration.value("borrowedConfiguration").toBool())
        {
            configuration.insert("borrowed",
                                 QVariantMap {
                                     {QString::fromRawData(_observation->borrowedKey, 4),
                                      QString::fromRawData(_observation->borrowedText, 4)},
                                     {"bytes", QByteArray::fromRawData(_observation->borrowedBytes, 4)}
            });
        }
        return configuration;
    }
    int inputCount() const override
    {
        return 2;
    }
    int outputCount() const override
    {
        return 2;
    }
    AcceptResult accept(Chunk chunk, Callbacks callbacks) override
    {
        if (QThread::currentThread() != _observation->executionThread.load())
        {
            _observation->packetCallsOwned = false;
        }
        if (_configuration.value("throwAccept").toBool())
        {
            throw std::runtime_error("fixture invocation failed");
        }
        QThread::msleep(_configuration.value("delay", 0).toUInt());
        if (_stopped)
        {
            return AcceptResult::Rejected;
        }
        if (callbacks.output)
        {
            callbacks.output(std::move(chunk));
        }
        if (callbacks.completed)
        {
            callbacks.completed({});
        }
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> ready) override
    {
        _stopped = true;
        _timer.stop();
        // A second retirement cancellation also acknowledges asynchronously.
        QTimer::singleShot(5, this, [ready = std::move(ready)] {
            if (ready)
            {
                ready();
            }
        });
    }

private:
    QVariantMap                  _configuration;
    std::shared_ptr<Observation> _observation;
    QTimer                       _timer;
    bool                         _stopped       = false;
    bool                         _timerObserved = false;
};

std::shared_ptr<Observation> registerFixture(ProcessorRegistry& registry)
{
    auto observation   = std::make_shared<Observation>();
    observation->owner = QThread::currentThread();
    ProcessorCatalog::Type type;
    type.id             = "fixture";
    type.title          = "Fixture";
    type.settingsSchema = QByteArray(
        "version: 1\ntype: termaster.settings-schema\ndescription: Fixture\ncontent:\n  type: object\n  properties: {}\n");
    type.inputCount  = 2;
    type.outputCount = 2;
    type.inputNames  = {"Receive", "Transmit"};
    type.outputNames = {"Display", "Device"};
    type.create      = [observation](const QVariantMap& configuration, const Processor::Limits&, QString*) {
        return std::make_unique<Fixture>(configuration, observation);
    };
    registry.registerType("fixture-owner", std::move(type));
    return observation;
}
} // namespace

class ThreadedProcessorTests final : public QObject
{
    Q_OBJECT
private slots:
    void workerReleasedPreparationsRetireExecutableCapturesOnCatalogOwner()
    {
        ProcessorRegistry registry;
        auto              observation = registerFixture(registry);
        auto              retirement  = std::make_shared<FactoryRetirement>();
        retirement->owner             = QThread::currentThread();
        ProcessorCatalog::Provider provider;
        provider.prepare = [observation, retirement](const QVariantMap&,
                                                     const QVariantMap&,
                                                     const ProcessorCatalog::Provider::References&,
                                                     const Processor::Limits&,
                                                     QString*) {
            auto capture         = std::make_shared<FactoryCapture>();
            capture->observation = retirement;
            return ProcessorCatalog::Type::Factory {
                [capture, observation](const QVariantMap& configuration, const Processor::Limits&, QString*) {
                    return std::make_unique<Fixture>(configuration, observation);
                }};
        };
        registry.registerProvider("retire-owner", "retirement", std::move(provider));
        ProcessorRegistry::Definition definition;
        definition.id    = "saved";
        definition.title = "Saved";
        definition.kind  = "retirement";
        QVERIFY(registry.publishDefinitions("workspace", {definition}));
        auto scope = registry.scoped({});
        QVERIFY(scope);
        std::vector<std::shared_ptr<const ProcessorRegistry::Prepared>> snapshots;
        constexpr int                                                   Count = 100;
        for (int index = 0; index < Count; ++index)
        {
            auto prepared = scope->prepare("saved", {}, {});
            QVERIFY(prepared);
            snapshots.push_back(std::move(prepared));
        }
        scope.reset();
        QVERIFY(registry.unregisterProvider("retire-owner", "retirement"));
        QVERIFY(!registry.canRemoveOwner("retire-owner"));
        auto* worker = QThread::create([snapshots = std::move(snapshots)]() mutable { snapshots.clear(); });
        worker->start();
        QVERIFY(worker->wait(5000));
        delete worker;
        // The releasing worker has finished, but GUI has not drained the retirement events.
        QCOMPARE(retirement->destroyed.load(), 0);
        QVERIFY(!registry.canRemoveOwner("retire-owner"));
        QVERIFY(registry.shutdown(1000));
        QCOMPARE(retirement->destroyed.load(), Count);
        QVERIFY(retirement->ownerThread);
        QTRY_VERIFY(registry.canRemoveOwner("retire-owner"));
        QVERIFY(registry.removeOwner("retire-owner"));
        QVERIFY(registry.shutdown(1000));
    }

    void shutdownDrainsCancelledExecutionAndItsCatalogRetirements()
    {
        ProcessorRegistry registry;
        auto              observation = registerFixture(registry);
        auto              processor   = ThreadedProcessor::create(registry.prepare("fixture",
                                                                                   {
                                                                                       {"delay", 20}
        },
                                                                                   {}));
        QVERIFY(processor);
        QCOMPARE(processor->accept({QByteArray("pending"), 0}, {}), Processor::AcceptResult::Accepted);
        bool quiescent = false;
        processor->cancel([&] { quiescent = true; });
        QString error;
        // Shutdown begins before the worker can publish its final preparation retirement.
        QVERIFY2(registry.shutdown(1000, &error), qPrintable(error));
        QCOMPARE(observation->destroyed.load(), 1);
        QVERIFY(registry.canRemoveOwner("fixture-owner"));
        QTRY_VERIFY(quiescent);
        processor.reset();
    }

    void cachedConfigurationOwnsProviderStorageAfterCancellation()
    {
        ProcessorRegistry registry;
        auto              observation = registerFixture(registry);
        auto              processor   = ThreadedProcessor::create(registry.prepare("fixture",
                                                                                   {
                                                                                       {"borrowedConfiguration", true}
        },
                                                                                   {}));
        QVERIFY(processor);
        bool quiescent = false;
        processor->cancel([&] { quiescent = true; });
        QTRY_VERIFY(quiescent);
        QVERIFY(registry.canRemoveOwner("fixture-owner"));
        QVERIFY(registry.removeOwner("fixture-owner"));
        observation->borrowedText[0]  = QChar('X');
        observation->borrowedKey[0]   = QChar('X');
        observation->borrowedBytes[0] = 'X';
        const auto borrowed           = processor->configuration().value("borrowed").toMap();
        QCOMPARE(borrowed.value("name").toString(), QString("seed"));
        QCOMPARE(borrowed.value("bytes").toByteArray(), QByteArray("a\0b\1", 4));
        processor.reset();
        QVERIFY(registry.shutdown(1000));
    }

    void cancellationAndShutdownWaitForThreadLocalCleanup()
    {
        for (const bool shutdownImmediately : {false, true})
        {
            ProcessorRegistry registry;
            auto              observation = registerFixture(registry);
            auto              processor   = ThreadedProcessor::create(registry.prepare("fixture",
                                                                                       {
                                                                                           {"threadExitProbe", true}
            },
                                                                                       {}));
            QVERIFY(processor);
            bool quiescent = false;
            processor->cancel([&] {
                QVERIFY(observation->threadExitComplete.load());
                quiescent = true;
            });
            if (shutdownImmediately)
            {
                QString error;
                QVERIFY2(registry.shutdown(1000, &error), qPrintable(error));
                QVERIFY(observation->threadExitComplete.load());
                QVERIFY(registry.canRemoveOwner("fixture-owner"));
            }
            QTRY_VERIFY(quiescent);
            QVERIFY(observation->threadExitComplete.load());
            QCOMPARE(observation->destroyed.load(), 1);
            QVERIFY(registry.canRemoveOwner("fixture-owner"));
            QVERIFY(registry.removeOwner("fixture-owner"));
            processor.reset();
            QVERIFY(registry.shutdown(1000));
        }
    }

    void nestedLineflowUsesOneExecutionThreadForItsCompleteDiagram()
    {
        ProcessorRegistry registry;
        auto              observation = registerFixture(registry);
        const auto        node        = [](const QString& name, const QString& type, bool traffic) {
            return QVariantMap {
                {         "name",                                                       name},
                {         "type",                                                       type},
                {         "lane",                                                   "duplex"},
                {"configuration", traffic ? QVariantMap {{"traffic", true}} : QVariantMap {}}
            };
        };
        ProcessorRegistry::Definition inner;
        inner.id          = "inner";
        inner.title       = "Inner";
        inner.kind        = "lineflow";
        inner.inputCount  = 2;
        inner.outputCount = 2;
        inner.source      = {
            {"channels",                                             2},
            {   "nodes", QVariantList {node("first", "fixture", true)}}
        };
        ProcessorRegistry::Definition outer;
        outer.id          = "outer";
        outer.title       = "Outer";
        outer.kind        = "lineflow";
        outer.inputCount  = 2;
        outer.outputCount = 2;
        outer.source      = {
            {"channels",                                                                              2},
            {   "nodes", QVariantList {node("nested", "inner", false), node("second", "fixture", true)}}
        };
        QString error;
        QVERIFY2(registry.publishDefinitions("workspace", {inner, outer}, &error), qPrintable(error));
        // Wrap only the diagram root: nested Lineflows and primitive nodes must share its worker.
        auto processor = ThreadedProcessor::create(registry.prepare("outer", {}, {}, &error), {}, &error);
        QVERIFY2(processor, qPrintable(error));
        QCOMPARE(observation->constructed.load(), 2);
        QVERIFY(observation->executionThread.load());
        QVERIFY(observation->executionThread.load() != QThread::currentThread());
        QVERIFY(observation->sharedExecutionThread);
        QTRY_COMPARE(observation->timedInstances.load(), 2);
        QVERIFY(observation->timerCallbacksOwned);
        QVERIFY(observation->packetCallsOwned);
        QByteArray output;
        int        completed = 0;
        QCOMPARE(processor->accept({QByteArray("a\0b", 3), 0},
                                   {[&](Processor::Chunk chunk) { output.append(chunk.bytes); },
                                    [&](QString failure) {
                                        QVERIFY2(failure.isEmpty(), qPrintable(failure));
                                        ++completed;
                                    }}),
                 Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 1);
        QCOMPARE(output, QByteArray("a\0b", 3));
        QVERIFY(observation->sharedExecutionThread);
        QVERIFY(observation->packetCallsOwned);
        bool quiescent = false;
        processor->cancel([&] { quiescent = true; });
        QTRY_VERIFY(quiescent);
        QCOMPARE(observation->destroyed.load(), 2);
        processor.reset();
        QVERIFY(registry.shutdown(1000));
    }

    void heavyPacketsLeaveOwnerResponsiveAndPreservePorts()
    {
        ProcessorRegistry registry;
        auto              observation = registerFixture(registry);
        auto              prepared    = registry.prepare("fixture",
                                                         {
                                                             {"delay", 100}
        },
                                                         {});
        QVERIFY(prepared);
        QString error;
        auto    processor = ThreadedProcessor::create(prepared,
                                                      {
                                                          {"opaque", "kept"}
        },
                                                      &error);
        QVERIFY2(processor, qPrintable(error));
        QCOMPARE(processor->thread(), QThread::currentThread());
        QVERIFY(observation->workerOwned);
        QVERIFY(observation->timerOwned);
        QCOMPARE(processor->configuration().value("opaque").toString(), QString("kept"));
        QCOMPARE(processor->inputCount(), 2);
        QCOMPARE(processor->outputCount(), 2);
        QCOMPARE(processor->inputName(0), QString("Receive"));
        QCOMPARE(processor->outputName(1), QString("Device"));
        QList<Processor::Chunk> output;
        int                     completions    = 0;
        bool                    callbacksOwned = true;
        auto                    callbacks      = Processor::Callbacks {[&](Processor::Chunk chunk) {
                                                   callbacksOwned &= QThread::currentThread() == observation->owner;
                                                   output.append(std::move(chunk));
                                                                       },
                                                                       [&](QString failure) {
                                                   callbacksOwned &= QThread::currentThread() == observation->owner;
                                                   QVERIFY2(failure.isEmpty(), qPrintable(failure));
                                                   ++completions;
                                                                       }};
        int                     ticks          = 0;
        QTimer                  heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
        heartbeat.start(1);
        char raw[] {'a', 0, 'b'};
        QCOMPARE(processor->accept({QByteArray::fromRawData(raw, 3), 1}, callbacks), Processor::AcceptResult::Accepted);
        raw[0] = 'x';
        QCOMPARE(processor->accept({QByteArray("blocked"), 1}, {}), Processor::AcceptResult::WouldBlock);
        QCOMPARE(processor->accept({{}, 0}, callbacks), Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completions, 2);
        QVERIFY(ticks > 5);
        QVERIFY(callbacksOwned);
        QCOMPARE(output.size(), 2);
        QCOMPARE(output[0].bytes, QByteArray("a\0b", 3));
        QCOMPARE(output[0].port, 1);
        QVERIFY(output[1].bytes.isEmpty());
        QCOMPARE(output[1].port, 0);
        bool quiescent = false;
        processor->cancel([&] { quiescent = true; });
        QTRY_VERIFY(quiescent);
        QCOMPARE(observation->destroyed.load(), 1);
        const auto previous = completions;
        QTest::qWait(20);
        QCOMPARE(completions, previous);
        prepared.reset();
        processor.reset();
        QVERIFY2(registry.shutdown(1000, &error), qPrintable(error));
    }

    void scopedPreparationSurvivesScopeAndCancelsTimerTraffic()
    {
        ProcessorRegistry registry;
        auto              observation = registerFixture(registry);
        auto              scope       = registry.scoped({});
        QVERIFY(scope);
        auto prepared = scope->prepare("fixture",
                                       {
                                           {"traffic", true}
        },
                                       {});
        QVERIFY(prepared);
        scope.reset();
        auto processor = ThreadedProcessor::create(prepared);
        QVERIFY(processor);
        prepared.reset();
        QVERIFY(!registry.canRemoveOwner("fixture-owner"));
        int  outputs        = 0;
        bool ownerCallbacks = true;
        processor->setOutputHandler([&](Processor::Chunk chunk) {
            ownerCallbacks &= QThread::currentThread() == observation->owner;
            QCOMPARE(chunk.bytes, QByteArray("tick"));
            QCOMPARE(chunk.port, 1);
            ++outputs;
        });
        QTRY_VERIFY(outputs > 0);
        QVERIFY(ownerCallbacks);
        int acknowledgements = 0;
        processor->cancel([&] { ++acknowledgements; });
        processor->cancel([&] { ++acknowledgements; });
        QTRY_COMPARE(acknowledgements, 2);
        QCOMPARE(observation->destroyed.load(), 1);
        const auto count = outputs;
        QTest::qWait(30);
        QCOMPARE(outputs, count);
        processor.reset();
        QTRY_VERIFY(registry.canRemoveOwner("fixture-owner"));
        QVERIFY(registry.shutdown(1000));
    }

    void ownerAdmissionIsBoundedIncludingEmptyPackets()
    {
        ProcessorRegistry registry;
        registerFixture(registry);
        Processor::Limits limits;
        limits.maxBytes  = 4;
        limits.maxChunks = 1;
        auto processor   = ThreadedProcessor::create(registry.prepare("fixture",
                                                                      {
                                                                          {"delay", 30}
        },
                                                                      limits));
        QVERIFY(processor);
        QCOMPARE(processor->accept({QByteArray("12345"), 0}, {}), Processor::AcceptResult::Rejected);
        QCOMPARE(processor->accept({{}, 0}, {}), Processor::AcceptResult::Accepted);
        QCOMPARE(processor->accept({{}, 1}, {}), Processor::AcceptResult::WouldBlock);
        bool quiescent = false;
        processor->cancel([&] { quiescent = true; });
        QCOMPARE(processor->accept({{}, 1}, {}), Processor::AcceptResult::Rejected);
        QTRY_VERIFY(quiescent);
        processor.reset();
        QVERIFY(registry.shutdown(1000));
    }

    void timerOutputFloodFailsOnceAndRetiresBeforeAcknowledging()
    {
        ProcessorRegistry registry;
        auto              observation = registerFixture(registry);
        Processor::Limits limits;
        limits.maxBytes  = 4;
        limits.maxChunks = 1;
        auto processor   = ThreadedProcessor::create(registry.prepare("fixture",
                                                                      {
                                                                          {"traffic", true}
        },
                                                                      limits));
        QVERIFY(processor);
        int failures = 0;
        processor->setErrorHandler([&](QString error) {
            QVERIFY(error.contains("queue limit"));
            ++failures;
        });
        // Let the worker fill its delivery queue while the owner deliberately does not drain.
        QThread::msleep(50);
        QTRY_COMPARE(failures, 1);
        bool quiescent = false;
        processor->cancel([&] { quiescent = true; });
        QTRY_VERIFY(quiescent);
        QCOMPARE(observation->destroyed.load(), 1);
        QTest::qWait(10);
        QCOMPARE(failures, 1);
        processor.reset();
        QVERIFY(registry.shutdown(1000));
    }

    void providerExceptionsReportAndRetireWithoutBlockingOwner()
    {
        ProcessorRegistry registry;
        auto              observation = registerFixture(registry);
        QString           error;
        auto              processor = ThreadedProcessor::create(registry.prepare("fixture",
                                                                                 {
                                                                                     {"throwStartup", true}
        },
                                                                                 {}),
                                                                {},
                                                                &error);
        QVERIFY(!processor);
        QVERIFY(error.contains("fixture configuration failed"));
        QTRY_COMPARE(observation->destroyed.load(), 1);

        processor = ThreadedProcessor::create(registry.prepare("fixture",
                                                               {
                                                                   {"throwAccept", true}
        },
                                                               {}));
        QVERIFY(processor);
        int completions = 0;
        QCOMPARE(processor->accept({QByteArray("input"), 0},
                                   {{},
                                    [&](QString failure) {
                                        QVERIFY(failure.contains("fixture invocation failed"));
                                        ++completions;
                                    }}),
                 Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completions, 1);
        bool quiescent = false;
        processor->cancel([&] { quiescent = true; });
        QTRY_VERIFY(quiescent);
        QCOMPARE(observation->destroyed.load(), 2);
        processor.reset();
        QVERIFY(registry.shutdown(1000));
    }

    void cancellationSuppressesAlreadyAdmittedPackets()
    {
        ProcessorRegistry registry;
        auto              observation = registerFixture(registry);
        auto              processor   = ThreadedProcessor::create(registry.prepare("fixture",
                                                                                   {
                                                                                       {"delay", 50}
        },
                                                                                   {}));
        QVERIFY(processor);
        int outputs     = 0;
        int completions = 0;
        QCOMPARE(processor->accept({QByteArray("pending"), 1},
                                   {[&](Processor::Chunk) { ++outputs; }, [&](QString) { ++completions; }}),
                 Processor::AcceptResult::Accepted);
        bool quiescent = false;
        processor->cancel([&] { quiescent = true; });
        QTRY_VERIFY(quiescent);
        QCOMPARE(observation->destroyed.load(), 1);
        QCOMPARE(outputs, 0);
        QCOMPARE(completions, 0);
        processor.reset();
        QVERIFY(registry.shutdown(1000));
    }

    void pacedNativeTreeCreatesAndDrainsItsTimersOnWorker()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto processor = ThreadedProcessor::create(registry.prepare("native.pacer",
                                                                    {
                                                                        {"byteDelayMs", 2}
        },
                                                                    {}));
        QVERIFY(processor);
        QByteArray output;
        int        completions = 0;
        QCOMPARE(processor->accept({QByteArray("a\0b", 3)},
                                   {[&](Processor::Chunk chunk) { output.append(chunk.bytes); },
                                    [&](QString error) {
                                        QVERIFY2(error.isEmpty(), qPrintable(error));
                                        ++completions;
                                    }}),
                 Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completions, 1);
        QCOMPARE(output, QByteArray("a\0b", 3));
        bool quiescent = false;
        processor->cancel([&] { quiescent = true; });
        QTRY_VERIFY(quiescent);
        processor.reset();
        QVERIFY(registry.shutdown(1000));
    }
};

QTEST_GUILESS_MAIN(ThreadedProcessorTests)
#include "ThreadedProcessorTests.moc"
