#include "gui/settings/SettingsDialog.h"

#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <QUuid>
#include <Termaster/App/App.h>
#include <Termaster/Core/ApplicationPaths.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/RandomNames.h>
#include <Termaster/Core/Session.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <Termaster/Core/Workspace.h>
#include <Termaster/Core/gui/action/MenuActionContainer.h>
#include <Termaster/Core/gui/action/MenubarActionContainer.h>
#include <Termaster/Core/gui/action/ToolbarActionContainer.h>
#include <Termaster/Core/gui/settings/SettingsFormModel.h>
#include <Termaster/Core/gui/settings/SettingsFormWidget.h>
#include <Termaster/Core/gui/settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/gui/terminal/TerminalDisplay.h>
#include <Termaster/Core/plugin/PluginHost.h>
#include <Termaster/Core/plugin/PluginManager.h>
#include <stdexcept>

class AppTests final : public QObject
{
    Q_OBJECT

public:
    explicit AppTests(App& app) : _app(app)
    {
    }

private:
    App& _app;

    Q_SLOT void shellAndCommands()
    {
        QCOMPARE(QCoreApplication::applicationName(), QString("Termaster"));
        QCOMPARE(_app.mainWindow().windowTitle(), QString("Termaster"));
        QVERIFY(_app.mainWindow().isVisible());
        QVERIFY(!QApplication::windowIcon().isNull());
        QVERIFY(!_app.mainWindow().windowIcon().isNull());
        QVERIFY(!QApplication::windowIcon().pixmap(32, 32).isNull());
        for (const auto& menu : {"/file", "/edit", "/view", "/tools", "/help"})
        {
            QVERIFY(_app.mainMenu().findMenu(menu));
        }
        QVERIFY(!_app.workspace());
        const QString capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("Welcome.png")));
        }
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(_app.mainWindow().findChild<QPushButton*>("createWorkspace")->isVisible());
        QVERIFY(_app.mainWindow().findChild<QPushButton*>("openWorkspace")->isVisible());
        QVERIFY(!_app.commands().find("Workspace.Save")->actions().first()->isEnabled());
        QVERIFY(!_app.commands().find("Workspace.Close")->actions().first()->isEnabled());
        auto* tools = _app.mainMenu().findMenu("/tools");
        QVERIFY(tools);
        auto* plugins = _app.commands().find("Application.Plugins");
        auto* quit    = _app.commands().find("Application.Exit");
        QVERIFY(plugins);
        QVERIFY(quit);
        QCOMPARE(plugins->actions().size(), 1);
        QCOMPARE(quit->shortcut(), QKeySequence(QKeySequence::Quit));
        QVERIFY(tools->actions("management").contains(plugins->actions().first()));
        QVERIFY(_app.toolbar().actions("application").contains(plugins->actions().first()));
        QCOMPARE(_app.toolbar().groups(), QStringList({"application", "workspace", "session", "terminal"}));
        QCOMPARE(_app.toolbar().toolbar()->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QVERIFY(!_app.toolbar().toolbar()->actions().isEmpty());
        for (auto* action : _app.toolbar().toolbar()->actions())
        {
            if (!action->isSeparator())
            {
                QVERIFY(!action->icon().isNull());
                QVERIFY(!action->toolTip().isEmpty());
            }
        }
        QVERIFY(_app.toolbar().toolbar()->actions().size() >= 5);
        QVERIFY(!QFileInfo::exists(_app.paths().configFile("plugins").toString()));
    }

    Q_SLOT void workspaceLifecycleAndRestart()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const Path file(temporary.filePath("example.termaster"));
        QString    error;
        QSignalSpy changed(&_app, &App::workspaceChanged);
        QVERIFY(_app.settings().setValue("settings/appearance/fontSize", 12));
        QVERIFY(_app.settings().setValue("settings/appearance/theme", "dark"));
        QVERIFY2(_app.createWorkspace(file, "Example", &error), qPrintable(error));
        QCOMPARE(changed.count(), 1);
        QVERIFY(_app.workspace());
        QCOMPARE(_app.workspace()->name(), QString("Example"));
        const QString capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("Workspace.png")));
        }
        QVERIFY(_app.mainWindow().windowTitle().contains("Example"));
        QVERIFY(_app.commands().find("Workspace.Save")->actions().first()->isEnabled());
        QCOMPARE(_app.configuration().value("appearance/fontSize").toInt(), 12);
        QVERIFY(_app.configuration().setValue("appearance/fontSize", 18));
        auto* dirtyWorkspace = _app.workspace();
        QVERIFY(_app.restoreLastWorkspace(&error));
        QCOMPARE(_app.workspace(), dirtyWorkspace);
        QVERIFY(_app.workspace()->isDirty());
        QCOMPARE(_app.configuration().value("appearance/fontSize").toInt(), 18);
        QCOMPARE(_app.configuration().value("appearance/theme").toString(), QString("dark"));
        QCOMPARE(_app.settings().value("settings/appearance/fontSize").toInt(), 12);
        QVERIFY2(_app.saveWorkspace(&error), qPrintable(error));
        QVERIFY(_app.settings().sync(&error));

        QProcess child;
        auto     environment = QProcessEnvironment::systemEnvironment();
        environment.insert("TERMASTER_APP_RESTART_DATA", _app.paths().dataDirectory().toString());
        environment.insert("TERMASTER_APP_RESTART_WORKSPACE", _app.workspace()->filePath().toString());
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), QStringList());
        QVERIFY(child.waitForStarted(5000));
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
        Settings savedApplication(_app.paths().configFile("application"),
                                  {"termaster.application", "Termaster application settings"});
        QVERIFY(savedApplication.load(&error));
        QCOMPARE(savedApplication.value("settings/child/autosave").toInt(), 41);
        Workspace savedWorkspace(file, savedApplication.node("settings"));
        QVERIFY(savedWorkspace.load(&error));
        QCOMPARE(savedWorkspace.settings().value("child/autosave").toInt(), 42);

        auto*      active     = _app.workspace();
        const auto remembered = _app.settings().value("state/lastWorkspace");
        QVERIFY(!_app.openWorkspace(Path(temporary.filePath("missing.termaster")), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(_app.workspace(), active);
        QCOMPARE(_app.settings().value("state/lastWorkspace"), remembered);
        QFile malformed(temporary.filePath("malformed.termaster"));
        QVERIFY(malformed.open(QIODevice::WriteOnly));
        QVERIFY(malformed.write("version: [invalid") > 0);
        malformed.close();
        QVERIFY(!_app.openWorkspace(Path(malformed.fileName()), &error));
        QCOMPARE(_app.workspace(), active);
        QVERIFY(_app.configuration().setValue("appearance/fontSize", 19));
        const QString backup = file.toString() + ".backup";
        QVERIFY(QFile::rename(file.toString(), backup));
        QVERIFY(QDir().mkdir(file.toString()));
        QVERIFY(!_app.closeWorkspace(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(_app.workspace(), active);
        QVERIFY(_app.workspace()->isDirty());
        QVERIFY(QDir().rmdir(file.toString()));
        QVERIFY(QFile::rename(backup, file.toString()));
        QVERIFY(_app.configuration().setValue("appearance/fontSize", 18));
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(!_app.workspace());
        QCOMPARE(_app.configuration().value("appearance/fontSize").toInt(), 12);
        QVERIFY(!_app.commands().find("Workspace.Save")->actions().first()->isEnabled());
        QVERIFY2(_app.openWorkspace(file, &error), qPrintable(error));
        QCOMPARE(_app.configuration().value("appearance/fontSize").toInt(), 18);
        QVERIFY(_app.configuration().remove("appearance/fontSize"));
        QCOMPARE(_app.configuration().value("appearance/fontSize").toInt(), 12);
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void missingLastWorkspaceRecovery()
    {
        QTemporaryDir          temporary;
        const ApplicationPaths paths(Path(temporary.filePath("data")));
        const QString          missing = temporary.filePath("missing.termaster");
        Settings settings(paths.configFile("application"), {"termaster.application", "Termaster application settings"});
        QVERIFY(settings.setValue("state/lastWorkspace", missing));
        QVERIFY(settings.sync());
        QFile file(paths.configFile("application").toString());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto before = file.readAll();
        file.close();
        QProcess child;
        auto     environment = QProcessEnvironment::systemEnvironment();
        environment.insert("TERMASTER_APP_RESTART_DATA", paths.dataDirectory().toString());
        environment.insert("TERMASTER_APP_RECOVERY", "1");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), QStringList());
        QVERIFY(child.waitForStarted(5000));
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), before);
        QVERIFY(!QFileInfo::exists(missing));
    }

    Q_SLOT void editingAndViewCommands()
    {
        auto* clipboard      = QApplication::clipboard();
        auto  savedClipboard = std::make_unique<QMimeData>();
        if (const auto* source = clipboard->mimeData())
        {
            for (const auto& format : source->formats())
            {
                savedClipboard->setData(format, source->data(format));
            }
        }
        const auto restoreClipboard = qScopeGuard([&] { clipboard->setMimeData(savedClipboard.release()); });
        clipboard->setText("replacement");
        const auto    action = [this](const char* id) { return _app.commands().find(id)->actions().first(); };
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("editing.termaster")), "Editing", &error));
        auto* file = _app.mainWindow().findChild<QLineEdit*>("workspaceFile");
        QVERIFY(file);
        _app.mainWindow().activateWindow();
        file->setFocus();
        file->selectAll();
        QTRY_VERIFY(action("Edit.Copy")->isEnabled());
        QVERIFY(!action("Edit.Cut")->isEnabled());
        QVERIFY(!action("Edit.Paste")->isEnabled());
        action("Edit.Copy")->trigger();
        QCOMPARE(clipboard->text(), file->text());
        file->deselect();
        QTRY_VERIFY(!action("Edit.Copy")->isEnabled());

        QPlainTextEdit editor(&_app.mainWindow());
        editor.setGeometry(50, 100, 400, 100);
        editor.show();
        editor.setFocus();
        editor.setPlainText("alpha");
        editor.selectAll();
        QTRY_VERIFY(action("Edit.Cut")->isEnabled());
        action("Edit.Cut")->trigger();
        QCOMPARE(editor.toPlainText(), QString());
        QCOMPARE(clipboard->text(), QString("alpha"));
        action("Edit.Paste")->trigger();
        QCOMPARE(editor.toPlainText(), QString("alpha"));
        QTRY_VERIFY(action("Edit.Undo")->isEnabled());
        action("Edit.Undo")->trigger();
        QCOMPARE(editor.toPlainText(), QString());
        QTRY_VERIFY(action("Edit.Redo")->isEnabled());
        action("Edit.Redo")->trigger();
        QCOMPARE(editor.toPlainText(), QString("alpha"));
        editor.selectAll();
        editor.setReadOnly(true);
        QTRY_VERIFY(!action("Edit.Cut")->isEnabled());
        QVERIFY(action("Edit.Copy")->isEnabled());
        QVERIFY(!action("Edit.Paste")->isEnabled());
        editor.setReadOnly(false);
        QTRY_VERIFY(action("Edit.Cut")->isEnabled());

        QLineEdit line(&_app.mainWindow());
        line.setGeometry(50, 220, 400, 30);
        line.show();
        line.setText("beta");
        line.setFocus();
        line.selectAll();
        QTRY_VERIFY(action("Edit.Cut")->isEnabled());
        action("Edit.Cut")->trigger();
        QCOMPARE(line.text(), QString());
        action("Edit.Paste")->trigger();
        QCOMPARE(line.text(), QString("beta"));
        line.setReadOnly(true);
        line.selectAll();
        QTRY_VERIFY(!action("Edit.Cut")->isEnabled());
        QVERIFY(action("Edit.Copy")->isEnabled());
        QVERIFY(!action("Edit.Paste")->isEnabled());
        _app.mainWindow().findChild<QPushButton*>("openWorkspace")->setFocus();
        QTRY_VERIFY(!action("Edit.Copy")->isEnabled());
        QVERIFY(!action("Edit.SelectAll")->isEnabled());

        QVERIFY(_app.settings().setValue("settings/ui/toolbarVisible", true));
        QVERIFY(_app.settings().setValue("settings/ui/statusBarVisible", true));
        action("View.Toolbar")->trigger();
        QVERIFY(_app.toolbar().toolbar()->isHidden());
        QCOMPARE(_app.workspace()->settings().value("ui/toolbarVisible").toBool(), false);
        QCOMPARE(_app.settings().value("settings/ui/toolbarVisible").toBool(), true);
        action("View.StatusBar")->trigger();
        QVERIFY(_app.mainWindow().statusBar()->isHidden());
        QCOMPARE(_app.workspace()->settings().value("ui/statusBarVisible").toBool(), false);
        QCOMPARE(_app.settings().value("settings/ui/statusBarVisible").toBool(), true);
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(!_app.toolbar().toolbar()->isHidden());
        QVERIFY(!_app.mainWindow().statusBar()->isHidden());
        QVERIFY(action("View.Toolbar")->isChecked());
        QVERIFY(action("View.StatusBar")->isChecked());
    }

    Q_SLOT void recentWorkspaceHistory()
    {
        QTemporaryDir temporary;
        QString       error;
        QStringList   files;
        for (int index = 0; index < 12; ++index)
        {
            const Path file(temporary.filePath(QString::number(index) + ".termaster"));
            QVERIFY2(_app.createWorkspace(file, QString::number(index), &error), qPrintable(error));
            files.prepend(_app.workspace()->filePath().toString());
        }
        files = files.mid(0, 10);
        QCOMPARE(_app.recentWorkspaces(), files);
        QVERIFY(_app.openWorkspace(Path(files.at(3)), &error));
        files.prepend(files.takeAt(3));
        QCOMPARE(_app.recentWorkspaces(), files);
        QVERIFY(_app.openWorkspace(Path(files.first()), &error));
        QCOMPARE(_app.recentWorkspaces(), files);
        QVERIFY(!_app.openWorkspace(Path(temporary.filePath("missing.termaster")), &error));
        QCOMPARE(_app.recentWorkspaces(), files);
        QVERIFY(_app.mainMenu().findMenu("/file/recentWorkspaces"));
        QVERIFY(_app.mainMenu().findMenu("/file/recentSessions"));
        auto* recentMenu        = static_cast<MenuActionContainer*>(_app.mainMenu().findMenu("/file/recentWorkspaces"))
                                      ->menu();
        int   visibleWorkspaces = 0;
        for (auto* action : recentMenu->actions())
        {
            if (action->isVisible())
            {
                ++visibleWorkspaces;
            }
        }
        QCOMPARE(visibleWorkspaces, 10);
        QCOMPARE(recentMenu->actions().first()->data().toString(), files.first());
        QVERIFY(_app.createSession("Recorded", &error));
        const auto recordedId = _app.session()->id();
        QVERIFY(_app.closeSession(&error));
        auto* sessionMenu = static_cast<MenuActionContainer*>(_app.mainMenu().findMenu("/file/recentSessions"))->menu();
        QVERIFY(QMetaObject::invokeMethod(sessionMenu, "aboutToShow", Qt::DirectConnection));
        int visibleSessions = 0;
        for (auto* action : sessionMenu->actions())
        {
            if (action->isVisible())
            {
                ++visibleSessions;
                QVERIFY(action->isEnabled());
                QVERIFY(action->text().contains("Recorded"));
                QCOMPARE(action->data().toString(), recordedId);
                action->trigger();
                QVERIFY(_app.session());
                QCOMPARE(_app.session()->name(), QString("Recorded"));
            }
        }
        QCOMPARE(visibleSessions, 1);
        const QString capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            recentMenu->popup(_app.mainWindow().mapToGlobal(QPoint(20, 60)));
            QTRY_VERIFY(recentMenu->isVisible());
            QApplication::processEvents();
            QVERIFY(recentMenu->grab().save(QDir(capture).filePath("RecentWorkspaces.png")));
            recentMenu->close();
            sessionMenu->popup(_app.mainWindow().mapToGlobal(QPoint(20, 60)));
            QTRY_VERIFY(sessionMenu->isVisible());
            QApplication::processEvents();
            QVERIFY(sessionMenu->grab().save(QDir(capture).filePath("RecentSessions.png")));
            sessionMenu->close();
        }
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void sessionLifecycleAndPersistence()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.settings().setValue("settings/sessionTest/value", 10));
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("session.termaster")), "Sessions", &error));
        QVERIFY(_app.workspace()->settings().setValue("sessionTest/value", 20));
        QVERIFY2(_app.createSession({}, &error), qPrintable(error));
        QVERIFY(_app.session());
        const auto generated = _app.session()->name().toLower().split(' ');
        QCOMPARE(generated.size(), 2);
        const RandomNames names;
        QVERIFY(names.adjectives().contains(generated.first(), Qt::CaseInsensitive));
        QVERIFY(names.nouns().contains(generated.last(), Qt::CaseInsensitive));
        const auto id = _app.session()->id();
        QVERIFY(!QUuid(id).isNull());
        QVERIFY(_app.session()->setName("Saved through workspace"));
        QVERIFY(_app.saveWorkspace(&error));
        QCOMPARE(_app.mainWindow().findChild<QLabel*>("sessionTitle")->text(), QString("Saved through workspace"));
        const auto directory = _app.session()->directory();
        const auto file      = _app.session()->filePath();
        QCOMPARE(directory.name(), id);
        QCOMPARE(directory.parent().toString(), _app.workspace()->sessionsDirectory().toString());
        QCOMPARE(_app.configuration().value("sessionTest/value").toInt(), 20);
        QVERIFY(_app.configuration().setValue("sessionTest/value", 30));
        QCOMPARE(_app.workspace()->settings().value("sessionTest/value").toInt(), 20);
        QCOMPARE(_app.settings().value("settings/sessionTest/value").toInt(), 10);
        QVERIFY(_app.configuration().remove("sessionTest/value"));
        QCOMPARE(_app.configuration().value("sessionTest/value").toInt(), 20);
        QVERIFY(_app.workspace()->settings().remove("sessionTest/value"));
        QCOMPARE(_app.configuration().value("sessionTest/value").toInt(), 10);
        QVERIFY(_app.configuration().setValue("sessionTest/value", 31));
        const QVariantMap object {
            {    "type",                          "terminal"},
            {  "module",                           "fixture"},
            {"settings", QVariantMap {{"port", "synthetic"}}}
        };
        QVERIFY(_app.session()->objects().setValue("source", object));
        QVERIFY(_app.session()->objects().setValue("target", object));
        QVERIFY(
            _app.session()->links().setValue("connection",
                                             QVariantMap {
                                                 {"source", QVariantMap {{"object", "source"}, {"port", "output"}}},
                                                 {"target",  QVariantMap {{"object", "target"}, {"port", "input"}}}
        }));
        QVERIFY(_app.session()->resources().setValue("script", "resources/example.txt"));
        QVERIFY(_app.session()->presentation().setValue("layout", "horizontal"));
        QVERIFY(_app.renameSession("Renamed session", &error));
        QCOMPARE(_app.session()->id(), id);
        QCOMPARE(_app.session()->filePath().toString(), file.toString());
        auto* title = _app.mainWindow().findChild<QLabel*>("sessionTitle");
        QVERIFY(title);
        QVERIFY(title->text().contains("Renamed session"));
        auto* sessionFile = _app.mainWindow().findChild<QLineEdit*>("sessionFile");
        QVERIFY(sessionFile);
        QCOMPARE(sessionFile->text(), file.toString());
        QVERIFY(_app.mainWindow().findChild<QLabel*>("sessionSummary"));
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("Session.png")));
        }
        QVERIFY(_app.saveSession(&error));
        QVERIFY(_app.saveWorkspace(&error));
        QVERIFY(_app.settings().sync(&error));
        QProcess child;
        auto     environment = QProcessEnvironment::systemEnvironment();
        environment.insert("TERMASTER_APP_RESTART_DATA", _app.paths().dataDirectory().toString());
        environment.insert("TERMASTER_APP_RESTART_SESSION", id);
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), QStringList());
        QVERIFY(child.waitForStarted(5000));
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
        Settings childSaved(file, {"termaster.session", "Termaster session"});
        QVERIFY(childSaved.load(&error));
        QCOMPARE(childSaved.value("settings/sessionTest/childAutosave").toInt(), 99);
        auto borrowed = _app.session()->objects();
        QVERIFY(_app.closeSession(&error));
        QVERIFY(!borrowed.isValid());
        QVERIFY(!_app.session());
        QVERIFY(_app.openSession(id, &error));
        QCOMPARE(_app.session()->name(), QString("Renamed session"));
        QCOMPARE(_app.configuration().value("sessionTest/value").toInt(), 31);
        QCOMPARE(_app.session()->objects().values().size(), 2);
        QCOMPARE(_app.session()->links().values().size(), 1);
        QCOMPARE(_app.session()->resources().value("script").toString(), QString("resources/example.txt"));
        QCOMPARE(_app.session()->presentation().value("layout").toString(), QString("horizontal"));
        QVERIFY(_app.configuration().setValue("sessionTest/switchSaved", 7));
        auto oldView = _app.session()->settings();
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("other.termaster")), "Other", &error));
        QVERIFY(!_app.session());
        QVERIFY(!oldView.isValid());
        Settings saved(file, {"termaster.session", "Termaster session"});
        QVERIFY(saved.load(&error));
        QCOMPARE(saved.value("settings/sessionTest/switchSaved").toInt(), 7);
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void sessionFailureRetainsOwners()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("failures.termaster")), "Failures", &error));
        QVERIFY(_app.createSession("Original", &error));
        auto*      original  = _app.session();
        auto*      workspace = _app.workspace();
        const auto history   = workspace->recentSessions();
        QVERIFY(!_app.openSession("missing", &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(_app.session(), original);
        QVERIFY(workspace->sessions().setValue("missing",
                                               QVariantMap {
                                                   {"directory", "sessions/missing"}
        }));
        QVERIFY(!_app.openSession("missing", &error));
        QCOMPARE(_app.session(), original);
        const auto badDirectory = workspace->sessionsDirectory().join("bad");
        QVERIFY(badDirectory.createDirectories());
        QVERIFY(workspace->registerSession("bad", badDirectory));
        QFile badFile(badDirectory.join("session.yaml").toString());
        QVERIFY(badFile.open(QIODevice::WriteOnly));
        QVERIFY(badFile.write("version: [invalid") > 0);
        badFile.close();
        QVERIFY(!_app.openSession("bad", &error));
        QCOMPARE(_app.session(), original);
        QVERIFY(QFile::remove(badFile.fileName()));
        QVERIFY(QDir().rmdir(badDirectory.toString()));
        Session mismatch(badDirectory, workspace->settings());
        QVERIFY(mismatch.create("different-id", "Mismatch", &error));
        QVERIFY(!_app.openSession("bad", &error));
        QCOMPARE(_app.session(), original);
        QCOMPARE(workspace->recentSessions(), history);
        const auto catalogBefore = workspace->sessionIds();
        const auto foldersBefore = QDir(workspace->sessionsDirectory().toString())
                                       .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        const auto workspaceFile = workspace->filePath().toString();
        QVERIFY(QFile::rename(workspaceFile, workspaceFile + ".backup"));
        QVERIFY(QDir().mkdir(workspaceFile));
        QVERIFY(!_app.createSession("Cannot persist catalog", &error));
        QCOMPARE(_app.session(), original);
        QCOMPARE(workspace->sessionIds(), catalogBefore);
        QCOMPARE(workspace->recentSessions(), history);
        QCOMPARE(QDir(workspace->sessionsDirectory().toString()).entryList(QDir::Dirs | QDir::NoDotAndDotDot),
                 foldersBefore);
        QVERIFY(QDir().rmdir(workspaceFile));
        QVERIFY(QFile::rename(workspaceFile + ".backup", workspaceFile));
        QVERIFY(original->settings().setValue("pending", 1));
        const auto file   = original->filePath().toString();
        const auto backup = file + ".backup";
        QVERIFY(QFile::rename(file, backup));
        QVERIFY(QDir().mkdir(file));
        QVERIFY(!_app.closeSession(&error));
        QVERIFY(!_app.closeWorkspace(&error));
        QCOMPARE(_app.workspace(), workspace);
        QCOMPARE(_app.session(), original);
        QVERIFY(original->isDirty());
        QVERIFY(QDir().rmdir(file));
        QVERIFY(QFile::rename(backup, file));
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void generatedSettingsDialog()
    {
        QString error;
        QVERIFY(_app.settings().setValue("settings/ui/toolbarVisible", true));
        QVERIFY(_app.settings().setValue("settings/ui/statusBarVisible", true));
        auto* command = _app.commands().find("Application.Settings")->actions().first();
        command->trigger();
        QPointer<QDialog> dialog(_app.mainWindow().findChild<QDialog*>("settingsDialog"));
        QVERIFY(dialog);
        auto* model = static_cast<SettingsDialog*>(dialog.data())
                          ->pageModel(static_cast<SettingsDialog*>(dialog.data())->currentPage());
        QVERIFY(model);
        model->setValue("toolbar", false);
        QVERIFY(_app.configuration().value("ui/toolbarVisible").toBool());
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(_app.configuration().value("ui/toolbarVisible").toBool());

        QTemporaryDir temporary;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("settings.termaster")), "Settings", &error));
        command->trigger();
        dialog = _app.mainWindow().findChild<QDialog*>("settingsDialog");
        QVERIFY(dialog);
        model = static_cast<SettingsDialog*>(dialog.data())
                    ->pageModel(static_cast<SettingsDialog*>(dialog.data())->currentPage());
        model->setValue("toolbar", false);
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        QVERIFY(buttons->button(QDialogButtonBox::Apply)->isEnabled());
        buttons->button(QDialogButtonBox::Apply)->click();
        QVERIFY(_app.toolbar().toolbar()->isHidden());
        QVERIFY(_app.settings().value("settings/ui/toolbarVisible").toBool());
        Settings saved(_app.workspace()->filePath(), {"termaster.workspace", "Termaster workspace"});
        QVERIFY(saved.load(&error));
        QVERIFY(!saved.value("settings/ui/toolbarVisible").toBool());
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(dialog->grab().save(QDir(capture).filePath("Settings.png")));
        }
        QVERIFY(_app.createSession("Settings session", &error));
        QTRY_VERIFY(dialog.isNull());
        command->trigger();
        dialog = _app.mainWindow().findChild<QDialog*>("settingsDialog");
        QVERIFY(dialog);
        model = static_cast<SettingsDialog*>(dialog.data())
                    ->pageModel(static_cast<SettingsDialog*>(dialog.data())->currentPage());
        model->setValue("statusBar", false);
        buttons = dialog->findChild<QDialogButtonBox*>();
        buttons->button(QDialogButtonBox::Apply)->click();
        QVERIFY(!_app.configuration().value("ui/statusBarVisible").toBool());
        QVERIFY(_app.workspace()->settings().value("ui/statusBarVisible", true).toBool());
        QVERIFY(_app.closeSession(&error));
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(!_app.toolbar().toolbar()->isHidden());
    }

    Q_SLOT void preferencesNavigationSearchAndPreview()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.settings().setValue("settings/ui/toolbarVisible", true));
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("preferences.termaster")), "Firmware lab", &error));
        QVERIFY(_app.createSession("Amber Falcon", &error));
        _app.commands().find("Application.Settings")->actions().first()->trigger();
        QPointer<SettingsDialog> dialog(_app.mainWindow().findChild<SettingsDialog*>("settingsDialog"));
        QVERIFY(dialog);
        const auto cleanup = qScopeGuard([&] {
            if (dialog)
            {
                dialog->reject();
            }
            _app.settings().setValue("settings/ui/toolbarVisible", true);
            _app.closeWorkspace();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        dialog->setFixedSize(1020, 720);
        auto* tree   = dialog->findChild<QTreeView*>("settingsTree");
        auto* search = dialog->findChild<QLineEdit*>("settingsSearch");
        QVERIFY(tree);
        QVERIFY(search);
        QCOMPARE(tree->model()->rowCount(), 4);
        for (int row = 0; row < tree->model()->rowCount(); ++row)
        {
            const auto category = tree->model()->index(row, 0);
            QVERIFY(tree->model()->rowCount(category) > 0);
            for (int child = 0; child < tree->model()->rowCount(category); ++child)
            {
                QCOMPARE(tree->model()->rowCount(tree->model()->index(child, 0, category)), 0);
            }
        }
        QVERIFY(dialog->selectPage("workspace.general"));
        auto* preview = dialog->pageModel("workspace.general");
        preview->setValue("name", "Preview-only workspace");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        buttons->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(_app.workspace()->name(), QString("Firmware lab"));
        QCOMPARE(dialog->previewSettings().node("workspace.general").value("identity/name").toString(),
                 QString("Preview-only workspace"));
        preview->setValue("name", "Firmware lab");
        auto* serial = dialog->pageModel("terminal.serial");
        serial->setValue("baudRate", 0);
        QVERIFY(!serial->isValid());
        search->setText("directory");
        buttons->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(dialog->currentPage(), QString("terminal.serial"));
        QVERIFY(search->text().isEmpty());
        serial->setValue("baudRate", 115200);
        auto* base      = dialog->pageModel("application.appearance");
        auto* workspace = dialog->pageModel("workspace.appearance");
        base->setValue("toolbar", false);
        workspace->setValue("toolbar", false);
        buttons->button(QDialogButtonBox::Apply)->click();
        QVERIFY(!base->isDirty());
        QVERIFY(!workspace->isDirty());
        QVERIFY(!_app.settings().value("settings/ui/toolbarVisible").toBool());
        QVERIFY(!_app.workspace()->settings().value("ui/toolbarVisible").toBool());
        QVERIFY(!dialog->pageModel("session.appearance")->value("toolbar").toBool());
        search->setText("baud");
        QCOMPARE(tree->model()->rowCount(), 1);
        QCOMPARE(dialog->currentPage(), QString("terminal.serial"));
        SettingsFormWidget* active = nullptr;
        for (auto* form : dialog->findChildren<SettingsFormWidget*>())
        {
            if (form->isVisible())
            {
                active = form;
            }
        }
        QVERIFY(active);
        QCOMPARE(active->visibleFields(), QStringList {"baudRate"});
        search->setText("Line parameters");
        QVERIFY(active->visibleFields().contains("dataBits"));
        QVERIFY(!active->visibleFields().contains("port"));
        search->setText("no-such-setting-5689");
        QCOMPARE(tree->model()->rowCount(), 0);
        search->clear();
        QCOMPARE(tree->model()->rowCount(), 4);

        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            const auto save = [&](const QString& id, const QString& file) {
                if (!dialog->selectPage(id))
                {
                    return false;
                }
                QApplication::processEvents();
                return dialog->size() == QSize(1020, 720) && dialog->grab().save(QDir(capture).filePath(file));
            };
            QVERIFY(save("workspace.general", "PreferencesWorkspace.png"));
            QVERIFY(save("session.layout", "PreferencesSession.png"));
            QVERIFY(save("terminal.serial", "PreferencesSerial.png"));
            search->setText("baud");
            QApplication::processEvents();
            QVERIFY(dialog->grab().save(QDir(capture).filePath("PreferencesSearch.png")));
        }
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(_app.settings().setValue("settings/ui/toolbarVisible", true));
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void serialPluginTerminalLifecycle()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString    error;
        const auto pluginFile = QDir(_app.plugins().directory())
                                    .filePath(QFileInfo(TERMASTER_SERIAL_PLUGIN_FILE).fileName());
        QVERIFY(QDir().mkpath(_app.plugins().directory()));
        QVERIFY2(QFile::copy(TERMASTER_SERIAL_PLUGIN_FILE, pluginFile), qPrintable(pluginFile));
        const auto oldFontSize   = _app.settings().value("settings/appearance/fontSize");
        const auto oldWindowSize = _app.mainWindow().size();
        const auto cleanup       = qScopeGuard([&] {
            _app.plugins().setEnabled("SerialTerminal", false);
            _app.closeWorkspace();
            _app.mainWindow().resize(oldWindowSize);
            _app.settings().setValue("settings/appearance/fontSize", oldFontSize);
            QFile::remove(pluginFile);
            _app.plugins().rescan();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QVERIFY2(_app.plugins().rescan(&error), qPrintable(error));
        QVERIFY2(_app.plugins().setEnabled("SerialTerminal", true, &error), qPrintable(error));
        auto* terminalMenu = _app.mainMenu().findMenu("/terminal");
        QVERIFY(terminalMenu);
        const QMap<QString, QString> groups {
            {       "Serial.New",     "create"},
            {   "Serial.Connect", "connection"},
            {"Serial.Disconnect", "connection"},
            {     "Serial.Close", "management"},
            {     "Serial.Clear",     "output"},
            {  "Serial.Settings",   "settings"}
        };
        QList<QPointer<QAction>> actions;
        for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        {
            auto* command = _app.commands().find(it.key());
            QVERIFY(command);
            auto* action = command->actions().first();
            actions.append(action);
            QVERIFY(!action->icon().isNull());
            QVERIFY(!action->toolTip().isEmpty());
            QVERIFY(terminalMenu->actions(it.value()).contains(action));
            QVERIFY(_app.toolbar().actions("terminal").contains(action));
        }
        QVERIFY(!_app.commands().find("Serial.New")->actions().first()->isEnabled());
        QVERIFY(_app.settings().setValue("settings/appearance/fontSize", 15));
        QVERIFY2(_app.createWorkspace(Path(temporary.filePath("serial.termaster")), "Serial test", &error),
                 qPrintable(error));
        QVERIFY(_app.workspace()->settings().setValue("appearance/fontSize", 17));
        QVERIFY2(_app.createSession("Disconnected serial", &error), qPrintable(error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        _app.pluginHost().refreshTerminalActions();
        QVERIFY(_app.commands().find("Serial.New")->actions().first()->isEnabled());
        QVERIFY2(_app.createTerminal("serial",
                                     {
                                         {"transport", QVariantMap {{"portName", "COM_TERMASTER_TEST_UNUSED"}}}
        },
                                     "Synthetic serial",
                                     &error),
                 qPrintable(error));
        QCOMPARE(_app.terminalIds().size(), 1);
        const auto id = _app.selectedTerminal();
        QVERIFY(!id.isEmpty());
        auto* device = _app.pluginHost().selectedDevice("serial");
        QVERIFY(device);
        QCOMPARE(device->state(), SerialDevice::State::Closed);
        QPointer<TerminalDisplay> display(_app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay"));
        QVERIFY(display);
        QCOMPARE(display->font().pointSizeF(), 17.0);
        QVERIFY(_app.commands().find("Serial.Connect")->actions().first()->isEnabled());
        QVERIFY(!_app.commands().find("Serial.Disconnect")->actions().first()->isEnabled());
        display->receive("synthetic output");
        QVERIFY(display->text().contains("synthetic output"));
        _app.commands().find("Serial.Clear")->actions().first()->trigger();
        QVERIFY(!display->text().contains("synthetic output"));
        const auto definition = _app.session()->objects().value(id);
        QVERIFY(!definition.toMap().value("settings").toMap().contains("appearance"));
        _app.commands().find("Serial.Settings")->actions().first()->trigger();
        QPointer<SettingsDialog> settings(_app.mainWindow().findChild<SettingsDialog*>("settingsDialog"));
        QVERIFY(settings);
        QVERIFY(settings->selectPage("terminal.connection"));
        if (QGuiApplication::platformName() == "windows")
        {
            QTRY_VERIFY(settings->screen()->availableGeometry().contains(settings->frameGeometry()));
            auto* lastField = settings->findChild<QWidget*>("reset_rts");
            QVERIFY(lastField);
            auto* scroll = lastField->parentWidget();
            while (scroll && !qobject_cast<QScrollArea*>(scroll))
            {
                scroll = scroll->parentWidget();
            }
            auto* area = qobject_cast<QScrollArea*>(scroll);
            QVERIFY(area);
            area->ensureWidgetVisible(lastField);
            QTRY_VERIFY(
                area->viewport()->rect().contains(lastField->mapTo(area->viewport(), lastField->rect().center())));
            const auto nativeCapture = qEnvironmentVariable("TERMASTER_APP_NATIVE_CAPTURE");
            if (!nativeCapture.isEmpty())
            {
                QVERIFY(QDir().mkpath(nativeCapture));
                settings->raise();
                settings->activateWindow();
                QTRY_COMPARE(QApplication::activeWindow(), settings.data());
                QApplication::processEvents();
                QVERIFY(settings->grab().save(QDir(nativeCapture).filePath("SettingsNativeBottom.png")));
                area->verticalScrollBar()->setValue(area->verticalScrollBar()->minimum());
                QApplication::processEvents();
                QVERIFY(settings->grab().save(QDir(nativeCapture).filePath("SettingsNativeTop.png")));
            }
        }
        QList<QPointer<SettingsFieldEditor>> editors;
        for (auto* editor : settings->findChildren<SettingsFieldEditor*>())
        {
            editors.append(editor);
        }
        QVERIFY(!editors.isEmpty());
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            _app.mainWindow().resize(1280, 800);
            settings->setFixedSize(1280, 800);
            QApplication::processEvents();
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("SerialTerminal.png")));
            QVERIFY(settings->grab().save(QDir(capture).filePath("SerialTerminalSettings.png")));
        }
        const auto sessionFile = _app.session()->filePath();
        QVERIFY2(_app.plugins().setEnabled("SerialTerminal", false, &error), qPrintable(error));
        QVERIFY(display.isNull());
        QVERIFY(settings.isNull());
        for (const auto& editor : editors)
        {
            QVERIFY(editor.isNull());
        }
        QVERIFY(_app.terminalIds().isEmpty());
        QVERIFY(_app.selectedTerminal().isEmpty());
        for (const auto& action : actions)
        {
            QVERIFY(action.isNull());
        }
        for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        {
            QVERIFY(!_app.commands().find(it.key()));
        }
        SettingsSchema::Field field;
        field.widget = "serialPortSelector";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, _app.settingsWidgets().create(field, nullptr));
        QCOMPARE(_app.session()->objects().value(id), definition);
        Settings persisted(sessionFile, {"termaster.session", "Termaster session"});
        QVERIFY2(persisted.load(&error), qPrintable(error));
        QCOMPARE(persisted.value("objects/" + id), definition);
        QVERIFY2(_app.plugins().setEnabled("SerialTerminal", true, &error), qPrintable(error));
        QCOMPARE(_app.terminalIds(), QStringList {id});
        QCOMPARE(_app.selectedTerminal(), id);
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        device = _app.pluginHost().selectedDevice("serial");
        QVERIFY(device);
        QCOMPARE(device->state(), SerialDevice::State::Closed);
        display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(display);
        QCOMPARE(display->font().pointSizeF(), 17.0);
        QCOMPARE(_app.session()->objects().value(id), definition);
    }

    Q_SLOT void pluginWindowLifecycle()
    {
        auto* action = _app.commands().find("Application.Plugins")->actions().first();
        action->trigger();
        auto dialogs = _app.mainWindow().findChildren<QDialog*>();
        QCOMPARE(dialogs.size(), 1);
        QPointer<QDialog> dialog(dialogs.first());
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(dialog->findChild<QTableView*>("pluginTable"));
        QVERIFY(!dialog->windowIcon().isNull());
        action->trigger();
        QCOMPARE(_app.mainWindow().findChildren<QDialog*>().size(), 1);

        const QString capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("MainWindow.png")));
            QVERIFY(QApplication::windowIcon().pixmap(64, 64).save(QDir(capture).filePath("ApplicationIcon.png")));
            QVERIFY(dialog->grab().save(QDir(capture).filePath("PluginWindow.png")));
        }
        dialog->close();
        QTRY_VERIFY(dialog.isNull());
        action->trigger();
        dialogs = _app.mainWindow().findChildren<QDialog*>();
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(dialogs.first()->isVisible());
    }
};

int main(int argc, char* argv[])
{
    const QString restartData = qEnvironmentVariable("TERMASTER_APP_RESTART_DATA");
    if (!restartData.isEmpty())
    {
        App::Options options;
        options.dataDirectory    = restartData;
        options.pluginsDirectory = restartData + "/empty-plugins";
        App app(argc, argv, options);
        int result = 1;
        QTimer::singleShot(0, &app, [&]() {
            if (qEnvironmentVariableIsSet("TERMASTER_APP_RESTART_SESSION"))
            {
                QString error;
                result = app.workspace()
                              && app.openSession(qEnvironmentVariable("TERMASTER_APP_RESTART_SESSION"), &error)
                              && app.session()->name() == "Renamed session"
                              && app.configuration().value("sessionTest/value").toInt() == 31
                              && app.session()->objects().values().size() == 2
                              && app.session()->links().values().size() == 1
                              && app.configuration().setValue("sessionTest/childAutosave", 99) ?
                             0 :
                             1;
            }
            else if (qEnvironmentVariableIsSet("TERMASTER_APP_RECOVERY"))
            {
                result = !app.workspace() && app.mainWindow().isVisible()
                              && app.mainWindow().findChild<QPushButton*>("openWorkspace")->isVisible()
                              && !app.mainWindow().findChild<QLabel*>("workspaceMessage")->text().isEmpty()
                              && !QApplication::activeModalWidget() ?
                             0 :
                             1;
            }
            else
            {
                result = app.workspace()
                              && app.workspace()->filePath().toString()
                                     == qEnvironmentVariable("TERMASTER_APP_RESTART_WORKSPACE")
                              && app.configuration().value("appearance/fontSize").toInt() == 18
                              && app.configuration().value("appearance/theme").toString() == "dark"
                              && !QApplication::activeModalWidget() ?
                             0 :
                             1;
                if (result == 0)
                {
                    result = app.settings().setValue("settings/child/autosave", 41)
                                  && app.configuration().setValue("child/autosave", 42) ?
                                 0 :
                                 1;
                }
            }
            if (!app.mainWindow().close())
            {
                result = 1;
                app.quit();
            }
        });
        const int loopResult = app.run();
        return loopResult == 0 ? result : loopResult;
    }

    QTemporaryDir temporary;
    if (!temporary.isValid())
    {
        return 1;
    }
    int                   result = 1;
    QPointer<QMainWindow> window;
    QPointer<QAction>     action;
    {
        App::Options options;
        options.dataDirectory    = temporary.path() + "/data";
        options.pluginsDirectory = temporary.path() + "/plugins";
        App      app(argc, argv, options);
        AppTests tests(app);
        window = &app.mainWindow();
        action = app.commands().find("Application.Plugins")->actions().first();
        QTimer::singleShot(0, &app, [&]() {
            result = QTest::qExec(&tests, argc, argv);
            app.quit();
        });
        const int loopResult = app.run();
        if (loopResult != 0)
        {
            result = loopResult;
        }
    }
    return window || action ? 1 : result;
}

#include "AppTests.moc"
