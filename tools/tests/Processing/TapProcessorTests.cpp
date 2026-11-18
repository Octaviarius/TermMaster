#include "Nodes/Tap.h"

#include <QFile>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRefactoring.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <algorithm>

namespace
{
class Manual final : public Processor
{
public:
    QList<QByteArray>     received;
    Callbacks             pending;
    std::function<void()> cancelled;
    bool                  busy  = false;
    bool                  block = false;

    QVariantMap configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        if (busy || block)
        {
            return AcceptResult::WouldBlock;
        }
        busy = true;
        received.append(input.bytes);
        pending = std::move(callbacks);
        return AcceptResult::Accepted;
    }
    void complete(const QString& error = {})
    {
        auto callbacks = std::move(pending);
        pending        = {};
        busy           = false;
        if (callbacks.output)
        {
            callbacks.output({"transformed"});
            callbacks.output({{}});
        }
        if (callbacks.completed)
        {
            callbacks.completed(error);
        }
    }
    void cancel(std::function<void()> ready) override
    {
        cancelled = std::move(ready);
    }
    void acknowledge()
    {
        auto ready = std::move(cancelled);
        if (ready)
        {
            ready();
        }
    }
};
QVariantMap saved(const QString& id, const QString& kind, const QVariantList& nodes)
{
    return {
        {    "id",                                              id},
        {  "name",                                              id},
        {  "kind",                                            kind},
        {"source", QVariantMap {{"channels", 1}, {"nodes", nodes}}}
    };
}
QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray {};
}
} // namespace

class TapProcessorTests final : public QObject
{
    Q_OBJECT
private slots:
    void mainPacketsCompleteBeforeIndependentBranchAndDiscardItsOutput()
    {
        auto branch = std::make_shared<Manual>();
        auto tap    = Tap::compose(branch, {}, {});
        QVERIFY(tap);
        const QByteArray  packet("A\0B", 3);
        QList<QByteArray> output;
        int               completions = 0;
        QString           error;
        const auto        callbacks = Processor::Callbacks {[&](auto chunk) { output.append(chunk.bytes); },
                                                            [&](QString failure) {
                                                         error = failure;
                                                         ++completions;
                                                            }};
        QCOMPARE(tap->accept({packet}, callbacks), Processor::AcceptResult::Accepted);
        QCOMPARE(tap->accept({{}}, callbacks), Processor::AcceptResult::Accepted);
        QCOMPARE(output, QList<QByteArray>({packet, {}}));
        QCOMPARE(completions, 2);
        QVERIFY(branch->received.isEmpty());
        QTRY_COMPARE(branch->received.size(), 1);
        QCOMPARE(branch->received[0], packet);
        branch->complete();
        QTRY_COMPARE(branch->received.size(), 2);
        QVERIFY(branch->received[1].isEmpty());
        branch->complete();
        QCOMPARE(output.size(), 2);
        QCOMPARE(completions, 2);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        bool ready = false;
        tap->cancel([&] { ready = true; });
        QVERIFY(!ready);
        branch->acknowledge();
        QVERIFY(ready);
    }

    void wouldBlockRetriesWholePacketAndCancellationWaitsForBranch()
    {
        auto branch      = std::make_shared<Manual>();
        branch->block    = true;
        auto tap         = Tap::compose(branch, {}, {});
        int  completions = 0;
        tap->accept({QByteArray("\0\xff", 2)}, {{}, [&](QString error) {
                                                    QVERIFY(error.isEmpty());
                                                    ++completions;
                                                }});
        QCOMPARE(completions, 1);
        QTest::qWait(5);
        QVERIFY(branch->received.isEmpty());
        branch->block = false;
        QTRY_COMPARE(branch->received.size(), 1);
        QCOMPARE(branch->received[0], QByteArray("\0\xff", 2));
        int acknowledgements = 0;
        tap->cancel([&] { ++acknowledgements; });
        tap->cancel([&] { ++acknowledgements; });
        QCOMPARE(acknowledgements, 0);
        branch->complete();
        QCOMPARE(completions, 1);
        branch->acknowledge();
        QCOMPARE(acknowledgements, 2);
        QCOMPARE(tap->accept({"late"}, {}), Processor::AcceptResult::Rejected);
    }

    void queueIncludesActivePacketAndFailsWithoutDroppingEarlierMainOutput()
    {
        auto              branch = std::make_shared<Manual>();
        Processor::Limits limits;
        limits.maxChunks   = 2;
        limits.maxBytes    = 4;
        auto       tap     = Tap::compose(branch, {}, limits);
        int        outputs = 0;
        QString    error;
        const auto callbacks = Processor::Callbacks {[&](auto) { ++outputs; },
                                                     [&](QString failure) { error = failure; }};
        tap->accept({"ab"}, callbacks);
        QTRY_COMPARE(branch->received.size(), 1);
        tap->accept({"cd"}, callbacks);
        QCOMPARE(outputs, 2);
        tap->accept({{}}, callbacks);
        QCOMPARE(outputs, 2);
        QVERIFY(error.contains("queue limit"));
        bool ready = false;
        tap->cancel([&] { ready = true; });
        QVERIFY(!ready);
        branch->acknowledge();
        QVERIFY(ready);
    }

    void branchFailureIsExplicitAfterMainCompletionAndCancellationJoins()
    {
        auto    branch = std::make_shared<Manual>();
        auto    tap    = Tap::compose(branch, {}, {});
        QString failure;
        tap->setErrorHandler([&](QString error) { failure = error; });
        int completed = 0;
        tap->accept({"input"}, {{}, [&](QString error) {
                                    QVERIFY(error.isEmpty());
                                    ++completed;
                                }});
        QCOMPARE(completed, 1);
        QTRY_COMPARE(branch->received.size(), 1);
        branch->complete("fixture failure");
        QVERIFY(failure.isEmpty());
        QCOMPARE(tap->accept({"more"}, {}), Processor::AcceptResult::Rejected);
        branch->acknowledge();
        QVERIFY(failure.contains("fixture failure"));
        QCOMPARE(completed, 1);
        tap->cancel({});
    }

    void savedTapUsesPrivateBranchOverridesAndNativeTemplateMetadata()
    {
        QTemporaryDir     directory;
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        const auto metadata = registry.type("native.tap");
        QVERIFY(metadata);
        QCOMPARE(metadata->kind, QString("tap"));
        QCOMPARE(metadata->inputCount, 1);
        QCOMPARE(metadata->outputCount, 1);
        QVERIFY(metadata->editor.value("compositionTemplate").toBool());
        auto tap    = saved("local.tap",
                            "tap",
                            {
                                QVariantMap {{"name", "encode"},
                                             {"type", "native.hex-encoder"},
                                             {"configuration", QVariantMap {{"separator", ""}}}}
        });
        auto source = tap.value("source").toMap();
        source.insert("final",
                      QVariantMap {
                          {"enabled",                           true},
                          {   "file", directory.filePath("copy.bin")},
                          { "format",                       "binary"}
        });
        tap.insert("source", source);
        const auto        root = saved("root",
                                       "lineflow",
                                       {
                                           QVariantMap {{"name", "observer"}, {"reference", "local.tap"}}
        });
        const QVariantMap definitions {
            {"local.tap",  tap},
            {     "root", root}
        };
        QString error;
        auto    scoped = registry.scoped(definitions, &error);
        QVERIFY2(scoped, qPrintable(error));
        const auto properties = scoped->properties("root", {}, {}, &error);
        QVERIFY2(properties, qPrintable(error));
        QVERIFY(std::any_of(properties->cbegin(), properties->cend(), [](const auto& node) {
            return node.path == "observer/encode";
        }));
        auto plan = scoped->prepare("root", {}, {}, &error);
        QVERIFY2(plan, qPrintable(error));
        const auto privateFile = directory.filePath("private-copy.bin");
        auto       runtime     = plan->instantiate(
            {
                {"observer/@final", QVariantMap {{"file", privateFile}}}
        },
            &error);
        QVERIFY2(runtime, qPrintable(error));
        scoped.reset();
        QVERIFY(!registry.canRemoveOwner("native"));
        QByteArray output;
        bool       completed = false;
        runtime->accept({QByteArray(1, char(0xab))},
                        {[&](auto chunk) { output.append(chunk.bytes); },
                         [&](QString failure) {
                             error     = failure;
                             completed = true;
                         }});
        QTRY_VERIFY(completed);
        QCOMPARE(output, QByteArray(1, char(0xab)));
        QTRY_COMPARE(read(privateFile), QByteArray("AB"));
        QVERIFY(!QFile::exists(directory.filePath("copy.bin")));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        bool cancelled = false;
        runtime->cancel([&] { cancelled = true; });
        QTRY_VERIFY(cancelled);
        runtime.reset();
        plan.reset();
        QTRY_VERIFY(registry.canRemoveOwner("native"));
        auto invalid = tap;
        source.insert("channels", 2);
        invalid.insert("source", source);
        QVERIFY(!ProcessorRegistry::parseDefinitions(
            {
                {"local.tap", invalid}
        },
            &error));
        QVERIFY(error.contains("one channel"));
    }

    void tapOccurrenceRefactoringIncludesCopyChildrenAndEndpoints()
    {
        const auto        before = saved("tap",
                                         "tap",
                                         {
                                             QVariantMap {{"name", "old"}, {"type", "native.gate"}}
        });
        const auto        after  = saved("tap",
                                         "tap",
                                         {
                                             QVariantMap {{"name", "new"}, {"type", "native.gate"}}
        });
        const auto        root   = saved("root",
                                         "lineflow",
                                         {
                                             QVariantMap {{"name", "observer"}, {"reference", "tap"}}
        });
        const QVariantMap overrides {
            {   "observer/old", QVariantMap {{"unknown", true}}},
            {"observer/@final",    QVariantMap {{"backups", 2}}}
        };
        const QVariantMap objects {
            {"terminal",
             QVariantMap {{"settings",
                           QVariantMap {{"processing",
                                         QVariantMap {{"lineflow",
                                                       QVariantMap {{"type", "root"}, {"overrides", overrides}}}}}}}}}
        };
        QString    error;
        const auto transformed = ProcessorRefactoring::transform(
            {
                {"root",   root},
                { "tap", before}
        },
            {{"root", root}, {"tap", after}},
            {{"tap", "old", "new"}},
            objects,
            &error);
        QVERIFY2(transformed, qPrintable(error));
        const auto result = transformed->sessionObjects.value("terminal")
                                .toMap()
                                .value("settings")
                                .toMap()
                                .value("processing")
                                .toMap()
                                .value("lineflow")
                                .toMap()
                                .value("overrides")
                                .toMap();
        QCOMPARE(result.value("observer/new"), overrides.value("observer/old"));
        QCOMPARE(result.value("observer/@final"), overrides.value("observer/@final"));
    }
};

QTEST_GUILESS_MAIN(TapProcessorTests)
#include "TapProcessorTests.moc"
