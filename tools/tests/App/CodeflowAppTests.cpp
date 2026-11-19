#include "Gui/Processing/CodeflowEditorDialog.h"
#include "Gui/Terminal/TerminalController.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <Termaster/App/App.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Gui/Help/HelpBrowser.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/Plugin/PluginManager.h>
#include <Termaster/Core/Processing/Codeflow.h>
#include <Termaster/Core/Workspace.h>

/** Loopback-only acceptance of the application-owned Codeflow lifecycle. */
class CodeflowAppTests final : public QObject
{
    Q_OBJECT
public:
    explicit CodeflowAppTests(App& app) : _app(app)
    {
    }

private:
    App& _app;

    Q_SLOT void workspaceActionPublishesAndUpdatesManagedSource()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString    error;
        const Path workspacePath(temporary.filePath("Publication.termaster"));
        QVERIFY2(_app.createWorkspace(workspacePath, "Codeflow publication", &error), qPrintable(error));
        const auto cleanup = qScopeGuard([&] { _app.closeWorkspace(); });
        auto*      command = _app.commands().find("Workspace.Codeflow");
        QVERIFY(command);
        QVERIFY(!command->actions().isEmpty());
        auto* open = command->actions().first();
        QVERIFY(open->isEnabled());
        open->trigger();
        QPointer<CodeflowEditorDialog> editor = _app.mainWindow().findChild<CodeflowEditorDialog*>();
        QVERIFY(editor);
        QTRY_VERIFY(editor->isVisible());
        QAction* helpAction = nullptr;
        for (auto* action : editor->findChildren<QAction*>())
        {
            if (action->text() == "Help")
            {
                helpAction = action;
            }
        }
        QVERIFY(helpAction);
        helpAction->trigger();
        auto* help = _app.mainWindow().findChild<HelpBrowser*>("helpBrowser");
        QVERIFY(help);
        QTRY_COMPARE(help->currentTopic(), QString("processing.codeflow"));
        help->window()->hide();
        editor->activateWindow();
        auto* source = editor->findChild<QPlainTextEdit*>("codeflowSource");
        auto* name   = editor->findChild<QLineEdit*>("codeflowName");
        auto* file   = editor->findChild<QLineEdit*>("codeflowFile");
        auto* status = editor->findChild<QLabel*>("codeflowStatus");
        QVERIFY(source && name && file && status);
        QAction* apply = nullptr;
        for (auto* action : editor->findChildren<QAction*>())
        {
            if (action->shortcut() == QKeySequence("Ctrl+Shift+Return"))
            {
                apply = action;
                break;
            }
        }
        QVERIFY(apply);
        const auto initialCode = Codeflow::templateSource(2);
        source->setPlainText(initialCode);
        name->setText("Published from workspace action");
        const auto managedPath = QDir::fromNativeSeparators(file->text());
        QVERIFY(managedPath.startsWith(QDir::fromNativeSeparators(workspacePath.toString()) + ".d/scripts/"));
        QVERIFY(!QFileInfo::exists(managedPath));
        apply->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("Published catalog revision"), 10000);
        const auto definitions = _app.workspace()->processorDefinitions().values();
        QCOMPARE(definitions.size(), 1);
        const auto id                = definitions.firstKey();
        const auto initialDefinition = definitions.value(id).toMap();
        QCOMPARE(initialDefinition.value("name").toString(), QString("Published from workspace action"));
        QCOMPARE(editor->definition(), initialDefinition);
        const auto initialSource = initialDefinition.value("source").toMap();
        const auto storedPath    = initialSource.value("file").toString();
        QVERIFY(QDir::isRelativePath(storedPath));
        QCOMPARE(QDir(temporary.path()).absoluteFilePath(storedPath), managedPath);
        QFile saved(managedPath);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), initialCode.toUtf8());
        saved.close();
        QCOMPARE(initialSource.value("code").toString(), initialCode);

        // A second Apply uses the baseline accepted by the real App transaction callback.
        const auto updatedCode = initialCode + "\n# Second published revision\n";
        source->setPlainText(updatedCode);
        name->setText("Updated from workspace action");
        apply->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("Published catalog revision")
                                     || status->text().startsWith("Partial save"),
                                 10000);
        QVERIFY2(status->text().startsWith("Published catalog revision"),
                 qPrintable(editor->findChild<QTextEdit*>("codeflowDiagnostics")->toPlainText()));
        QCOMPARE(_app.workspace()->processorDefinitions().value(id).toMap().value("name").toString(),
                 QString("Updated from workspace action"));
        const auto updatedDefinition = _app.workspace()->processorDefinitions().value(id).toMap();
        QCOMPARE(_app.workspace()->processorDefinitions().values().size(), 1);
        QCOMPARE(updatedDefinition.value("id").toString(), id);
        const auto updatedSource = updatedDefinition.value("source").toMap();
        QCOMPARE(updatedSource.value("file").toString(), storedPath);
        QCOMPARE(updatedSource.value("code").toString(), updatedCode);
        QVERIFY(updatedSource.value("digest") != initialSource.value("digest"));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), updatedCode.toUtf8());
        saved.close();

        QVERIFY2(_app.closeWorkspace(&error), qPrintable(error));
        QTRY_VERIFY(editor.isNull());
        QVERIFY2(_app.openWorkspace(workspacePath, &error), qPrintable(error));
        QCOMPARE(_app.workspace()->processorDefinitions().value(id).toMap(), updatedDefinition);
    }

    Q_SLOT void stopAndFaultPreserveConnection_data()
    {
        QTest::addColumn<bool>("fault");
        QTest::newRow("explicit-stop") << false;
        QTest::newRow("python-exception") << true;
    }

    Q_SLOT void stopAndFaultPreserveConnection()
    {
        QFETCH(bool, fault);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString    error;
        const auto plugin = QDir(_app.plugins().directory())
                                .filePath(QFileInfo(TERMASTER_SOCKET_PLUGIN_FILE).fileName());
        QVERIFY(QDir().mkpath(_app.plugins().directory()));
        QVERIFY(QFile::copy(TERMASTER_SOCKET_PLUGIN_FILE, plugin));
        const auto cleanup = qScopeGuard([&] {
            _app.closeWorkspace();
            _app.plugins().setEnabled("SocketTerminal", false);
            QFile::remove(plugin);
            _app.plugins().rescan();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QVERIFY2(_app.plugins().rescan(&error), qPrintable(error));
        QVERIFY2(_app.plugins().setEnabled("SocketTerminal", true, &error), qPrintable(error));
        QVERIFY2(_app.createWorkspace(Path(temporary.filePath("Codeflow.termaster")), "Codeflow acceptance", &error),
                 qPrintable(error));
        QVERIFY2(_app.createSession("Codeflow acceptance", &error), qPrintable(error));

        auto code = Codeflow::templateSource(2);
        code.replace(
            "return self.prefix.encode(\"utf-8\") + data",
            "if data == b'FAULT':\n            raise RuntimeError('Synthetic Codeflow failure')\n        return self.prefix.encode('utf-8') + data.hex().encode('ascii')");
        code.replace("return data\n", "return self.prefix.encode('utf-8') + data\n");
        const QVariantMap source {
            {  "code",                      code},
            {"python", Codeflow::defaultPython()}
        };
        std::pair<QVariantMap, QString> checked;
        std::unique_ptr<QThread>        inspection(
            QThread::create([&] { checked.first = Codeflow::inspect(source, &checked.second); }));
        inspection->start();
        QVERIFY(inspection->wait(10000));
        QVERIFY2(checked.second.isEmpty(), qPrintable(checked.second));
        QVERIFY(!checked.first.isEmpty());
        const QString definitionId = "acceptance.python";
        QVERIFY2(_app.workspace()->setProcessorDefinition(definitionId,
                                                          Codeflow::definition(definitionId,
                                                                               "Acceptance Python",
                                                                               source,
                                                                               checked.first),
                                                          &error),
                 qPrintable(error));

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QVariantMap processing {
            {    "mode",                                                "lineflow"                        },
            {"lineflow",
             QVariantMap {{"type", definitionId}, {"configuration", QVariantMap {{"prefix", "Checked:"}}}}}
        };
        QVERIFY2(_app.createTerminal("socket",
                                     {
                                         { "transport",
                                          QVariantMap {{"protocol", "TCP"},
                                          {"host", "127.0.0.1"},
                                          {"port", int(server.serverPort())}}             },
                                         {"processing",                         processing}
        },
                                     "Python loopback",
                                     &error),
                 qPrintable(error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        auto& host        = _app.pluginHost();
        auto& controller  = static_cast<TerminalController&>(host);
        auto* device      = host.selectedDevice("socket");
        auto* display     = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        auto* diagnostics = _app.mainWindow().findChild<QLabel*>("processingDiagnostics");
        QVERIFY(device);
        QVERIFY(display);
        if (!diagnostics)
        {
            for (auto* widget : QApplication::allWidgets())
            {
                if (widget->objectName() == "processingDiagnostics")
                {
                    diagnostics = qobject_cast<QLabel*>(widget);
                    break;
                }
            }
        }
        QVERIFY(diagnostics);
        diagnostics->parentWidget()->show();
        host.connectSelectedTerminal();
        QTRY_COMPARE(device->state(), SerialDevice::State::Open);
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
        QByteArray                  outbound;
        connect(peer.get(), &QIODevice::readyRead, this, [&] { outbound += peer->readAll(); });

        // Binary input reaches Python unchanged; its hexadecimal result is observable on screen.
        const QByteArray binary = QByteArray::fromHex("4100ff42");
        QCOMPARE(peer->write(binary), binary.size());
        QTRY_VERIFY(display->text().contains("Checked:4100ff42"));
        display->outputBytes(binary, TerminalDisplay::OutputKind::Reply, {});
        QTRY_COMPARE(outbound, QByteArray("Checked:") + binary);
        outbound.clear();
        if (fault)
        {
            QCOMPARE(peer->write("FAULT"), qint64(5));
            QTRY_VERIFY(diagnostics->toolTip().contains("Synthetic Codeflow failure"));
        }
        else
        {
            QVERIFY2(controller.controlProcessing(definitionId, false, &error), qPrintable(error));
            QTRY_VERIFY(diagnostics->toolTip().contains("Processing stopped"));
        }
        QCOMPARE(device->state(), SerialDevice::State::Open);
        QVERIFY(host.connectionRequested("socket"));
        QCOMPARE(peer->state(), QAbstractSocket::ConnectedState);
        QVERIFY(!server.hasPendingConnections());

        // Restart replaces processing while retaining the same transport and saved selection.
        QVERIFY2(controller.controlProcessing(definitionId, true, &error), qPrintable(error));
        display->clear();
        QCOMPARE(peer->write(binary), binary.size());
        QTRY_VERIFY(display->text().contains("Checked:4100ff42"));
        display->outputBytes(binary, TerminalDisplay::OutputKind::Reply, {});
        QTRY_COMPARE(outbound, QByteArray("Checked:") + binary);
        QCOMPARE(device->state(), SerialDevice::State::Open);
        QCOMPARE(peer->state(), QAbstractSocket::ConnectedState);
        QVERIFY(!server.hasPendingConnections());
        QCOMPARE(controller.selectedSettings().value("processing").toMap(), processing);
        QVERIFY2(_app.saveSession(&error), qPrintable(error));
        host.disconnectSelectedTerminal();
        QTRY_COMPARE(device->state(), SerialDevice::State::Closed);
    }
};

int main(int argc, char* argv[])
{
    QTemporaryDir temporary;
    if (!temporary.isValid())
    {
        return 1;
    }
    App::Options options;
    options.dataDirectory    = temporary.filePath("data");
    options.pluginsDirectory = temporary.filePath("plugins");
    App              app(argc, argv, options);
    CodeflowAppTests tests(app);
    int              result = 1;
    QTimer::singleShot(0, &app, [&] {
        result = QTest::qExec(&tests, argc, argv);
        app.quit();
    });
    const int loopResult = app.run();
    return loopResult == 0 ? result : loopResult;
}

#include "CodeflowAppTests.moc"
