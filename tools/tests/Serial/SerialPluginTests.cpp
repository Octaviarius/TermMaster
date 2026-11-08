#include "SerialByteDevice.h"
#include "SerialPlugin.h"

#include <QLabel>
#include <QTest>
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
    bool                         canCreate      = false;
    int                          created        = 0;
    int                          cleared        = 0;
    QObject*                     context() override
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
        return nullptr;
    }
    bool canCreateTerminal() const override
    {
        return canCreate;
    }
    bool hasSelectedTerminal(const QString&) const override
    {
        return selected;
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
        ++cleared;
    }
    void showSelectedTerminalSettings() override
    {
    }
    void refreshTerminalActions() override
    {
    }
};
class SerialPluginTests final : public QObject
{
    Q_OBJECT
private slots:
    void lifecycleAndRetainedRegistrations()
    {
        FakeHost     host;
        SerialPlugin plugin;
        plugin.setHost(&host);
        QVERIFY(plugin.startup());
        QCOMPARE(plugin.state(), Plugin::State::Running);
        QCOMPARE(host.type.id, "serial");
        QCOMPARE(host.type.plugin, "SerialTerminal");
        QVERIFY(host.type.settingsSchema.contains("x-widget: serialPortSelector"));
        QCOMPARE(host.actions.size(), 6);
        QCOMPARE(host.editors.size(), 1);
        auto device = host.type.createDevice();
        QCOMPARE(device->state(), SerialDevice::State::Closed);
        device.reset();
        auto*                    display = new QWidget;
        std::unique_ptr<QWidget> panel(
            host.type.createPanel(display,
                                  {
                                      {"transport", QVariantMap {{"portName", "fixture-port"}, {"baudRate", 9600}}}
        },
                                  nullptr));
        QCOMPARE(display->parentWidget(), panel.get());
        QCOMPARE(panel->findChild<QLabel*>()->text(), "fixture-port / 9600 baud");
        panel.reset();
        host.removalAllowed = false;
        QString error;
        QVERIFY(!plugin.shutdown(&error));
        QCOMPARE(plugin.state(), Plugin::State::Failed);
        QCOMPARE(error, "Synthetic persistence failure");
        QCOMPARE(host.actions.size(), 6);
        QCOMPARE(host.editors.size(), 1);
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
    void registrationFailuresReleaseOnlyOwnedEntries()
    {
        for (const auto& failure : {QString("editor"), QString("type"), QString("Serial.Disconnect")})
        {
            FakeHost host;
            host.failRegistration = failure;
            PluginHost::Action existing;
            existing.id    = "Serial.Disconnect";
            existing.title = "Existing command";
            host.actions.insert(existing.id, existing);
            SerialPlugin plugin;
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
    void actionsResolveCurrentTarget()
    {
        FakeHost     host;
        SerialPlugin plugin;
        plugin.setHost(&host);
        QVERIFY(plugin.startup());
        const auto create = host.actions.value("Serial.New");
        const auto clear  = host.actions.value("Serial.Clear");
        QVERIFY(create.visible());
        QVERIFY(!create.enabled());
        create.trigger();
        QCOMPARE(host.created, 0);
        host.canCreate = true;
        QVERIFY(create.enabled());
        create.trigger();
        QCOMPARE(host.created, 1);
        QVERIFY(!clear.visible());
        clear.trigger();
        QCOMPARE(host.cleared, 0);
        host.selected = true;
        QVERIFY(clear.visible());
        clear.trigger();
        QCOMPARE(host.cleared, 1);
        host.selected = false;
        clear.trigger();
        QCOMPARE(host.cleared, 1);
        QVERIFY(plugin.shutdown());
    }
};
QTEST_MAIN(SerialPluginTests)
#include "SerialPluginTests.moc"
