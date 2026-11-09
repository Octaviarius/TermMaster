#include "SerialByteDevice.h"
#include "SerialPlugin.h"

#include <QComboBox>
#include <QLineEdit>
#include <QSerialPortInfo>
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
    SerialDevice*                device         = nullptr;
    bool                         wanted         = false;
    int                          stopped        = 0;
    int                          started        = 0;
    bool                         canCreate      = false;
    int                          created        = 0;
    int                          cleared        = 0;
    QVariantMap                  configuration;
    QVariantMap                  workspaceSettings() const override
    {
        return configuration;
    }
    void showMessage(const QString&) override
    {
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
        events.append("connect");
    }
    void disconnectSelectedTerminal() override
    {
        ++stopped;
        events.append("disconnect");
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

class SerialPluginTests final : public QObject
{
    Q_OBJECT
private slots:
    void reconnectRevalidatesStateAndDispatchesInOrder()
    {
        FakeHost     host;
        SerialPlugin plugin;
        plugin.setHost(&host);
        QVERIFY(plugin.startup());
        StateDevice device;
        host.device          = &device;
        host.selected        = true;
        const auto reconnect = host.actions.value("Serial.Reconnect");
        QCOMPARE(reconnect.role, PluginHost::Action::Role::Restart);
        for (const bool wanted : {false, true})
        {
            host.wanted = wanted;
            for (const auto state : {SerialDevice::State::Closed,
                                     SerialDevice::State::Opening,
                                     SerialDevice::State::Open,
                                     SerialDevice::State::Closing,
                                     SerialDevice::State::Failed})
            {
                device.current     = state;
                const bool allowed = state != SerialDevice::State::Closing
                                  && (wanted || state == SerialDevice::State::Open
                                      || state == SerialDevice::State::Opening);
                QCOMPARE(reconnect.enabled(), allowed);
                host.events.clear();
                reconnect.trigger();
                QCOMPARE(host.events, allowed ? QStringList({"disconnect", "connect"}) : QStringList());
            }
        }
        device.current = SerialDevice::State::Open;
        QVERIFY(reconnect.enabled());
        host.selected = false;
        QVERIFY(!reconnect.enabled());
        host.events.clear();
        reconnect.trigger();
        QVERIFY(host.events.isEmpty());
        host.selected = true;
        host.device   = nullptr;
        QVERIFY(!reconnect.enabled());
        reconnect.trigger();
        QVERIFY(host.events.isEmpty());
        QVERIFY(plugin.shutdown());
        host.device = &device;
        host.events.clear();
        QVERIFY(!reconnect.enabled());
        reconnect.trigger();
        QVERIFY(host.events.isEmpty());
    }
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
        QCOMPARE(host.actions.size(), 7);
        QCOMPARE(host.editors.size(), 2);
        auto device = host.type.createDevice();
        QCOMPARE(device->state(), SerialDevice::State::Closed);
        device.reset();
        QVERIFY(!host.type.createPanel);
        host.removalAllowed = false;
        QString error;
        QVERIFY(!plugin.shutdown(&error));
        QCOMPARE(plugin.state(), Plugin::State::Failed);
        QCOMPARE(error, "Synthetic persistence failure");
        QCOMPARE(host.actions.size(), 7);
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
    void baudRateSuggestionsAndManualInput()
    {
        FakeHost host;
        host.configuration = {
            {"serial", QVariantMap {{"baudRates", QVariantList {115200, 123456, 123456, 250000}}}}
        };
        SerialPlugin plugin;
        plugin.setHost(&host);
        QVERIFY(plugin.startup());
        const auto factory = host.editors.value("serialBaudRateSelector");
        auto       editor  = factory({}, nullptr);
        auto*      combo   = editor->widget()->findChild<QComboBox*>("baudRateSelector");
        QVERIFY(combo);
        QVERIFY(combo->isEditable());
        QList<int> rates;
        for (int i = 0; i < combo->count(); ++i)
        {
            rates.append(combo->itemData(i).toInt());
        }
        for (const auto rate : QSerialPortInfo::standardBaudRates())
        {
            QVERIFY(rates.contains(rate));
        }
        QCOMPARE(rates.count(123456), 1);
        QCOMPARE(rates.count(115200), 1);
        QVERIFY(rates.contains(250000));
        int changes = 0;
        editor->setChangedCallback([&] { ++changes; });
        editor->setValue(115200);
        QCOMPARE(changes, 0);
        QCOMPARE(editor->value().toInt(), 115200);
        combo->setCurrentIndex(combo->findData(123456));
        QCOMPARE(editor->value().toInt(), 123456);
        QVERIFY(changes > 0);
        combo->setEditText("234567");
        QCOMPARE(editor->value().toInt(), 234567);
        QVERIFY(editor->inputError().isEmpty());
        for (const auto& text :
             {QString(), QString("0"), QString("-1"), QString("2147483648"), QString("1.5"), QString("abc")})
        {
            combo->setEditText(text);
            QVERIFY(!editor->inputError().isEmpty());
        }
        editor->setValue(765432);
        QCOMPARE(combo->currentText(), "765432");
        QCOMPARE(editor->value().toInt(), 765432);
        QVERIFY(editor->inputError().isEmpty());
        host.configuration = {
            {"serial", QVariantMap {{"baudRates", QVariantList {654321}}}}
        };
        auto  next      = factory({}, nullptr);
        auto* nextCombo = next->widget()->findChild<QComboBox*>();
        QVERIFY(nextCombo->findData(654321) >= 0);
        QCOMPARE(nextCombo->findData(123456), -1);
        editor.reset();
        next.reset();
        QVERIFY(plugin.shutdown());
    }
    void desiredConnectionKeepsStopAvailable()
    {
        FakeHost     host;
        SerialPlugin plugin;
        plugin.setHost(&host);
        QVERIFY(plugin.startup());
        auto device   = host.type.createDevice();
        host.device   = device.get();
        host.selected = true;
        QVERIFY(host.actions.value("Serial.Connect").enabled());
        host.wanted = true;
        QVERIFY(!host.actions.value("Serial.Connect").enabled());
        QVERIFY(host.actions.value("Serial.Disconnect").enabled());
        host.actions.value("Serial.Disconnect").trigger();
        QCOMPARE(host.stopped, 1);
        device.reset();
        host.device = nullptr;
        QVERIFY(plugin.shutdown());
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
