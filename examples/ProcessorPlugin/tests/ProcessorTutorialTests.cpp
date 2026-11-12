#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <Termaster/Core/Plugin/PluginHost.h>
#include <Termaster/Core/Plugin/PluginManager.h>
#include <Termaster/Core/Settings.h>

class ProcessorTestHost final : public QObject, public PluginHost
{
public:
    QVariantMap workspaceSettings() const override
    {
        return {};
    }
    void showMessage(const QString&) override
    {
    }
    QObject* context() override
    {
        return this;
    }
    void registerTerminalType(TerminalType) override
    {
    }
    bool unregisterTerminalType(const QString&, QString*) override
    {
        return true;
    }
    void registerAction(Action) override
    {
    }
    void unregisterAction(const QString&) override
    {
    }
    void registerEditor(const QString&, EditorFactory) override
    {
    }
    void unregisterEditor(const QString&) override
    {
    }
    void showCreateTerminal(const QString&) override
    {
    }
    SerialDevice* selectedDevice(const QString&) override
    {
        return nullptr;
    }
    bool canCreateTerminal() const override
    {
        return false;
    }
    bool connectionRequested(const QString&) const override
    {
        return false;
    }
    bool hasSelectedTerminal(const QString&) const override
    {
        return false;
    }
    void connectSelectedTerminal() override
    {
    }
    void disconnectSelectedTerminal() override
    {
    }
    void closeSelectedTerminal() override
    {
    }
    void clearSelectedTerminal() override
    {
    }
    void showSelectedTerminalSettings() override
    {
    }
    void refreshTerminalActions() override
    {
    }
};

class ProcessorTutorialTests final : public QObject
{
    Q_OBJECT

private:
    Q_SLOT void compiledPluginEndToEnd()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString directory = temporary.path() + "/plugin";
        QVERIFY(QDir().mkpath(directory));
        const QString library = directory + "/" + QFileInfo(TUTORIAL_PLUGIN).fileName();
        QVERIFY(QFile::copy(TUTORIAL_PLUGIN, library));
        QVERIFY(QFileInfo::exists(QFileInfo(TUTORIAL_PLUGIN).absolutePath() + "/help/ProcessorTutorial.qch"));
        QPluginLoader metadata(library);
        QCOMPARE(metadata.metaData().value("MetaData").toObject().value("help").toString(),
                 QString("help/ProcessorTutorial.qch"));
        QVERIFY(!metadata.isLoaded());

        Settings settings(temporary.path() + "/plugins.yaml");
        QVERIFY(settings.load());
        QVERIFY(settings.setValue("enabled/ProcessorTutorialPlugin", false));
        QVERIFY(settings.sync());
        ProcessorTestHost host;
        PluginManager     manager(settings, directory);
        manager.setHost(&host);
        QString error;
        QVERIFY2(manager.start(&error), qPrintable(error));
        QCOMPARE(manager.plugins().size(), 1);
        QCOMPARE(manager.plugins().first().status, PluginManager::Status::Disabled);
        QVERIFY(!manager.processors().type("sample.prefix"));
        QVERIFY2(manager.setEnabled("ProcessorTutorialPlugin", true, &error), qPrintable(error));
        QCOMPARE(manager.plugins().first().status, PluginManager::Status::Running);
        {
            const auto type = manager.processors().type("sample.prefix");
            QVERIFY(type.has_value());
            QCOMPARE(type->inputCount, 1);
            QCOMPARE(type->outputCount, 1);
            QCOMPARE(type->editor.value("helpTopic").toString(), QString("example.prefix"));
        }

        auto prepared = manager.processors().prepare("sample.prefix", {}, {}, &error);
        QVERIFY2(prepared != nullptr, qPrintable(error));
        auto processor = prepared->instantiate({}, &error);
        QVERIFY2(processor != nullptr, qPrintable(error));
        QCOMPARE(processor->thread(), QThread::currentThread());
        QCOMPARE(processor->configuration().value("prefix").toString(), QString("[sample] "));
        QList<QByteArray> outputs;
        int               completions = 0;
        const auto        callbacks   = Processor::Callbacks {[&](Processor::Chunk output) {
                                                         QCOMPARE(QThread::currentThread(), processor->thread());
                                                         QCOMPARE(output.port, 0);
                                                         outputs.append(output.bytes);
                                                              },
                                                              [&](QString cause) {
                                                         QVERIFY(cause.isEmpty());
                                                         ++completions;
                                                              }};
        QCOMPARE(processor->accept({QByteArray("A\0B", 3), 0}, callbacks), Processor::AcceptResult::Accepted);
        QCOMPARE(outputs, QList<QByteArray> {QByteArray("[sample] A\0B", 12)});
        QCOMPARE(completions, 1);
        QCOMPARE(processor->accept({{}, 0}, callbacks), Processor::AcceptResult::Accepted);
        QCOMPARE(outputs.last(), QByteArray("[sample] "));
        QCOMPARE(completions, 2);
        QCOMPARE(processor->accept({QByteArray("invalid port"), 1}, callbacks), Processor::AcceptResult::Rejected);
        QCOMPARE(completions, 2);

        QVERIFY(!manager.stop(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(manager.processors().type("sample.prefix").has_value());
        bool quiescent = false;
        processor->cancel([&] { quiescent = true; });
        QVERIFY(quiescent);
        QCOMPARE(processor->accept({QByteArray("stopped"), 0}, callbacks), Processor::AcceptResult::Rejected);
        QCOMPARE(completions, 2);
        processor.reset();
        QCoreApplication::processEvents();
        QVERIFY(!manager.stop(&error)); // A prepared factory still holds the plugin lease.
        prepared.reset();
        QTRY_VERIFY(manager.processors().canRemoveOwner("ProcessorTutorialPlugin"));

        Processor::Limits limits;
        limits.maxBytes  = 4;
        limits.maxChunks = 1;
        auto limited     = manager.processors().create("sample.prefix",
                                                       {
                                                           {"prefix", ">"}
        },
                                                       limits,
                                                       &error);
        QVERIFY2(limited != nullptr, qPrintable(error));
        QCOMPARE(limited->configuration().value("prefix").toString(), QString(">"));
        QList<QByteArray> boundedOutputs;
        int               boundedCompletions = 0;
        const auto        boundedCallbacks   = Processor::Callbacks {[&](Processor::Chunk output) {
                                                                boundedOutputs.append(output.bytes);
                                                                     },
                                                                     [&](QString cause) {
                                                                QVERIFY(cause.isEmpty());
                                                                ++boundedCompletions;
                                                                     }};
        QCOMPARE(limited->accept({QByteArray("A\0B", 3), 0}, boundedCallbacks), Processor::AcceptResult::Accepted);
        QCOMPARE(boundedOutputs, QList<QByteArray> {QByteArray(">A\0B", 4)});
        QCOMPARE(limited->accept({QByteArray("1234"), 0}, boundedCallbacks), Processor::AcceptResult::Rejected);
        QCOMPARE(boundedCompletions, 1);
        limited.reset();

        auto empty = manager.processors().create("sample.prefix",
                                                 {
                                                     {"prefix", ""}
        },
                                                 {},
                                                 &error);
        QVERIFY2(empty != nullptr, qPrintable(error));
        QCOMPARE(empty->accept({{}, 0}, boundedCallbacks), Processor::AcceptResult::Accepted);
        QCOMPARE(boundedOutputs.size(), 2);
        QVERIFY(boundedOutputs.last().isEmpty());
        QCOMPARE(boundedCompletions, 2);
        bool reentrantQuiescent   = false;
        int  reentrantCompletions = 0;
        QCOMPARE(empty->accept({{}, 0},
                               {[&](Processor::Chunk) {
                                    empty->cancel([&] { reentrantQuiescent = true; });
                                    QVERIFY(!reentrantQuiescent);
                                },
                                [&](QString) { ++reentrantCompletions; }}),
                 Processor::AcceptResult::Accepted);
        QVERIFY(reentrantQuiescent);
        QCOMPARE(reentrantCompletions, 0);
        int acknowledgements = 0;
        empty->cancel([&] { ++acknowledgements; });
        empty->cancel([&] { ++acknowledgements; });
        QCOMPARE(acknowledgements, 2);
        empty.reset();

        QTRY_VERIFY(manager.processors().canRemoveOwner("ProcessorTutorialPlugin"));
        QVERIFY2(manager.stop(&error), qPrintable(error));
        QVERIFY(!manager.processors().type("sample.prefix"));
        QCOMPARE(manager.plugins().first().status, PluginManager::Status::Stopped);
    }
};

QTEST_GUILESS_MAIN(ProcessorTutorialTests)
#include "ProcessorTutorialTests.moc"
