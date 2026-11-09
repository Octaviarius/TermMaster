#include "Gui/Settings/SettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <Termaster/Core/Gui/Settings/SettingsFormModel.h>
#include <Termaster/Core/Gui/Settings/SettingsFormWidget.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/SettingsSchema.h>

class SocketSettingsTests final : public QObject
{
    Q_OBJECT
private:
    static std::shared_ptr<SettingsSchema> _schema(const QString& name)
    {
        QFile file(":/SocketTerminal/" + name + ".yaml");
        if (!file.open(QIODevice::ReadOnly))
        {
            return {};
        }
        return std::make_shared<SettingsSchema>(file.readAll());
    }
    Q_SLOT void validationAndScopedPersistence()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Settings base(directory.filePath("Base.yaml"));
        Settings target(directory.filePath("Terminal.yaml"));
        QVERIFY(base.setValue("transport/host", "127.0.0.1"));
        QVERIFY(base.setValue("transport/port", 5000));
        QVERIFY(target.setValue("unrelated/keep", "preserved"));
        const auto schema = _schema("Settings");
        QVERIFY(schema);
        SettingsFormModel model(schema, SettingsOverlay(base.node(""), target.node("")));
        QVERIFY(model.isValid());
        QCOMPARE(model.value("host").toString(), "127.0.0.1");
        QVERIFY(!model.hasOverride("host"));
        model.setValue("host", "::1");
        model.setValue("protocol", "UDP");
        model.setValue("bindAddress", "::1");
        model.setValue("bindPort", 20000);
        QString error;
        QVERIFY2(model.apply(&error), qPrintable(error));
        QCOMPARE(target.value("transport/host").toString(), "::1");
        QCOMPARE(base.value("transport/host").toString(), "127.0.0.1");
        QCOMPARE(target.value("unrelated/keep").toString(), "preserved");
        model.setValue("port", 65536);
        QVERIFY(!model.isValid());
        QVERIFY(!model.apply(&error));
        QVERIFY(!target.contains("transport/port"));
        model.setValue("port", 5000);
        model.setValue("protocol", "unknown");
        QVERIFY(!model.isValid());
        model.setValue("protocol", "TCP");
        model.setValue("host", "");
        QVERIFY(!model.isValid());
        Settings          defaults(directory.filePath("Defaults.yaml"));
        SettingsFormModel defaultModel(_schema("Defaults"), defaults.node(""));
        QVERIFY(defaultModel.isValid());
    }
    Q_SLOT void presentationAndSearch()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Settings settings(directory.filePath("Settings.yaml"));
        QVERIFY(settings.setValue("transport/host", "terminal.example.test"));
        SettingsWidgetRegistry registry;
        SettingsDialog         dialog(registry, [](QString*) { return true; });
        dialog.addPage("terminal.connection",
                       "Terminal",
                       "Connection",
                       "TCP or UDP connection settings for this terminal.",
                       _schema("Settings"),
                       settings.node(""));
        QVERIFY(dialog.selectPage("terminal.connection"));
        dialog.show();
        if (!qEnvironmentVariable("TERMASTER_SOCKET_GUI_CAPTURE").isEmpty())
        {
            dialog.resize(1280, 800);
        }
        QTest::qWait(100);
        if (qEnvironmentVariable("TERMASTER_SOCKET_GUI_CAPTURE").isEmpty())
        {
            QVERIFY(dialog.width() <= dialog.screen()->availableGeometry().width());
            QVERIFY(dialog.height() <= dialog.screen()->availableGeometry().height());
        }
        const auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(buttons);
        QVERIFY(dialog.rect().contains(buttons->geometry()));
        auto* model = dialog.pageModel("terminal.connection");
        QVERIFY(model && model->isValid());
        auto* form = dialog.findChild<SettingsFormWidget*>();
        QVERIFY(form);
        QVERIFY(form->editor("protocol"));
        QVERIFY(form->editor("bindAddress"));
        auto* combo = form->editor("protocol")->findChild<QComboBox*>();
        if (!combo)
        {
            combo = qobject_cast<QComboBox*>(form->editor("protocol"));
        }
        QVERIFY(combo);
        QCOMPARE(combo->count(), 2);
        model->setValue("protocol", "UDP");
        QCOMPARE(model->value("protocol").toString(), "UDP");
        auto* search = dialog.findChild<QLineEdit*>("settingsSearch");
        QVERIFY(search);
        search->setText("Local binding");
        QTRY_VERIFY(form->visibleFields().contains("bindAddress"));
        QVERIFY(!form->visibleFields().contains("host"));
        search->clear();
        QTRY_VERIFY(form->visibleFields().contains("host"));
        model->setValue("protocol", "TCP");
        const auto capture = qEnvironmentVariable("TERMASTER_SOCKET_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QApplication::processEvents();
            QVERIFY(dialog.grab().save(QDir(capture).filePath("SocketSettings.png")));
        }
    }
};
QTEST_MAIN(SocketSettingsTests)
#include "SocketSettingsTests.moc"
