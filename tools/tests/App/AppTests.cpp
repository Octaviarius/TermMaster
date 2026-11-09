#include "Gui/Session/SessionPicker.h"
#include "Gui/Settings/SettingsDialog.h"
#include "Gui/Terminal/CommandEditor.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
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
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>
#include <QUuid>
#include <Termaster/App/App.h>
#include <Termaster/Core/ApplicationPaths.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <Termaster/Core/Gui/Action/MenubarActionContainer.h>
#include <Termaster/Core/Gui/Action/ToolbarActionContainer.h>
#include <Termaster/Core/Gui/Settings/SettingsFormModel.h>
#include <Termaster/Core/Gui/Settings/SettingsFormWidget.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/Plugin/PluginHost.h>
#include <Termaster/Core/Plugin/PluginManager.h>
#include <Termaster/Core/RandomNames.h>
#include <Termaster/Core/Session.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <Termaster/Core/Workspace.h>
#include <stdexcept>

/** Synthetic backend: every connection attempt is in-memory, never a device open. */
class ReconnectDevice final : public QObject, public SerialDevice
{
public:
    struct Probe
    {
        bool             available = false;
        int              attempts  = 0;
        ReconnectDevice* device    = nullptr;
        QByteArray       output;
        qint64           allowance = 2;
    };
    explicit ReconnectDevice(std::shared_ptr<Probe> probe) : _probe(std::move(probe))
    {
        _probe->device = this;
    }
    ~ReconnectDevice() override
    {
        _probe->device = nullptr;
    }
    QObject* object() override
    {
        return this;
    }
    State state() const override
    {
        return _state;
    }
    void setCallbacks(Callbacks callbacks) override
    {
        _callbacks = std::move(callbacks);
    }
    bool open(const QVariantMap&, QString* error) override
    {
        ++_probe->attempts;
        _setState(State::Opening);
        if (!_probe->available)
        {
            if (error)
            {
                *error = "Synthetic port unavailable";
            }
            drop("Synthetic port unavailable");
            return false;
        }
        _setState(State::Open);
        return true;
    }
    void close() override
    {
        _setState(State::Closing);
        _setState(State::Closed);
    }
    qint64 write(const QByteArray& bytes, QString*) override
    {
        if (_state != State::Open)
        {
            return -1;
        }
        const auto count = qMin<qint64>(bytes.size(), _probe->allowance);
        _probe->output.append(bytes.first(count));
        return count;
    }
    qint64 pendingBytes() const override
    {
        return 0;
    }
    void drop(const QString& error)
    {
        _setState(State::Failed);
        const auto callback = _callbacks.error;
        if (callback)
        {
            callback(error);
        }
    }

private:
    std::shared_ptr<Probe> _probe;
    State                  _state = State::Closed;
    Callbacks              _callbacks;
    void                   _setState(State state)
    {
        _state              = state;
        const auto callback = _callbacks.stateChanged;
        if (callback)
        {
            callback(state);
        }
    }
};

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
        for (const auto& menu : {"/file", "/edit", "/view", "/session", "/terminal", "/tools", "/help"})
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
        QVERIFY(!_app.toolbar().toolbar()->actions().contains(plugins->actions().first()));
        QCOMPARE(_app.toolbar().groups(), QStringList({"terminalCreation", "terminal"}));
        QCOMPARE(_app.toolbar().toolbar()->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QVERIFY(_app.toolbar().toolbar()->actions().isEmpty());
        for (auto* action : _app.toolbar().toolbar()->actions())
        {
            if (!action->isSeparator())
            {
                QVERIFY(!action->icon().isNull());
                QVERIFY(!action->toolTip().isEmpty());
            }
        }
        for (const auto* id : {"Workspace.New",
                               "Workspace.Open",
                               "Workspace.Save",
                               "Workspace.Close",
                               "Session.New",
                               "Session.Open",
                               "Session.Save",
                               "Session.Close"})
        {
            QVERIFY(!_app.toolbar().toolbar()->actions().contains(_app.commands().find(id)->actions().first()));
        }
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
        QVERIFY(!_app.workspace()->settings().containsOverride("ui/toolbarVisible"));
        QCOMPARE(_app.settings().value("settings/ui/toolbarVisible").toBool(), false);
        action("View.StatusBar")->trigger();
        QVERIFY(_app.mainWindow().statusBar()->isHidden());
        QVERIFY(!_app.workspace()->settings().containsOverride("ui/statusBarVisible"));
        QCOMPARE(_app.settings().value("settings/ui/statusBarVisible").toBool(), false);
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(_app.toolbar().toolbar()->isHidden());
        action("View.Toolbar")->trigger();
        action("View.StatusBar")->trigger();
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
        QVERIFY(_app.mainMenu().findMenu("/session/recentSessions"));
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
        auto* sessionMenu = static_cast<MenuActionContainer*>(_app.mainMenu().findMenu("/session/recentSessions"))
                                ->menu();
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
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.settings().setValue("settings/ui/toolbarVisible", true));
        QVERIFY(_app.settings().setValue("settings/ui/statusBarVisible", true));
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Settings.termaster")), "Settings", &error));
        const auto cleanup = qScopeGuard([&] {
            for (auto* dialog : _app.mainWindow().findChildren<SettingsDialog*>())
            {
                delete dialog;
            }
            _app.closeWorkspace();
            auto* action = _app.commands().find("View.Toolbar")->actions().first();
            if (!action->isChecked())
            {
                action->trigger();
            }
        });
        _app.commands().find("Application.Settings")->actions().first()->trigger();
        QPointer<SettingsDialog> dialog(_app.mainWindow().findChild<SettingsDialog*>("settingsDialog"));
        QVERIFY(dialog);
        QVERIFY(dialog->pageModel("application.appearance"));
        QVERIFY(!dialog->pageModel("workspace.terminal"));
        QVERIFY(!dialog->pageModel("terminal.connection"));
        auto* model = dialog->pageModel("application.appearance");
        model->setValue("toolbar", false);
        QVERIFY(!_app.toolbar().toolbar()->isHidden());
        auto* reset = dialog->findChild<QToolButton*>("reset_toolbar");
        QVERIFY(reset && !reset->isHidden());
        auto* booleanEditor = reset->parentWidget()->findChild<QCheckBox*>();
        QVERIFY(booleanEditor);
        QApplication::processEvents();
        const int gap = reset->mapTo(dialog, QPoint()).x()
                      - booleanEditor->mapTo(dialog, QPoint(booleanEditor->width(), 0)).x();
        QVERIFY2(gap >= 0 && gap <= 24, qPrintable(QString::number(gap)));
        const auto interfaceCapture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!interfaceCapture.isEmpty())
        {
            QVERIFY(QDir().mkpath(interfaceCapture));
            dialog->setFixedSize(1280, 800);
            QApplication::processEvents();
            QVERIFY(dialog->grab().save(QDir(interfaceCapture).filePath("InterfaceReset.png")));
        }
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(!_app.toolbar().toolbar()->isHidden());
        _app.commands().find("Application.Settings")->actions().first()->trigger();
        dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        model  = dialog->pageModel("application.appearance");
        model->setValue("toolbar", false);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QVERIFY(_app.toolbar().toolbar()->isHidden());
        QVERIFY(!_app.settings().value("settings/ui/toolbarVisible").toBool());
        QVERIFY(!_app.workspace()->settings().containsOverride("ui/toolbarVisible"));
        QVERIFY(_app.createSession("Settings session", &error));
        QVERIFY(dialog);
        auto* applicationDraft = dialog->pageModel("application.terminal");
        applicationDraft->setValue("fontSize", 18.0);
        _app.commands().find("Session.Settings")->actions().first()->trigger();
        QVERIFY(dialog);
        QVERIFY(applicationDraft->isDirty());
        for (auto* candidate : _app.mainWindow().findChildren<SettingsDialog*>("settingsDialog"))
        {
            if (candidate->pageModel("session.terminal"))
            {
                dialog = candidate;
            }
        }
        QVERIFY(dialog->pageModel("session.terminal"));
        QVERIFY(!dialog->pageModel("application.appearance"));
        QVERIFY(!dialog->pageModel("workspace.terminal"));
        auto*      font      = dialog->pageModel("session.terminal");
        const auto inherited = font->value("fontSize");
        font->setValue("fontSize", 19.0);
        QCOMPARE(_app.session()->settings().value("appearance/fontSize", inherited), inherited);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(_app.session()->settings().value("appearance/fontSize").toDouble(), 19.0);
        QVERIFY(!_app.workspace()->settings().containsOverride("appearance/fontSize"));
        QVERIFY(_app.closeSession(&error));
        QTRY_VERIFY(dialog.isNull());
    }

    Q_SLOT void preferencesNavigationSearchAndPreview()
    {
        QString error;
        _app.commands().find("Application.Settings")->actions().first()->trigger();
        QPointer<SettingsDialog> dialog(_app.mainWindow().findChild<SettingsDialog*>("settingsDialog"));
        QVERIFY(dialog);
        const auto cleanup = qScopeGuard([&] {
            if (dialog)
            {
                delete dialog.data();
            }
        });
        auto*      tree    = dialog->findChild<QTreeView*>("settingsTree");
        auto*      search  = dialog->findChild<QLineEdit*>("settingsSearch");
        QVERIFY(tree);
        QVERIFY(search);
        QCOMPARE(tree->model()->rowCount(), 2);
        QVERIFY(dialog->selectPage("application.terminal"));
        auto*      model         = dialog->pageModel("application.terminal");
        const auto inheritedSize = model->value("fontSize");
        auto*      preview       = dialog->findChild<TerminalDisplay*>("settingsAppearancePreview");
        QVERIFY(preview);
        model->setValue("fontSize", 21.0);
        QCOMPARE(preview->font().pointSizeF(), 21.0);
        model->setValue("fontSize", 72.0);
        QApplication::processEvents();
        const auto previewImage      = preview->viewport()->grab().toImage();
        const auto previewBackground = QColor(model->value("background").toString()).rgb();
        bool       visibleText       = false;
        for (int y = 0; y < previewImage.height() && !visibleText; ++y)
        {
            for (int x = 0; x < previewImage.width(); ++x)
            {
                if (previewImage.pixelColor(x, y).rgb() != previewBackground)
                {
                    visibleText = true;
                    break;
                }
            }
        }
        QVERIFY(visibleText);
        model->setValue("fontSize", 21.0);
        QVERIFY(!(_app.settings().value("settings/appearance/fontSize").toDouble() == 21.0));
        search->setText("background");
        QCOMPARE(tree->model()->rowCount(), 1);
        QCOMPARE(dialog->currentPage(), QString("application.terminal"));
        SettingsFormWidget* active = nullptr;
        for (auto* form : dialog->findChildren<SettingsFormWidget*>())
        {
            if (form->isVisible())
            {
                active = form;
            }
        }
        QVERIFY(active);
        QCOMPARE(active->visibleFields(), QStringList {"background"});
        search->setText("no-such-setting-5689");
        QCOMPARE(tree->model()->rowCount(), 0);
        search->clear();
        QCOMPARE(model->value("fontSize").toDouble(), 21.0);
        model->removeValue("fontSize");
        QCOMPARE(preview->font().pointSizeF(), inheritedSize.toDouble());
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            dialog->setFixedSize(1280, 800);
            QVERIFY(dialog->selectPage("application.terminal"));
            QApplication::processEvents();
            QVERIFY(dialog->grab().save(QDir(capture).filePath("ApplicationSettings.png")));
        }
    }

    Q_SLOT void sessionPickerKeepsIdentitySeparate()
    {
        const QString first("11111111-1111-1111-1111-111111111111");
        const QString second("22222222-2222-2222-2222-222222222222");
        SessionPicker picker(
            {
                {"Same readable name",  first},
                {"Same readable name", second}
        },
            &_app.mainWindow());
        picker.show();
        auto* combo = picker.findChild<QComboBox*>("sessionChoice");
        QVERIFY(combo);
        QCOMPARE(combo->currentText(), QString("Same readable name"));
        QCOMPARE(picker.selectedSession(), first);
        combo->setCurrentIndex(1);
        QCOMPARE(picker.selectedSession(), second);
        QVERIFY(combo->itemData(1, Qt::AccessibleTextRole).toString().contains(second));
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QApplication::processEvents();
            QPixmap canvas(1280, 800);
            canvas.fill(picker.palette().color(QPalette::Window));
            QPainter painter(&canvas);
            painter.drawPixmap(QPoint((1280 - picker.width()) / 2, (800 - picker.height()) / 2), picker.grab());
            painter.end();
            QVERIFY(canvas.save(QDir(capture).filePath("SessionPicker.png")));
        }
        picker.reject();
    }

    Q_SLOT void terminalTabTypeAndEndpoint()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString error;
        QVERIFY2(_app.createWorkspace(Path(temporary.filePath("Tabs.termaster")), "Tab fixture", &error),
                 qPrintable(error));
        QVERIFY2(_app.createSession("Tab fixture", &error), qPrintable(error));
        auto&      host         = _app.pluginHost();
        const auto probe        = std::make_shared<ReconnectDevice::Probe>();
        const auto registration = [probe](const QString& id) {
            PluginHost::TerminalType type;
            type.id             = id;
            type.title          = "Fixture " + id;
            type.plugin         = "TabFixture";
            type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic tab presentation fixture
content:
  type: object
  properties:
    host:
      type: string
      default: localhost
      x-settings-path: transport/host
    port:
      type: integer
      default: 5000
      x-settings-path: transport/port
    portName:
      type: string
      default: COM1
      x-settings-path: transport/portName
    program:
      type: string
      default: ''
      x-settings-path: transport/program
)";
            type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
            return type;
        };
        const auto cleanup = qScopeGuard([&] {
            _app.closeWorkspace();
            for (const auto& id : {"socket", "serial", "console"})
            {
                host.unregisterTerminalType(id);
            }
        });
        for (const auto& id : {"socket", "serial", "console"})
        {
            host.registerTerminalType(registration(id));
        }
        auto* tabs = _app.mainWindow().findChild<QTabWidget*>("terminalTabs");
        QVERIFY(tabs);
        QVERIFY2(_app.createTerminal("socket", {}, "Saved <friendly> socket", &error), qPrintable(error));
        const auto socketId = _app.selectedTerminal();
        QCOMPARE(tabs->tabText(0), "Socket\nlocalhost:5000");
        const auto socketGlyph = qvariant_cast<QIcon>(tabs->tabBar()->tabData(0)).pixmap(16, 16).toImage();
        QVERIFY(!socketGlyph.isNull());
        QVERIFY(tabs->tabBar()->tabButton(0, QTabBar::LeftSide));
        QCOMPARE(tabs->tabBar()->tabButton(0, QTabBar::LeftSide)->objectName(), "terminalStatusIndicator");
        QVERIFY(QTextDocumentFragment::fromHtml(tabs->tabToolTip(0)).toPlainText().contains("Saved <friendly> socket"));
        QVERIFY(tabs->tabBar()->accessibleTabName(0).contains("localhost:5000"));
        QVERIFY(tabs->tabBar()->accessibleTabName(0).contains("Disconnected"));

        host.showSelectedTerminalSettings();
        auto* dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(dialog);
        auto* model = dialog->pageModel("terminal.connection");
        QVERIFY(model);
        QVERIFY(_app.workspace()->settings().setValue("transport/host", "inherited.example"));
        dialog->refreshInheritedValues();
        model->setValue("port", 5001);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(tabs->tabText(0), "Socket\ninherited.example:5001");
        QVERIFY(!_app.session()->objects().node(socketId + "/settings").contains("transport/host"));
        model->setValue("host", "2001:db8::1");
        model->setValue("port", 6000);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(tabs->tabText(0), "Socket\n[2001:db8::1]:6000");
        delete dialog;
        const auto objectBefore = _app.session()->objects().value(socketId);
        QVERIFY(host.unregisterTerminalType("socket", &error));
        host.registerTerminalType(registration("socket"));
        QCOMPARE(_app.selectedTerminal(), socketId);
        QCOMPARE(tabs->tabText(0), "Socket\n[2001:db8::1]:6000");
        QCOMPARE(_app.session()->objects().value(socketId), objectBefore);
        QCOMPARE(qvariant_cast<QIcon>(tabs->tabBar()->tabData(0)).pixmap(16, 16).toImage(), socketGlyph);
        QVERIFY(tabs->tabBar()->accessibleTabName(0).contains("Saved <friendly> socket"));

        QVERIFY2(_app.createTerminal("serial", {}, "Saved serial", &error), qPrintable(error));
        QCOMPARE(tabs->tabText(1), "Serial\nCOM1");
        QVERIFY2(_app.createTerminal("console",
                                     {
                                         {"transport",
                                          QVariantMap {{"program", "sh"},
                                                       {"arguments", QStringList {"private-argument"}},
                                                       {"environment", QVariantMap {{"TOKEN", "private-token"}}}}}
        },
                                     "Saved console",
                                     &error),
                 qPrintable(error));
        QCOMPARE(tabs->tabText(2), "Console\nsh");
        const auto serialGlyph  = qvariant_cast<QIcon>(tabs->tabBar()->tabData(1)).pixmap(16, 16).toImage();
        const auto consoleGlyph = qvariant_cast<QIcon>(tabs->tabBar()->tabData(2)).pixmap(16, 16).toImage();
        QVERIFY(!serialGlyph.isNull());
        QVERIFY(!consoleGlyph.isNull());
        QVERIFY(serialGlyph != socketGlyph);
        QVERIFY(consoleGlyph != socketGlyph);
        QVERIFY(consoleGlyph != serialGlyph);
        QVERIFY(!tabs->tabToolTip(2).contains("private-"));
        QVERIFY(!tabs->tabBar()->accessibleTabName(2).contains("private-"));
        tabs->tabBar()->moveTab(2, 0);
        QCOMPARE(tabs->tabText(tabs->currentIndex()), "Console\nsh");
        QCOMPARE(qvariant_cast<QIcon>(tabs->tabBar()->tabData(0)).pixmap(16, 16).toImage(), consoleGlyph);
        QCOMPARE(qvariant_cast<QIcon>(tabs->tabBar()->tabData(1)).pixmap(16, 16).toImage(), socketGlyph);
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        QCoreApplication::processEvents();
        QVERIFY(tabs->tabBar()->tabRect(0).height() >= 2 * tabs->tabBar()->fontMetrics().height());
        const auto closeSide = tabs->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition) == QTabBar::LeftSide ?
                                   QTabBar::LeftSide :
                                   QTabBar::RightSide;
        for (int index = 0; index < tabs->count(); ++index)
        {
            const auto* close = tabs->tabBar()->tabButton(index, closeSide);
            QVERIFY(close);
            const auto firstLineCenter = tabs->tabBar()->tabRect(index).center().y()
                                       - tabs->tabBar()->fontMetrics().lineSpacing() / 2;
            QVERIFY(qAbs(close->geometry().center().y() - firstLineCenter) <= 3);
            auto* status = tabs->tabBar()->tabButton(index, QTabBar::LeftSide);
            QVERIFY(status);
            QCOMPARE(status->objectName(), "terminalStatusIndicator");
            QVERIFY(qAbs(status->geometry().center().y() - firstLineCenter) <= 3);
            QVERIFY(!status->grab().isNull());
        }
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("TerminalTabs.png")));
        }

        const auto originalScheme  = QApplication::styleHints()->colorScheme();
        const auto originalPalette = QApplication::palette();
        const auto restoreScheme   = qScopeGuard([originalScheme, originalPalette] {
            QApplication::styleHints()->setColorScheme(originalScheme);
            QApplication::setPalette(originalPalette);
        });
        for (const auto scheme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark})
        {
            QApplication::styleHints()->setColorScheme(scheme);
            auto colors = originalPalette;
            if (scheme == Qt::ColorScheme::Dark)
            {
                colors.setColor(QPalette::Window, QColor("#202020"));
                colors.setColor(QPalette::WindowText, QColor("#eeeeee"));
                colors.setColor(QPalette::Base, QColor("#161616"));
                colors.setColor(QPalette::Button, QColor("#303030"));
                colors.setColor(QPalette::ButtonText, QColor("#eeeeee"));
                colors.setColor(QPalette::Highlight, QColor("#4aa3ff"));
                colors.setColor(QPalette::HighlightedText, QColor("#000000"));
            }
            QApplication::setPalette(colors);
            QCoreApplication::processEvents();
            for (const int selected : {0, 1})
            {
                tabs->setCurrentIndex(selected);
                QCoreApplication::processEvents();
                const auto image  = tabs->tabBar()->grab().toImage();
                const auto ratio  = image.devicePixelRatio();
                const auto stripe = tabs->tabBar()->tabRect(selected).bottomLeft() + QPoint(8, -1);
                QCOMPARE(image.pixelColor(qRound(stripe.x() * ratio), qRound(stripe.y() * ratio)),
                         tabs->tabBar()->palette().color(QPalette::Highlight));
                const auto selectedBackground = tabs->tabBar()->tabRect(selected).bottomLeft() + QPoint(3, -5);
                const auto inactiveBackground = tabs->tabBar()->tabRect(1 - selected).bottomLeft() + QPoint(3, -5);
                QCOMPARE(image.pixelColor(qRound(inactiveBackground.x() * ratio),
                                          qRound(inactiveBackground.y() * ratio)),
                         QApplication::palette().color(QPalette::Window));
                QVERIFY(image.pixelColor(qRound(selectedBackground.x() * ratio), qRound(selectedBackground.y() * ratio))
                        != QApplication::palette().color(QPalette::Window));
                const auto inactiveStripe = tabs->tabBar()->tabRect(1 - selected).bottomLeft() + QPoint(8, -1);
                QVERIFY(image.pixelColor(qRound(inactiveStripe.x() * ratio), qRound(inactiveStripe.y() * ratio))
                        != tabs->tabBar()->palette().color(QPalette::Highlight));
            }
            if (!capture.isEmpty())
            {
                const auto name = scheme == Qt::ColorScheme::Dark ? "TerminalTabsSelectedDark.png" :
                                                                    "TerminalTabsSelectedLight.png";
                QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath(name)));
            }
        }
        QApplication::styleHints()->setColorScheme(originalScheme);
        QApplication::setPalette(originalPalette);
        QCoreApplication::processEvents();
        tabs->setCurrentIndex(1);
        host.showSelectedTerminalSettings();
        dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(dialog);
        model               = dialog->pageModel("terminal.connection");
        const auto longHost = QString(180, 'a') + ".example";
        model->setValue("host", longHost);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        const auto label = tabs->tabText(1);
        QVERIFY(label.startsWith("Socket\n"));
        QVERIFY(label.size() < longHost.size());
        QVERIFY(tabs->tabToolTip(1).contains(longHost));
        QVERIFY(tabs->tabBar()->accessibleTabName(1).contains(longHost));
        delete dialog;
        if (!capture.isEmpty())
        {
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("TerminalTabsLongEndpoint.png")));
        }
        QCOMPARE(probe->attempts, 0);
    }
    Q_SLOT void socketPluginTerminalLifecycle()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString    error;
        const auto pluginFile = QDir(_app.plugins().directory())
                                    .filePath(QFileInfo(TERMASTER_SOCKET_PLUGIN_FILE).fileName());
        QVERIFY(QDir().mkpath(_app.plugins().directory()));
        QVERIFY(QFile::copy(TERMASTER_SOCKET_PLUGIN_FILE, pluginFile));
        const auto cleanup = qScopeGuard([&] {
            _app.plugins().setEnabled("SocketTerminal", false);
            _app.closeWorkspace();
            QFile::remove(pluginFile);
            _app.plugins().rescan();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QVERIFY2(_app.plugins().rescan(&error), qPrintable(error));
        QVERIFY2(_app.plugins().setEnabled("SocketTerminal", true, &error), qPrintable(error));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QVERIFY2(_app.createWorkspace(Path(temporary.filePath("Socket.termaster")), "Socket fixture", &error),
                 qPrintable(error));
        QVERIFY2(_app.createSession("Socket fixture", &error), qPrintable(error));
        QVERIFY2(_app.createTerminal("socket",
                                     {
                                         {"transport",
                                          QVariantMap {{"protocol", "TCP"},
                                                       {"host", "127.0.0.1"},
                                                       {"port", int(server.serverPort())}}}
        },
                                     "Socket acceptance",
                                     &error),
                 qPrintable(error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        auto& host   = _app.pluginHost();
        auto* device = host.selectedDevice("socket");
        QVERIFY(device);
        QVERIFY(device->autoReconnect());
        auto* display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(display);
        host.connectSelectedTerminal();
        QVERIFY(host.connectionRequested("socket"));
        QTRY_COMPARE(device->state(), SerialDevice::State::Open);
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
        peer->write(QByteArray::fromHex("1b5b324a1b5b48536f636b6574201b5b33326d5443501b5b306d0d0a"));
        QTRY_VERIFY(display->text().contains("Socket TCP"));
        QByteArray outbound;
        connect(peer.get(), &QIODevice::readyRead, this, [&] { outbound += peer->readAll(); });
        QTest::keyClicks(display, "ping");
        QTest::keyClick(display, Qt::Key_Return);
        QTRY_VERIFY(outbound.contains("ping\r"));
        peer->disconnectFromHost();
        QTRY_VERIFY(device->state() != SerialDevice::State::Open);
        QVERIFY(host.connectionRequested("socket"));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
        peer.reset(server.nextPendingConnection());
        QTRY_COMPARE(device->state(), SerialDevice::State::Open);
        host.disconnectSelectedTerminal();
        QVERIFY(!host.connectionRequested("socket"));
        QCOMPARE(device->state(), SerialDevice::State::Closed);
        QTest::qWait(1200);
        QVERIFY(!server.hasPendingConnections());
        host.showSelectedTerminalSettings();
        auto* dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(dialog);
        auto* model = dialog->pageModel("terminal.connection");
        QVERIFY(model && model->isValid());
        QCOMPARE(model->value("protocol").toString(), "TCP");
        model->setValue("protocol", "UDP");
        QVERIFY2(model->apply(&error), qPrintable(error));
        QPointer<SettingsDialog> liveDialog(dialog);
        QVERIFY2(_app.plugins().setEnabled("SocketTerminal", false, &error), qPrintable(error));
        QVERIFY(liveDialog.isNull());
        QVERIFY(_app.terminalIds().isEmpty());
        QVERIFY(!_app.commands().find("Socket.New"));
        // Persisted objects retain their type/settings and reconstruct disconnected.
        QVERIFY2(_app.plugins().setEnabled("SocketTerminal", true, &error), qPrintable(error));
        QCOMPARE(_app.terminalIds().size(), 1);
        device = host.selectedDevice("socket");
        QVERIFY(device);
        QCOMPARE(device->state(), SerialDevice::State::Closed);
        QVERIFY(!host.connectionRequested("socket"));
    }
    Q_SLOT void consistentTerminalControls()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString     error;
        QStringList files;
        const auto  originalSize = _app.mainWindow().size();
        const auto  cleanup      = qScopeGuard([&] {
            for (const auto* id : {"SerialTerminal", "ConsoleTerminal", "SocketTerminal"})
            {
                _app.plugins().setEnabled(id, false);
            }
            _app.closeWorkspace();
            for (const auto& file : files)
            {
                QFile::remove(file);
            }
            _app.plugins().rescan();
            _app.mainWindow().resize(originalSize);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QVERIFY(QDir().mkpath(_app.plugins().directory()));
        for (const auto* source :
             {TERMASTER_SOCKET_PLUGIN_FILE, TERMASTER_SERIAL_PLUGIN_FILE, TERMASTER_CONSOLE_PLUGIN_FILE})
        {
            const auto file = QDir(_app.plugins().directory()).filePath(QFileInfo(source).fileName());
            QVERIFY(QFile::copy(source, file));
            files.append(file);
        }
        QVERIFY2(_app.plugins().rescan(&error), qPrintable(error));
        // Deliberately load in a different order from the toolbar's semantic ordering.
        for (const auto* id : {"SocketTerminal", "SerialTerminal", "ConsoleTerminal"})
        {
            QVERIFY2(_app.plugins().setEnabled(id, true, &error), qPrintable(error));
        }
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("controls.termaster")), "Controls", &error));
        QVERIFY(_app.createSession("Controls", &error));
        auto* tabs = _app.mainWindow().findChild<QTabWidget*>("terminalTabs");
        QVERIFY(tabs);
        _app.mainWindow().resize(1280, 800);
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        QStringList terminalIds;
        for (const auto* type : {"serial", "socket", "console"})
        {
            QVariantMap transport;
            if (QString(type) == "serial")
            {
                transport.insert("portName", "COM_TERMASTER_TEST_UNUSED");
            }
            else if (QString(type) == "socket")
            {
                transport.insert("host", "127.0.0.1");
            }
            QVERIFY2(_app.createTerminal(type,
                                         {
                                             {"transport", transport}
            },
                                         QString(type),
                                         &error),
                     qPrintable(error));
            terminalIds.append(_app.selectedTerminal());
            QVERIFY(tabs->currentWidget()->findChild<TerminalDisplay*>());
            auto* display = tabs->currentWidget()->findChild<TerminalDisplay*>();
            display->receive(QByteArray("KEEP ") + type);
            _app.mainWindow().activateWindow();
            QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
            _app.pluginHost().refreshTerminalActions();
            QStringList visible;
            for (const auto* action : _app.toolbar().toolbar()->actions())
            {
                if (action->isVisible() && !action->isSeparator())
                {
                    visible.append(action->text());
                }
            }
            const bool console = QString(type) == "console";
            QCOMPARE(visible,
                     QStringList({"New console",
                                  "New serial",
                                  "New socket",
                                  console ? "Start" : "Connect",
                                  console ? "Stop" : "Disconnect",
                                  console ? "Restart" : "Reconnect",
                                  "Clear screen",
                                  "Settings..."}));
            QVERIFY(!visible.contains("Close"));
            QCOMPARE(_app.pluginHost().selectedDevice(type)->state(), SerialDevice::State::Closed);
            const auto capture = qEnvironmentVariable("TERMASTER_CONTROLS_CAPTURE");
            if (!capture.isEmpty())
            {
                QVERIFY(QDir().mkpath(capture));
                QCoreApplication::processEvents();
                QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath(QString(type) + ".png")));
            }
        }
        const auto menuCapture = qEnvironmentVariable("TERMASTER_CONTROLS_CAPTURE");
        if (!menuCapture.isEmpty())
        {
            auto* terminalMenu = static_cast<MenuActionContainer*>(_app.mainMenu().findMenu("/terminal"))->menu();
            terminalMenu->popup(_app.mainWindow().mapToGlobal(QPoint(250, 60)));
            QTest::qWait(100);
            auto     canvas = _app.mainWindow().grab();
            QPainter painter(&canvas);
            painter.drawPixmap(_app.mainWindow().mapFromGlobal(terminalMenu->pos()), terminalMenu->grab());
            painter.end();
            QVERIFY(canvas.save(QDir(menuCapture).filePath("TerminalMenu.png")));
            terminalMenu->close();
        }
        auto* bar      = tabs->tabBar();
        auto  openMenu = [&](int index) {
            const auto point = bar->tabRect(index).center();
            QMetaObject::invokeMethod(bar, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point));
            return _app.mainWindow().findChild<QMenu*>("terminalTabMenu");
        };
        auto* menu = openMenu(0); // Right-click an inactive serial tab.
        QVERIFY(menu);
        QCOMPARE(_app.selectedTerminal(), terminalIds.at(0));
        QVERIFY(menu->findChild<QAction*>("Serial.Close"));
        QVERIFY(!menu->findChild<QAction*>("Serial.New"));
        QVERIFY(!menu->findChild<QAction*>("Console.Environment.Refresh"));
        QVERIFY(menu->findChild<QAction*>("Serial.Connect")->isEnabled());
        QVERIFY(!menu->findChild<QAction*>("Serial.Disconnect")->isEnabled());
        QVERIFY(!menu->findChild<QAction*>("Serial.Reconnect")->isEnabled());
        const auto capture = qEnvironmentVariable("TERMASTER_CONTROLS_CAPTURE");
        if (!capture.isEmpty())
        {
            QTest::qWait(100);
            auto     canvas = _app.mainWindow().grab();
            QPainter painter(&canvas);
            painter.drawPixmap(_app.mainWindow().mapFromGlobal(menu->pos()), menu->grab());
            painter.end();
            QVERIFY(canvas.save(QDir(capture).filePath("TabContextMenu.png")));
        }
        // Moving tabs must not redirect an already offered command to another terminal.
        bar->moveTab(0, 2);
        menu->findChild<QAction*>("Serial.Clear")->trigger();
        QVERIFY(tabs->currentWidget()->findChild<TerminalDisplay*>()->text().trimmed().isEmpty());
        QVERIFY(tabs->widget(0)->findChild<TerminalDisplay*>()->text().contains("KEEP socket"));
        menu->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        menu = openMenu(2);
        QVERIFY(menu);
        QPointer<QAction> close = menu->findChild<QAction*>("Serial.Close");
        QVERIFY(close);
        _app.pluginHost().closeSelectedTerminal();
        close->trigger(); // A stale menu cannot close the newly selected terminal.
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(_app.terminalIds().size(), 2);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        menu = openMenu(0);
        QVERIFY(menu);
        QPointer<QAction> stale = menu->findChild<QAction*>("Socket.Close");
        QVERIFY(stale);
        QVERIFY(_app.plugins().setEnabled("SocketTerminal", false, &error));
        QVERIFY(!stale->isEnabled());
        stale->trigger();
        QCOMPARE(tabs->count(), 1);
        QVERIFY(_app.pluginHost().hasSelectedTerminal("console"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto count = _app.mainWindow().findChildren<QMenu*>("terminalTabMenu").size();
        QMetaObject::invokeMethod(bar,
                                  "customContextMenuRequested",
                                  Qt::DirectConnection,
                                  Q_ARG(QPoint, QPoint(bar->width() - 1, bar->height() - 1)));
        QCOMPARE(_app.mainWindow().findChildren<QMenu*>("terminalTabMenu").size(), count);
        menu = openMenu(0);
        QVERIFY(menu);
        auto* closeConsole = menu->findChild<QAction*>("Console.Close");
        QVERIFY(closeConsole && closeConsole->isEnabled());
        closeConsole->trigger();
        QCOMPARE(tabs->count(), 0);
        QVERIFY(_app.terminalIds().isEmpty());
    }

    Q_SLOT void consolePluginTerminalLifecycle()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString    error;
        const auto pluginFile = QDir(_app.plugins().directory())
                                    .filePath(QFileInfo(TERMASTER_CONSOLE_PLUGIN_FILE).fileName());
        QVERIFY(QDir().mkpath(_app.plugins().directory()));
        QVERIFY(QFile::copy(TERMASTER_CONSOLE_PLUGIN_FILE, pluginFile));
        const auto cleanup = qScopeGuard([&] {
            _app.plugins().setEnabled("ConsoleTerminal", false);
            _app.closeWorkspace();
            QFile::remove(pluginFile);
            _app.plugins().rescan();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QVERIFY2(_app.plugins().rescan(&error), qPrintable(error));
        QVERIFY2(_app.plugins().setEnabled("ConsoleTerminal", true, &error), qPrintable(error));
        QVERIFY2(_app.createWorkspace(Path(temporary.filePath("console.termaster")), "Console test", &error),
                 qPrintable(error));
        QVERIFY2(_app.createSession("Local console", &error), qPrintable(error));
        QVariantMap transport;
#ifdef Q_OS_WIN
        transport.insert("program",
                         qEnvironmentVariable("SystemRoot") + "/System32/WindowsPowerShell/v1.0/powershell.exe");
        transport.insert("arguments",
                         QStringList {"-NoProfile",
                                      "-NonInteractive",
                                      "-Command",
                                      "Write-Output 'TERMASTER_CONSOLE_ACCEPTANCE'; exit 7"});
#else
        transport.insert("program", "/bin/sh");
        transport.insert("arguments", QStringList {"-c", "printf 'TERMASTER_CONSOLE_ACCEPTANCE\\n'; exit 7"});
#endif
        QVERIFY2(_app.createTerminal("console",
                                     {
                                         {"transport", transport}
        },
                                     "Console acceptance",
                                     &error),
                 qPrintable(error));
        const auto menuCapture = qEnvironmentVariable("TERMASTER_TERMINAL_MENU_CAPTURE");
        if (!menuCapture.isEmpty())
        {
            for (const auto* source : {TERMASTER_SERIAL_PLUGIN_FILE, TERMASTER_SOCKET_PLUGIN_FILE})
            {
                QVERIFY(QFile::copy(source, QDir(_app.plugins().directory()).filePath(QFileInfo(source).fileName())));
            }
            QVERIFY(_app.plugins().rescan(&error));
            QVERIFY(_app.plugins().setEnabled("SerialTerminal", true, &error));
            QVERIFY(_app.plugins().setEnabled("SocketTerminal", true, &error));
            _app.mainWindow().hide();
            _app.mainWindow().show();
            _app.mainWindow().activateWindow();
            QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
            _app.pluginHost().refreshTerminalActions();
            auto* menu = static_cast<MenuActionContainer*>(_app.mainMenu().findMenu("/terminal"))->menu();
            menu->popup(QPoint(20, 20));
            QTest::qWait(100);
            QPixmap canvas(1280, 800);
            canvas.fill(_app.mainWindow().palette().color(QPalette::Window));
            QPainter painter(&canvas);
            painter.drawPixmap(20, 20, menu->grab());
            painter.end();
            QVERIFY(QDir().mkpath(menuCapture));
            QVERIFY(canvas.save(QDir(menuCapture).filePath("TerminalMenu.png")));
            _app.mainWindow().resize(1280, 800);
            QVERIFY(_app.mainWindow().grab().save(QDir(menuCapture).filePath("TerminalToolbar.png")));
            menu->close();
            return;
        }
        const auto launchCapture = qEnvironmentVariable("TERMASTER_SHELL_GUI_CAPTURE");
        if (!launchCapture.isEmpty())
        {
            // Native rendering preview only: editing elevation must not launch UAC.
            _app.pluginHost().showSelectedTerminalSettings();
            auto* dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
            QVERIFY(dialog && dialog->selectPage("terminal.connection"));
            auto* model = dialog->pageModel("terminal.connection");
            model->setValue("program", "pwsh.exe");
            model->setValue("arguments", QVariantList {});
            model->setValue("runAsAdministrator", true);
            QVERIFY(model->isValid());
            auto* search = dialog->findChild<QLineEdit*>("settingsSearch");
            QVERIFY(search);
            search->setText("Launch");
            auto*      form  = dialog->findChild<SettingsFormWidget*>();
            const auto forms = dialog->findChildren<SettingsFormWidget*>();
            for (auto* candidate : forms)
            {
                if (candidate->editor("program"))
                {
                    form = candidate;
                }
            }
            QVERIFY(form && form->visibleFields().contains("runAsAdministrator"));
            QVERIFY(!form->visibleFields().contains("environment"));
            dialog->resize(1280, 800);
            QTest::qWait(100);
            auto* combo = dialog->findChild<QComboBox*>("consoleShellCombo");
            QVERIFY(combo && combo->isEditable());
            combo->showPopup();
            QTest::qWait(100);
            auto     image = dialog->grab();
            QPainter painter(&image);
            auto*    popup = combo->view()->window();
            painter.drawPixmap(popup->mapToGlobal(QPoint()) - dialog->mapToGlobal(QPoint()), popup->grab());
            painter.end();
            QVERIFY(QDir().mkpath(launchCapture));
            QVERIFY(image.save(QDir(launchCapture).filePath("ShellSelector.png")));
            combo->hidePopup();
            return;
        }
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        auto* device = _app.pluginHost().selectedDevice("console");
        QVERIFY(device);
        QVERIFY(!device->autoReconnect());
        auto* display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(display);
        _app.pluginHost().connectSelectedTerminal();
        QTRY_VERIFY_WITH_TIMEOUT(display->text().contains("TERMASTER_CONSOLE_ACCEPTANCE"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!_app.pluginHost().connectionRequested("console"), 15000);
        QCOMPARE(device->state(), SerialDevice::State::Closed);
        auto* tabs = _app.mainWindow().findChild<QTabWidget*>("terminalTabs");
        QVERIFY(tabs);
        QVERIFY(QTextDocumentFragment::fromHtml(tabs->tabToolTip(tabs->currentIndex()))
                    .toPlainText()
                    .contains("Process exited with code 7"));
        _app.pluginHost().showSelectedTerminalSettings();
        QTRY_VERIFY(_app.mainWindow().findChild<SettingsDialog*>());
        auto* settings = _app.mainWindow().findChild<SettingsDialog*>();
        QVERIFY(settings->pageModel("terminal.connection"));
        auto* shell = settings->findChild<QComboBox*>("consoleShellCombo");
        QVERIFY(shell && shell->isEditable());
        QCOMPARE(shell->currentText(), transport.value("program").toString());
        auto* consoleModel = settings->pageModel("terminal.connection");
        QCOMPARE(consoleModel->value("runAsAdministrator").toBool(), false);
        consoleModel->setValue("runAsAdministrator", true);
        QVERIFY(consoleModel->isValid());
        consoleModel->setValue("runAsAdministrator", false);
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            _app.mainWindow().resize(1280, 800);
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("ConsoleTerminal.png")));
        }
        QVERIFY2(_app.plugins().setEnabled("ConsoleTerminal", false, &error), qPrintable(error));
        QVERIFY(_app.terminalIds().isEmpty());
        QVERIFY(!_app.commands().find("Console.New"));
    }
    Q_SLOT void serialPluginTerminalLifecycle()
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
        const auto    restoreClipboard = qScopeGuard([&] { clipboard->setMimeData(savedClipboard.release()); });
        const auto    editAction       = [this](const char* id) { return _app.commands().find(id)->actions().first(); };
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
            QCOMPARE(_app.toolbar().actions(it.value() == "create" ? "terminalCreation" : "terminal").contains(action),
                     it.key() != "Serial.Close");
        }
        QVERIFY(!_app.commands().find("Serial.New")->actions().first()->isEnabled());
        QVERIFY(_app.settings().setValue("settings/appearance/fontSize", 15));
        QVERIFY2(_app.createWorkspace(Path(temporary.filePath("serial.termaster")), "Serial test", &error),
                 qPrintable(error));
        QVERIFY(_app.workspace()->settings().setValue("appearance/fontSize", 17));
        QVERIFY(_app.workspace()->settings().setValue("input/pastePolicy", "immediate"));
        QVERIFY(_app.workspace()->settings().setValue("serial/baudRates", QVariantList {123456, 250000}));
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
        display->setFocus();
        QTRY_VERIFY(display->hasFocus());
        QTRY_VERIFY(editAction("Edit.SelectAll")->isEnabled());
        QVERIFY(!editAction("Edit.Copy")->isEnabled());
        QVERIFY(!editAction("Edit.Cut")->isEnabled());
        QVERIFY(!editAction("Edit.Undo")->isEnabled());
        QVERIFY(!editAction("Edit.Redo")->isEnabled());
        editAction("Edit.SelectAll")->trigger();
        QTRY_VERIFY(editAction("Edit.Copy")->isEnabled());
        QVERIFY(!editAction("Edit.Cut")->isEnabled());
        QVERIFY(!editAction("Edit.Undo")->isEnabled());
        QVERIFY(!editAction("Edit.Redo")->isEnabled());
        editAction("Edit.Copy")->trigger();
        QVERIFY(clipboard->text().contains("synthetic output"));
        clipboard->setText("synthetic paste");
        QTRY_VERIFY(editAction("Edit.Paste")->isEnabled());
        QSignalSpy terminalOutput(display.data(), &TerminalDisplay::outputBytes);
        const auto clipboardBeforeControl = clipboard->text();
        QTest::keyClick(display, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(terminalOutput.size(), 1);
        QCOMPARE(terminalOutput.first().at(0).toByteArray(), QByteArray(1, '\3'));
        QCOMPARE(clipboard->text(), clipboardBeforeControl);
        terminalOutput.clear();
        QTest::keyClick(display, Qt::Key_Tab);
        QCOMPARE(terminalOutput.size(), 1);
        QCOMPARE(terminalOutput.first().at(0).toByteArray(), QByteArray("\t"));
        QVERIFY(display->hasFocus());
        terminalOutput.clear();

        editAction("Edit.Paste")->trigger();
        QCOMPARE(terminalOutput.size(), 1);
        QCOMPARE(terminalOutput.first().at(0).toByteArray(), QByteArray("synthetic paste"));
        QCOMPARE(terminalOutput.first().at(1).value<TerminalDisplay::OutputKind>(), TerminalDisplay::OutputKind::Paste);
        QCOMPARE(device->state(), SerialDevice::State::Closed);
        clipboard->clear();
        QTRY_VERIFY(!editAction("Edit.Paste")->isEnabled());
        auto* workspaceFile = _app.mainWindow().findChild<QLineEdit*>("workspaceFile");
        QVERIFY(workspaceFile);
        workspaceFile->setFocus();
        workspaceFile->deselect();
        QTRY_VERIFY(!editAction("Edit.Copy")->isEnabled());
        QVERIFY(!editAction("Edit.Paste")->isEnabled());
        display->setFocus();
        QTRY_VERIFY(editAction("Edit.Copy")->isEnabled());
        _app.commands().find("Serial.Clear")->actions().first()->trigger();
        QVERIFY(!display->text().contains("synthetic output"));
        QTRY_VERIFY(!editAction("Edit.Copy")->isEnabled());
        QVERIFY(!editAction("Edit.SelectAll")->isEnabled());
        const auto definition = _app.session()->objects().value(id);
        QVERIFY(!definition.toMap().value("settings").toMap().contains("appearance"));
        _app.commands().find("Workspace.Settings")->actions().first()->trigger();
        auto* workspaceDialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(workspaceDialog);
        auto* workspaceModel = workspaceDialog->pageModel("workspace.serial.suggestions");
        QVERIFY(workspaceModel);
        workspaceModel->setValue("baudRates", QVariantList {123456, 123456});
        QVERIFY(!workspaceModel->isValid());
        workspaceModel->setValue("baudRates", QVariantList {123456, 250000, 654321});
        QVERIFY(workspaceModel->isValid());
        QVERIFY(workspaceModel->apply(&error));
        QVERIFY(_app.saveWorkspace(&error));
        QVERIFY(!workspaceDialog->pageModel("terminal.connection"));
        delete workspaceDialog;
        _app.commands().find("Application.Settings")->actions().first()->trigger();
        auto* applicationDialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(applicationDialog->pageModel("application.serial"));
        QVERIFY(!applicationDialog->pageModel("workspace.serial.suggestions"));
        QVERIFY(!applicationDialog->pageModel("terminal.connection"));
        auto* defaultPort = applicationDialog->findChild<QComboBox*>("serialPortCombo");
        QVERIFY(defaultPort && defaultPort->isEditable());
        defaultPort->setEditText("COM_GLOBAL_DRAFT");
        QCOMPARE(_app.settings().value("settings/transport/portName", QString()).toString(), QString());
        delete applicationDialog;
        _app.commands().find("Serial.Settings")->actions().first()->trigger();
        QPointer<SettingsDialog> settings(_app.mainWindow().findChild<SettingsDialog*>("settingsDialog"));
        QVERIFY(settings);
        QVERIFY(!settings->pageModel("workspace.serial"));
        QVERIFY(!settings->pageModel("application.appearance"));
        SettingsFormWidget* workspaceForm  = nullptr;
        SettingsFormWidget* connectionForm = nullptr;
        for (auto* form : settings->findChildren<SettingsFormWidget*>())
        {
            if (form->editor("baudRates"))
            {
                workspaceForm = form;
            }
            if (form->findChild<QComboBox*>("baudRateSelector"))
            {
                connectionForm = form;
            }
        }
        QVERIFY(!workspaceForm);
        QVERIFY(connectionForm);
        auto* port = connectionForm->findChild<QComboBox*>("serialPortCombo");
        QVERIFY(port && port->isEditable());
        QCOMPARE(port->currentText(), QString("COM_TERMASTER_TEST_UNUSED"));
        auto* terminalTree = settings->findChild<QTreeView*>("settingsTree");
        QCOMPARE(terminalTree->model()->rowCount(), 1);
        QCOMPARE(terminalTree->model()->index(0, 0).data().toString(), QString("Terminal"));
        auto* baud = connectionForm->editor("baudRate")->findChild<QComboBox*>("baudRateSelector");
        QVERIFY(baud);
        QVERIFY(baud->isEditable());
        QVERIFY(baud->findData(123456) >= 0);
        QVERIFY(baud->findData(250000) >= 0);
        QCOMPARE(baud->currentText(), "115200");
        baud->setEditText("234567");
        auto* connectionModel = settings->pageModel("terminal.connection");
        QVERIFY(connectionModel->isValid());
        QCOMPARE(connectionModel->value("baudRate").toInt(), 234567);
        QVERIFY(connectionModel->canReset("baudRate"));
        connectionModel->removeValue("baudRate");
        QCOMPARE(baud->currentText(), "115200");

        auto* tabs = _app.mainWindow().findChild<QTabWidget*>();
        QVERIFY(tabs);
        QVERIFY(tabs->tabBar()->tabButton(tabs->currentIndex(), QTabBar::LeftSide));
        QVERIFY(!tabs->tabBar()->tabButton(tabs->currentIndex(), QTabBar::LeftSide)->grab().isNull());
        QCOMPARE(tabs->tabText(tabs->currentIndex()), QString("Serial\nCOM_TERMASTER_TEST_UNUSED"));
        QVERIFY(settings->selectPage("terminal.connection"));
        if (QGuiApplication::platformName() == "windows")
        {
            QTRY_VERIFY(settings->screen()->availableGeometry().contains(settings->frameGeometry()));
            QWidget* lastField = nullptr;
            for (auto* form : settings->findChildren<SettingsFormWidget*>())
            {
                if (auto* editor = form->editor("rts"))
                {
                    lastField = editor;
                    break;
                }
            }
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

    Q_SLOT void bufferedCommandSubmission()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Input.termaster")), "Input fixture", &error));
        QVERIFY(_app.createSession("Input fixture", &error));
        const auto probe                         = std::make_shared<ReconnectDevice::Probe>();
        probe->available                         = true;
        auto&                    host            = _app.pluginHost();
        const auto               originalSize    = _app.mainWindow().size();
        const auto               originalMinimum = _app.mainWindow().minimumSize();
        const auto               cleanup         = qScopeGuard([&] {
            _app.closeWorkspace();
            host.unregisterTerminalType("inputFixture");
            _app.mainWindow().setMinimumSize(originalMinimum);
            _app.mainWindow().resize(originalSize);
        });
        PluginHost::TerminalType type;
        type.id             = "inputFixture";
        type.title          = "Command fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic command input fixture
content:
  type: object
  properties: {}
)";
        type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(std::move(type));
        QVariantMap input {
            {      "mode", "buffered"},
            {"terminator",      "nul"},
            { "localEcho",       true}
        };
        QVERIFY(_app.createTerminal("inputFixture",
                                    {
                                        {"input", input}
        },
                                    "Commands",
                                    &error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        auto* editor  = _app.mainWindow().findChild<CommandEditor*>("commandEditor");
        auto* draft   = editor->findChild<QPlainTextEdit*>("commandDraft");
        auto* display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(editor && draft && display);
        QVERIFY(editor->buffered());
        host.connectSelectedTerminal();
        QCOMPARE(probe->device->state(), SerialDevice::State::Open);
        display->setFocus();
        QTest::keyClicks(QApplication::focusWidget(), "AT");
        QCOMPARE(draft->toPlainText(), "AT");
        QCOMPARE(probe->output, QByteArray());
        QTest::keyClick(draft, Qt::Key_Return);
        QTRY_COMPARE(probe->output, QByteArray("AT\0", 3));
        QVERIFY(draft->toPlainText().isEmpty());
        auto* history = editor->findChild<QComboBox*>("commandHistory");
        QCOMPARE(history->count(), 1);
        probe->allowance = 0;
        draft->setPlainText("must not replay");
        QTest::keyClick(draft, Qt::Key_Return);
        QCoreApplication::processEvents();
        host.disconnectSelectedTerminal();
        host.connectSelectedTerminal();
        probe->allowance = 2;
        QTest::qWait(40);
        QCOMPARE(probe->output, QByteArray("AT\0", 3));
        const auto beforeReply = display->text();
        display->receive("\x1b[6n");
        QTRY_VERIFY(probe->output.mid(3).endsWith('R'));
        QVERIFY(probe->output.mid(3).startsWith("\x1b["));
        QCOMPARE(display->text(), beforeReply);
        QApplication::clipboard()->setText("PASTE");
        display->paste();
        QCOMPARE(draft->toPlainText(), "PASTE");
        const auto sentBeforePaste = probe->output;
        auto*      pastePolicy     = editor->findChild<QComboBox*>("commandPastePolicy");
        pastePolicy->setCurrentIndex(pastePolicy->findData("immediate"));
        QVERIFY(QMetaObject::invokeMethod(pastePolicy, "activated", Q_ARG(int, pastePolicy->currentIndex())));
        display->paste();
        QTRY_COMPARE(probe->output, sentBeforePaste + QByteArray("PASTE\0", 6));
        QCOMPARE(draft->toPlainText(), "PASTE");
        const auto capture = qEnvironmentVariable("TERMASTER_COMMAND_CAPTURE");
        if (!capture.isEmpty())
        {
            _app.mainWindow().setMinimumSize(1100, 760);
            _app.mainWindow().resize(1100, 760);
            display->clear();
            display->receive("Synthetic command fixture: exact byte output and bounded queued send.\r\n");
            draft->setPlainText("AT+GMR");
            draft->setFocus();
            QTest::qWait(100);
            QCOMPARE(_app.mainWindow().size(), QSize(1100, 760));
            QVERIFY(_app.mainWindow().grab().save(capture));
            auto* mode = editor->findChild<QComboBox*>("commandInputMode");
            mode->setCurrentIndex(mode->findData("interactive"));
            QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, mode->currentIndex())));
            QTest::qWait(30);
            const QFileInfo captureFile(capture);
            const auto      interactiveName = QString(captureFile.completeBaseName()).replace("Buffered", "Interactive")
                                            + ".png";
            QVERIFY(_app.mainWindow().grab().save(QDir(captureFile.absolutePath()).filePath(interactiveName)));
        }
    }

    Q_SLOT void bulkTerminalConnections()
    {
        auto* start = _app.commands().find("Terminal.StartAll")->actions().first();
        auto* stop  = _app.commands().find("Terminal.StopAll")->actions().first();
        QVERIFY(!start->isEnabled());
        QVERIFY(!stop->isEnabled());
        auto* menu = _app.mainMenu().findMenu("/terminal");
        QCOMPARE(menu->actions("allConnections"), QList<QAction*>({start, stop}));
        QVERIFY(!_app.toolbar().toolbar()->actions().contains(start));
        QVERIFY(!_app.toolbar().toolbar()->actions().contains(stop));

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Bulk.termaster")), "Bulk fixture", &error));
        QVERIFY(_app.createSession("Bulk fixture", &error));
        auto available       = std::make_shared<ReconnectDevice::Probe>();
        auto unavailable     = std::make_shared<ReconnectDevice::Probe>();
        available->available = true;
        auto&      host      = _app.pluginHost();
        const auto cleanup   = qScopeGuard([&] {
            _app.closeWorkspace();
            host.unregisterTerminalType("bulkAvailable");
            host.unregisterTerminalType("bulkUnavailable");
        });
        for (const bool working : {true, false})
        {
            PluginHost::TerminalType type;
            type.id             = working ? "bulkAvailable" : "bulkUnavailable";
            type.title          = type.id;
            type.plugin         = "Synthetic";
            type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic bulk fixture
content:
  type: object
  properties: {}
)";
            type.createDevice   = [probe = working ? available : unavailable] {
                return std::make_unique<ReconnectDevice>(probe);
            };
            host.registerTerminalType(std::move(type));
        }
        QVERIFY(_app.createTerminal("bulkAvailable", {}, "Available", &error));
        QVERIFY(_app.createTerminal("bulkUnavailable", {}, "Unavailable", &error));
        const auto selected = _app.selectedTerminal();
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.refreshTerminalActions();
        QVERIFY(start->isEnabled());
        QVERIFY(!stop->isEnabled());
        start->trigger();
        QCOMPARE(available->device->state(), SerialDevice::State::Open);
        QCOMPARE(unavailable->device->state(), SerialDevice::State::Failed);
        QCOMPARE(available->attempts, 1);
        QCOMPARE(unavailable->attempts, 1);
        QCOMPARE(_app.selectedTerminal(), selected);
        QVERIFY(host.connectionRequested("bulkUnavailable"));
        QVERIFY(!start->isEnabled());
        QVERIFY(stop->isEnabled());
        start->trigger();
        QCOMPARE(available->attempts, 1);
        stop->trigger();
        QCOMPARE(available->device->state(), SerialDevice::State::Closed);
        QCOMPARE(unavailable->device->state(), SerialDevice::State::Closed);
        QVERIFY(!host.connectionRequested("bulkUnavailable"));
        QCOMPARE(_app.selectedTerminal(), selected);
        QVERIFY(start->isEnabled());
        QVERIFY(!stop->isEnabled());
        QTest::qWait(1200);
        QCOMPARE(unavailable->attempts, 1);
        unavailable->available = true;
        start->trigger();
        QCOMPARE(available->attempts, 2);
        QCOMPARE(unavailable->attempts, 2);
        QCOMPARE(unavailable->device->state(), SerialDevice::State::Open);
        stop->trigger();

        // Close is still offered when a backend registers no presentation actions.
        auto*      tabs  = _app.mainWindow().findChild<QTabWidget*>("terminalTabs");
        const auto point = tabs->tabBar()->tabRect(0).center();
        QMetaObject::invokeMethod(tabs->tabBar(),
                                  "customContextMenuRequested",
                                  Qt::DirectConnection,
                                  Q_ARG(QPoint, point));
        auto* popup = _app.mainWindow().findChild<QMenu*>("terminalTabMenu");
        QVERIFY(popup);
        QVERIFY(!popup->findChild<QAction*>("Terminal.StartAll"));
        auto* close = popup->findChild<QAction*>("Terminal.Close");
        QVERIFY(close);
        close->trigger();
        QCOMPARE(tabs->count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(!start->isEnabled());
        QVERIFY(!stop->isEnabled());
    }

    Q_SLOT void desiredConnectionSurvivesDeviceLoss()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Retry.termaster")), "Retry fixture", &error));
        QVERIFY(_app.createSession("Retry fixture", &error));
        auto                     probe = std::make_shared<ReconnectDevice::Probe>();
        auto&                    host  = _app.pluginHost();
        PluginHost::TerminalType type;
        type.id             = "retryFixture";
        type.title          = "Retry fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic retry fixture
content:
  type: object
  properties:
    portName:
      type: string
      default: synthetic
      x-settings-path: transport/portName
)";
        type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(type);
        const auto cleanup = qScopeGuard([&] {
            _app.closeWorkspace();
            host.unregisterTerminalType("retryFixture");
        });
        QVERIFY(_app.createTerminal("retryFixture", {}, "Retry terminal", &error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        auto* tabs = _app.mainWindow().findChild<QTabWidget*>();
        QVERIFY(tabs);
        const auto tooltip = [tabs] {
            return QTextDocumentFragment::fromHtml(tabs->tabToolTip(tabs->currentIndex()))
                .toPlainText()
                .replace(QChar(0x00a0), ' ');
        };
        host.connectSelectedTerminal();
        QVERIFY(host.connectionRequested("retryFixture"));
        QCOMPARE(probe->device->state(), SerialDevice::State::Failed);
        QVERIFY(tooltip().contains("Desired: Connected"));
        QVERIFY(tooltip().contains("Synthetic port unavailable"));
        const auto hoverPoint = tabs->tabBar()->tabRect(tabs->currentIndex()).center();
        QTest::mouseMove(tabs->tabBar(), hoverPoint);
        QApplication::processEvents();
        QHelpEvent hover(QEvent::ToolTip, hoverPoint, tabs->tabBar()->mapToGlobal(hoverPoint));
        QCoreApplication::sendEvent(tabs->tabBar(), &hover);
        QTRY_VERIFY(QToolTip::isVisible());
        QCOMPARE(QToolTip::text(), tabs->tabToolTip(tabs->currentIndex()));
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            _app.mainWindow().resize(1280, 800);
            QApplication::processEvents();
            auto     image = _app.mainWindow().grab();
            QPainter painter(&image);
            for (auto* widget : QApplication::topLevelWidgets())
            {
                if (widget->isVisible() && widget->windowType() == Qt::ToolTip)
                {
                    const auto point = widget->mapToGlobal(QPoint()) - _app.mainWindow().mapToGlobal(QPoint());
                    painter.drawPixmap(point, widget->grab());
                }
            }
            painter.end();
            QVERIFY(image.save(QDir(capture).filePath("ConnectionRetry.png")));
        }
        QToolTip::hideText();
        const auto failedIcon = tabs->tabBar()->tabButton(tabs->currentIndex(), QTabBar::LeftSide)->grab().toImage();
        probe->available      = true;
        QTRY_COMPARE_WITH_TIMEOUT(probe->device->state(), SerialDevice::State::Open, 3500);
        QVERIFY(probe->attempts >= 2);
        QVERIFY(tooltip().contains("Actual: Connected"));
        QVERIFY(!tooltip().contains("Synthetic port unavailable"));
        QVERIFY(tabs->tabBar()->tabButton(tabs->currentIndex(), QTabBar::LeftSide)->grab().toImage() != failedIcon);
        auto* display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(display);
        display->receive("\033]2;Remote title\007");
        QVERIFY(tooltip().contains("Desired: Connected"));
        QVERIFY(tooltip().contains("Remote title"));

        probe->available = false;
        probe->device->drop("Removed <synthetic> port");
        QVERIFY(host.connectionRequested("retryFixture"));
        QVERIFY(tabs->tabToolTip(tabs->currentIndex()).contains("&lt;synthetic&gt;"));
        const int beforeRetry = probe->attempts;
        QTRY_VERIFY_WITH_TIMEOUT(probe->attempts > beforeRetry, 3500);
        host.disconnectSelectedTerminal();
        QVERIFY(!host.connectionRequested("retryFixture"));
        QCOMPARE(probe->device->state(), SerialDevice::State::Closed);
        const int stoppedAttempts = probe->attempts;
        probe->available          = true;
        QTest::qWait(1200);
        QCOMPARE(probe->attempts, stoppedAttempts);
        host.connectSelectedTerminal();
        QCOMPARE(probe->device->state(), SerialDevice::State::Open);
        host.connectSelectedTerminal();
        QCOMPARE(probe->attempts, stoppedAttempts + 1);

        probe->available = false;
        probe->device->drop("Removed synthetic port again");
        const int unloadAttempts = probe->attempts;
        QVERIFY(host.unregisterTerminalType("retryFixture", &error));
        QVERIFY(!probe->device);
        QTest::qWait(1200);
        QCOMPARE(probe->attempts, unloadAttempts);
        host.registerTerminalType(type);
        QVERIFY(probe->device);
        QCOMPARE(probe->device->state(), SerialDevice::State::Closed);
        QVERIFY(!host.connectionRequested("retryFixture"));
        host.connectSelectedTerminal();
        QVERIFY(host.connectionRequested("retryFixture"));
        const int closedAttempts = probe->attempts;
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(!probe->device);
        QTest::qWait(1200);
        QCOMPARE(probe->attempts, closedAttempts);
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
