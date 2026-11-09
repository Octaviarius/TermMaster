#include "SocketPlugin.h"

#include <QTest>
#include <QWidget>
#include <stdexcept>
#include <utility>

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
    int                          closed         = 0;
    int                          settings       = 0;
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
        ++closed;
    }
    void clearSelectedTerminal() override
    {
        ++cleared;
    }
    void showSelectedTerminalSettings() override
    {
        ++settings;
    }
    void refreshTerminalActions() override
    {
    }
};

class StateDevice final : public SerialDevice
{
public:
    State    current = State::Closed;
    QObject* object() override
    {
        return nullptr;
    }
    State state() const override
    {
        return current;
    }
    void setCallbacks(Callbacks) override
    {
    }
    bool open(const QVariantMap&, QString*) override
    {
        return false;
    }
    void close() override
    {
    }
    qint64 write(const QByteArray&, QString*) override
    {
        return -1;
    }
    qint64 pendingBytes() const override
    {
        return 0;
    }
};

class SocketPluginTests final : public QObject
{
    Q_OBJECT
private slots:
    void lifecycleRetainsFactoriesUntilTypeRemoval()
    {
        FakeHost     host;
        SocketPlugin plugin;
        plugin.setHost(&host);
        QString error;
        QVERIFY2(plugin.startup(&error), qPrintable(error));
        QCOMPARE(plugin.state(), Plugin::State::Running);
        QCOMPARE(host.type.id, "socket");
        QCOMPARE(host.type.plugin, "SocketTerminal");
        QCOMPARE(host.type.title, "Socket");
        QVERIFY(plugin.startup());
        QVERIFY(host.type.settingsSchema.contains("enum: [TCP, UDP]"));
        QVERIFY(host.type.defaultSettingsSchema.contains("minLength: 0"));
        QVERIFY(!host.type.createPanel);
        QCOMPARE(host.actions.size(), 6);
        QVERIFY(host.editors.isEmpty());
        auto device = host.type.createDevice();
        QCOMPARE(device->state(), SerialDevice::State::Closed);
        QVERIFY(device->autoReconnect());
        device.reset();
        host.removalAllowed = false;
        QVERIFY(!plugin.shutdown(&error));
        QCOMPARE(plugin.state(), Plugin::State::Failed);
        QCOMPARE(error, "Synthetic persistence failure");
        QCOMPARE(host.actions.size(), 6);
        QVERIFY(host.editors.isEmpty());
        QVERIFY(!plugin.startup());
        host.removalAllowed = true;
        host.events.clear();
        QVERIFY(plugin.shutdown());
        QCOMPARE(host.events.first(), "terminal");
        QCOMPARE(host.events.last(), "action");
        QVERIFY(host.actions.isEmpty());
        QVERIFY(host.editors.isEmpty());
        QVERIFY(plugin.startup());
        QVERIFY(plugin.shutdown());
    }

    void registrationFailureRollsBackOnlyOwnedContributions()
    {
        for (const auto& failure : {QString("type"), QString("Socket.Disconnect")})
        {
            FakeHost host;
            host.failRegistration = failure;
            PluginHost::Action existing;
            existing.id    = "Socket.Disconnect";
            existing.title = "Existing command";
            host.actions.insert(existing.id, existing);
            SocketPlugin plugin;
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

    void actionsRespectDeviceStates()
    {
        FakeHost     host;
        SocketPlugin plugin;
        plugin.setHost(&host);
        QVERIFY(plugin.startup());
        StateDevice device;
        host.selected = true;
        host.device   = &device;
        for (const auto state : {SerialDevice::State::Closed,
                                 SerialDevice::State::Opening,
                                 SerialDevice::State::Open,
                                 SerialDevice::State::Closing,
                                 SerialDevice::State::Failed})
        {
            device.current           = state;
            const bool canConnect    = state == SerialDevice::State::Closed || state == SerialDevice::State::Failed;
            const bool canDisconnect = state == SerialDevice::State::Open || state == SerialDevice::State::Opening;
            const auto connect       = host.actions.value("Socket.Connect");
            const auto disconnect    = host.actions.value("Socket.Disconnect");
            QCOMPARE(connect.enabled(), canConnect);
            QCOMPARE(disconnect.enabled(), canDisconnect);
            const int beforeStarted = host.started;
            const int beforeStopped = host.stopped;
            connect.trigger();
            disconnect.trigger();
            QCOMPARE(host.started, beforeStarted + int(canConnect));
            QCOMPARE(host.stopped, beforeStopped + int(canDisconnect));
        }
        host.device = nullptr;
        QVERIFY(!host.actions.value("Socket.Connect").enabled());
        QVERIFY(!host.actions.value("Socket.Disconnect").enabled());
        QVERIFY(plugin.shutdown());
    }

    void actionsRevalidateSelectionAndIntent()
    {
        FakeHost     host;
        SocketPlugin plugin;
        plugin.setHost(&host);
        QVERIFY(plugin.startup());
        const auto create = host.actions.value("Socket.New");
        QVERIFY(create.visible());
        QVERIFY(!create.enabled());
        for (auto it = host.actions.cbegin(); it != host.actions.cend(); ++it)
        {
            QVERIFY(!it->iconName.isEmpty());
            QVERIFY(!it->group.isEmpty());
            if (it.key() != "Socket.New")
            {
                QVERIFY(!it->visible());
                QVERIFY(!it->enabled());
                it->trigger();
            }
        }
        QCOMPARE(host.started, 0);
        QCOMPARE(host.stopped, 0);
        QCOMPARE(host.closed, 0);
        QCOMPARE(host.cleared, 0);
        QCOMPARE(host.settings, 0);
        create.trigger();
        QCOMPARE(host.created, 0);
        host.canCreate = true;
        create.trigger();
        QCOMPARE(host.created, 1);
        auto device   = host.type.createDevice();
        host.device   = device.get();
        host.selected = true;
        QVERIFY(host.actions.value("Socket.Connect").enabled());
        QVERIFY(!host.actions.value("Socket.Disconnect").enabled());
        host.actions.value("Socket.Connect").trigger();
        QCOMPARE(host.started, 1);
        host.actions.value("Socket.Close").trigger();
        host.actions.value("Socket.Clear").trigger();
        host.actions.value("Socket.Settings").trigger();
        QCOMPARE(host.closed, 1);
        QCOMPARE(host.cleared, 1);
        QCOMPARE(host.settings, 1);
        host.wanted = true;
        QVERIFY(!host.actions.value("Socket.Connect").enabled());
        host.actions.value("Socket.Connect").trigger();
        QCOMPARE(host.started, 1);
        QVERIFY(host.actions.value("Socket.Disconnect").enabled());
        host.actions.value("Socket.Disconnect").trigger();
        QCOMPARE(host.stopped, 1);
        host.selected = false;
        host.actions.value("Socket.Disconnect").trigger();
        QCOMPARE(host.stopped, 1);
        for (const auto& action : std::as_const(host.actions))
        {
            if (action.id != "Socket.New")
            {
                action.trigger();
            }
        }
        QCOMPARE(host.started, 1);
        QCOMPARE(host.stopped, 1);
        QCOMPARE(host.closed, 1);
        QCOMPARE(host.cleared, 1);
        QCOMPARE(host.settings, 1);
        device.reset();
        host.device      = nullptr;
        const auto stale = host.actions.value("Socket.Clear");
        QVERIFY(plugin.shutdown());
        host.selected = true;
        QVERIFY(!stale.enabled());
        stale.trigger();
        QCOMPARE(host.cleared, 1);
    }
};

QTEST_MAIN(SocketPluginTests)
#include "SocketPluginTests.moc"
