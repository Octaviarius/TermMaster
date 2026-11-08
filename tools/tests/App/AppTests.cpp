#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <Termaster/App/App.h>
#include <Termaster/Core/ApplicationPaths.h>
#include <Termaster/Core/CommandRegistry.h>
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
        QVERIFY(_app.mainMenu().findMenu("/file"));
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
        QCOMPARE(_app.toolbar().toolbar()->actions().size(), 1);
        QVERIFY(!QFileInfo::exists(_app.paths().configFile("plugins").toString()));
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
        action->trigger();
        QCOMPARE(_app.mainWindow().findChildren<QDialog*>().size(), 1);

        const QString capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("MainWindow.png")));
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
