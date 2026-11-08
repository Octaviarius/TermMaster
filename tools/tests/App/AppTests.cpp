#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <Termaster/App/App.h>
#include <Termaster/Core/ApplicationPaths.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <Termaster/Core/Workspace.h>
#include <Termaster/Core/gui/action/MenuActionContainer.h>
#include <Termaster/Core/gui/action/MenubarActionContainer.h>
#include <Termaster/Core/gui/action/ToolbarActionContainer.h>

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
        QVERIFY(_app.workspace()->sessions().setValue("recorded", QVariantMap {}));
        QVERIFY(_app.workspace()->rememberSession("recorded", &error));
        auto* sessionMenu = static_cast<MenuActionContainer*>(_app.mainMenu().findMenu("/file/recentSessions"))->menu();
        QVERIFY(QMetaObject::invokeMethod(sessionMenu, "aboutToShow", Qt::DirectConnection));
        int visibleSessions = 0;
        for (auto* action : sessionMenu->actions())
        {
            if (action->isVisible())
            {
                ++visibleSessions;
                QVERIFY(!action->isEnabled());
                QVERIFY(action->text().contains("recorded"));
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
            if (qEnvironmentVariableIsSet("TERMASTER_APP_RECOVERY"))
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
