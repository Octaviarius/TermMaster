#include "ConsolePlugin.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPainter>
#include <QTest>
#include <QVBoxLayout>
#include <QWidget>
#include <stdexcept>

class FakeHost final : public QObject, public PluginHost
{
public:
    QMap<QString, Action>        actions;
    QMap<QString, EditorFactory> editors;
    TerminalType                 type;
    QStringList                  events;
    QString                      failRegistration;
    bool                         removalAllowed = true;
    bool                         selected       = false;
    SerialDevice*                device         = nullptr;
    bool                         wanted         = false;
    int                          stopped        = 0;
    bool                         canCreate      = false;
    int                          created        = 0;
    int                          cleared        = 0;
    QVariantMap                  configuration;
    int                          started = 0;
    QStringList                  messages;
    QVariantMap                  workspaceSettings() const override
    {
        return configuration;
    }
    void showMessage(const QString& message) override
    {
        messages.append(message);
    }
    QObject* context() override
    {
        return this;
    }
    void registerTerminalType(TerminalType value) override
    {
        if (failRegistration == "type")
        {
            throw std::runtime_error("Synthetic type collision");
        }
        type = std::move(value);
    }
    bool unregisterTerminalType(const QString&, QString* error) override
    {
        events.append("terminal");
        if (!removalAllowed)
        {
            if (error)
            {
                *error = "Synthetic persistence failure";
            }
            return false;
        }
        type = {};
        return true;
    }
    void registerAction(Action value) override
    {
        if (actions.contains(value.id) || failRegistration == value.id)
        {
            throw std::runtime_error("Synthetic action collision");
        }
        actions.insert(value.id, std::move(value));
    }
    void unregisterAction(const QString& id) override
    {
        events.append("action");
        actions.remove(id);
    }
    void registerEditor(const QString& id, EditorFactory value) override
    {
        if (editors.contains(id) || failRegistration == "editor")
        {
            throw std::runtime_error("Synthetic editor collision");
        }
        editors.insert(id, std::move(value));
    }
    void unregisterEditor(const QString& id) override
    {
        events.append("editor");
        editors.remove(id);
    }
    void showCreateTerminal(const QString&) override
    {
        ++created;
    }
    SerialDevice* selectedDevice(const QString&) override
    {
        return device;
    }
    bool canCreateTerminal() const override
    {
        return canCreate;
    }
    bool connectionRequested(const QString&) const override
    {
        return wanted;
    }
    bool hasSelectedTerminal(const QString&) const override
    {
        return selected;
    }
    void connectSelectedTerminal() override
    {
        ++started;
    }
    void disconnectSelectedTerminal() override
    {
        ++stopped;
    }
    void closeSelectedTerminal() override
    {
    }
    void clearSelectedTerminal() override
    {
        ++cleared;
    }
    void showSelectedTerminalSettings() override
    {
    }
    void refreshTerminalActions() override
    {
    }
};

class ConsolePluginTests final : public QObject
{
    Q_OBJECT
private slots:
    void lifecycleRetainsFactoriesUntilTypeRemoval()
    {
        FakeHost      host;
        ConsolePlugin plugin;
        plugin.setHost(&host);
        QString error;
        QVERIFY2(plugin.startup(&error), qPrintable(error));
        QCOMPARE(plugin.state(), Plugin::State::Running);
        QCOMPARE(host.type.id, "console");
        QCOMPARE(host.type.plugin, "ConsoleTerminal");
        QVERIFY(host.type.settingsSchema.contains("x-widget: consoleEnvironment"));
        QCOMPARE(host.actions.size(), 9);
        QCOMPARE(host.editors.size(), 2);
        auto device = host.type.createDevice();
        QCOMPARE(device->state(), SerialDevice::State::Closed);
        QVERIFY(!device->autoReconnect());
        device.reset();
        host.removalAllowed = false;
        QVERIFY(!plugin.shutdown(&error));
        QCOMPARE(plugin.state(), Plugin::State::Failed);
        QCOMPARE(error, "Synthetic persistence failure");
        QCOMPARE(host.actions.size(), 9);
        QCOMPARE(host.editors.size(), 2);
        QVERIFY(!plugin.startup());
        host.removalAllowed = true;
        host.events.clear();
        QVERIFY(plugin.shutdown());
        QCOMPARE(host.events.first(), "terminal");
        QCOMPARE(host.events.last(), "editor");
        QVERIFY(host.actions.isEmpty());
        QVERIFY(host.editors.isEmpty());
        QVERIFY(plugin.startup());
        QVERIFY(plugin.shutdown());
    }

    void registrationFailureRollsBackOnlyOwnedContributions()
    {
        for (const auto& failure : {QString("editor"), QString("type"), QString("Console.Stop")})
        {
            FakeHost host;
            host.failRegistration = failure;
            PluginHost::Action existing;
            existing.id    = "Console.Stop";
            existing.title = "Existing command";
            host.actions.insert(existing.id, existing);
            ConsolePlugin plugin;
            plugin.setHost(&host);
            QString error;
            QVERIFY(!plugin.startup(&error));
            QVERIFY(!error.isEmpty());
            QVERIFY(host.editors.isEmpty());
            QVERIFY(host.type.id.isEmpty());
            QCOMPARE(host.actions.size(), 1);
            QCOMPARE(host.actions.value(existing.id).title, "Existing command");
            QVERIFY(plugin.shutdown());
            QCOMPARE(host.actions.size(), 1);
        }
    }

    void refreshModesPreserveDraftsAndRestartOnlyCurrentTarget()
    {
        FakeHost      host;
        ConsolePlugin plugin;
        plugin.setHost(&host);
        QVERIFY(plugin.startup());
        auto editor = host.editors.value("consoleEnvironment")({}, nullptr);
        QVERIFY(editor != nullptr);
        const QVariantMap overrides {
            {"TERMASTER_REFRESH_FIXTURE", "draft-value"},
            {"TERMASTER_REMOVED_FIXTURE",    QVariant()}
        };
        editor->setValue(overrides);
        int changes = 0;
        editor->setChangedCallback([&] { ++changes; });
        const auto refresh = host.actions.value("Console.Environment.Refresh");
        const auto restart = host.actions.value("Console.Environment.RefreshRestart");
        QVERIFY(refresh.enabled());
        QVERIFY(refresh.visible());
        refresh.trigger();
        QCOMPARE(editor->value().toMap(), overrides);
        QCOMPARE(changes, 0);
        QCOMPARE(host.started, 0);
        QCOMPARE(host.stopped, 0);
        QVERIFY(!host.messages.isEmpty());
        QVERIFY(!restart.enabled());
        restart.trigger();
        QCOMPARE(host.started, 0);
        auto device   = host.type.createDevice();
        host.device   = device.get();
        host.selected = true;
        QVERIFY(restart.enabled());
        restart.trigger();
        QCOMPARE(host.started, 1);
        QCOMPARE(host.stopped, 1);
        QCOMPARE(editor->value().toMap(), overrides);
        QCOMPARE(changes, 0);
        host.selected = false;
        restart.trigger();
        QCOMPARE(host.started, 1);
        QCOMPARE(host.stopped, 1);
        // Expired editor pointers must be ignored on a later refresh.
        editor.reset();
        refresh.trigger();
        device.reset();
        host.device = nullptr;
        QVERIFY(plugin.shutdown());
    }

    void shellSuggestionsAndManualValues()
    {
        FakeHost      host;
        ConsolePlugin plugin;
        plugin.setHost(&host);
        QVERIFY(plugin.startup());
        QVERIFY(host.type.settingsSchema.contains("x-widget: consoleShellSelector"));
        QVERIFY(host.type.defaultSettingsSchema.contains("x-widget: consoleShellSelector"));
        QWidget panel;
        auto*   layout = new QVBoxLayout(&panel);
        auto*   group  = new QGroupBox("Launch", &panel);
        auto*   form   = new QFormLayout(group);
        form->setLabelAlignment(Qt::AlignRight);
        auto editor = host.editors.value("consoleShellSelector")({}, group);
        form->addRow("Shell executable", editor->widget());
        layout->addWidget(group);
        layout->addStretch();
        auto* combo = editor->widget()->findChild<QComboBox*>("consoleShellCombo");
        QVERIFY(combo && combo->isEditable());
#ifdef Q_OS_WIN
        QVERIFY(combo->findText("pwsh.exe") >= 0);
        QVERIFY(combo->findText("cmd.exe") >= 0);
#else
        QVERIFY(combo->findText("bash") >= 0);
        QVERIFY(combo->findText("ash") >= 0);
#endif
        int changes = 0;
        editor->setChangedCallback([&] { ++changes; });
        editor->setValue(QString());
        QCOMPARE(editor->value().toString(), QString());
        QVERIFY(!combo->lineEdit()->placeholderText().isEmpty());
        QCOMPARE(changes, 0);
        const QString custom = "F:/Custom tools/My shell.exe";
        editor->setValue(custom);
        QCOMPARE(combo->currentText(), custom);
        QCOMPARE(editor->value().toString(), custom);
        QCOMPARE(changes, 0);
        combo->setEditText("custom-shell-not-installed");
        QCOMPARE(editor->value().toString(), "custom-shell-not-installed");
        QVERIFY(changes > 0);
        QVERIFY(editor->inputError().isEmpty());
        const int count = combo->count();
        combo->setCurrentIndex(0);
        QCOMPARE(editor->value().toString(), combo->itemText(0));
        QCOMPARE(combo->count(), count);
        editor->setValue(42);
        QVERIFY(!editor->inputError().isEmpty());
        QCOMPARE(editor->value(), QVariant(42));
        editor->setValue(QString(32768, 'x'));
        QVERIFY(!editor->inputError().isEmpty());
        QCOMPARE(editor->value().toString().size(), 32768);
        editor->setValue(QString());
        QVERIFY(editor->inputError().isEmpty());
        const auto capture = qEnvironmentVariable("TERMASTER_SHELL_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            panel.resize(1280, 800);
            panel.show();
            QTest::qWait(100);
            combo->showPopup();
            QTest::qWait(100);
            auto     image = panel.grab();
            QPainter painter(&image);
            auto*    popup = combo->view()->window();
            painter.drawPixmap(popup->mapToGlobal(QPoint()) - panel.mapToGlobal(QPoint()), popup->grab());
            painter.end();
            QVERIFY(image.save(QDir(capture).filePath("ShellSelectorWidget.png")));
            combo->hidePopup();
        }
        editor.reset();
        QVERIFY(plugin.shutdown());
    }

    void actionsRevalidateSelectionAndIntent()
    {
        FakeHost      host;
        ConsolePlugin plugin;
        plugin.setHost(&host);
        QVERIFY(plugin.startup());
        const auto create = host.actions.value("Console.New");
        create.trigger();
        QCOMPARE(host.created, 0);
        host.canCreate = true;
        create.trigger();
        QCOMPARE(host.created, 1);
        auto device   = host.type.createDevice();
        host.device   = device.get();
        host.selected = true;
        QVERIFY(host.actions.value("Console.Start").enabled());
        host.wanted = true;
        QVERIFY(!host.actions.value("Console.Start").enabled());
        QVERIFY(host.actions.value("Console.Stop").enabled());
        host.actions.value("Console.Stop").trigger();
        QCOMPARE(host.stopped, 1);
        host.selected = false;
        host.actions.value("Console.Stop").trigger();
        QCOMPARE(host.stopped, 1);
        device.reset();
        host.device = nullptr;
        QVERIFY(plugin.shutdown());
    }
};

QTEST_MAIN(ConsolePluginTests)
#include "ConsolePluginTests.moc"
