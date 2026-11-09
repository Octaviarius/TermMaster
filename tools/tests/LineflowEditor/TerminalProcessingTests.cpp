#include "Gui/Processing/ProcessorCatalogTree.h"
#include "Gui/Processing/ProcessorFavorites.h"
#include "Gui/Processing/ProcessorSelector.h"
#include "Gui/Terminal/TerminalProcessingPanel.h"
#include "Terminal/ProcessingRouter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>

namespace
{
void select(QComboBox* combo, const QString& id)
{
    const auto row = combo->findData(id);
    QVERIFY(row >= 0);
    combo->setCurrentIndex(row);
    emit combo->activated(row);
}
QVariantMap block()
{
    return {
        {    "id",                                                           "test.protocol"                  },
        {  "name",                                                                           "Device protocol"},
        {  "kind",                                                                                  "lineflow"},
        { "group",                                                                                 "Protocols"},
        {"source",
         QVariantMap {{"nodes",
         QVariantList {QVariantMap {{"name", "Traffic gate"}, {"type", "native.gate"}},
         QVariantMap {{"name", "Hex encoding"}, {"type", "native.hex-encoder"}}}}}                            }
    };
}
} // namespace
class TerminalProcessingTests : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void selectionsAndPrivateProperties()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        auto    definitions = ProcessorRegistry::parseDefinitions(
            {
                {"test.protocol", block()}
        },
            &error);
        QVERIFY2(definitions.has_value(), qPrintable(error));
        QVERIFY2(registry.publishDefinitions("workspace", *definitions, &error), qPrintable(error));
        SettingsWidgetRegistry  editors;
        ProcessingRouter        router;
        QVariantMap             applied;
        bool                    allow = false;
        int                     calls = 0;
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      {},
                                      [&](const QVariantMap& baseline, const QVariantMap& draft, QString* error) {
                                          ++calls;
                                          if (!baseline.isEmpty())
                                          {
                                              *error = "Unexpected baseline";
                                              return false;
                                          }
                                          if (!allow)
                                          {
                                              *error = "Synthetic persistence failure";
                                              return false;
                                          }
                                          applied = draft;
                                          return true;
                                      });
        auto*                   rx = panel.findChild<QComboBox*>("rxProcessor");
        auto*                   tx = panel.findChild<QComboBox*>("txProcessor");
        QVERIFY(rx && tx);
        QCOMPARE(rx->currentData().toString(), QString("native.bypass"));
        select(rx, "test.protocol");
        auto* table = panel.propertiesWidget()->findChild<QTableWidget*>("processingPropertyTable");
        QVERIFY(table);
        QCOMPARE(table->item(0, 0)->text(), QString("Traffic gate"));
        auto* gate = panel.propertiesWidget()->findChild<SettingsFieldEditor*>("Traffic gate/enabled");
        QVERIFY(gate);
        gate->setValue(false);
        emit gate->valueChanged();
        QVERIFY(!panel.applyDraft(&error));
        QCOMPARE(error, QString("Synthetic persistence failure"));
        QCOMPARE(calls, 1);
        QVERIFY(applied.isEmpty());
        allow = true;
        QVERIFY(panel.applyDraft(&error));
        QCOMPARE(applied.value("rx").toMap().value("overrides").toMap().value("Traffic gate").toMap().value("enabled"),
                 QVariant(false));
        QVERIFY(!applied.contains("tx"));
        QCOMPARE(registry.type("native.gate")->defaults.value("enabled"), QVariant(true));
        QCOMPARE(tx->currentData().toString(), QString("native.bypass"));
    }
    Q_SLOT void resetAndDirectionDrafts()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        SettingsWidgetRegistry editors;
        ProcessingRouter       router;
        const QVariantMap      initial {
            {"rx", QVariantMap {{"type", "native.gate"}, {"overrides", QVariantMap {{"enabled", false}}}}}
        };
        QVariantMap             applied;
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      initial,
                                      [&](const QVariantMap&, const QVariantMap& value, QString*) {
                                          applied = value;
                                          return true;
                                      });
        auto*                   reset = panel.propertiesWidget()->findChild<QToolButton*>("reset_/enabled");
        QVERIFY(reset);
        reset->click();
        QVERIFY(reset->isHidden());
        auto* tx = panel.findChild<QComboBox*>("txProcessor");
        select(tx, "native.hex-encoder");
        QVERIFY(panel.applyDraft());
        QVERIFY(!applied.value("rx").toMap().contains("overrides"));
        QCOMPARE(applied.value("tx").toMap().value("type").toString(), QString("native.hex-encoder"));
    }
    Q_SLOT void unavailableSelectionIsRetained()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry editors;
        ProcessingRouter       router;
        const QVariantMap      initial {
            {"rx", QVariantMap {{"type", "missing.plugin"}}}
        };
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      initial,
                                      [](const QVariantMap&, const QVariantMap&, QString*) { return false; });
        QCOMPARE(panel.findChild<QComboBox*>("rxProcessor")->currentData().toString(), QString("missing.plugin"));
        QVERIFY(!panel.propertiesWidget()->findChild<QLabel*>("processingError")->text().isEmpty());
    }
    Q_SLOT void reselectAndExternalRevertPreserveScope()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        SettingsWidgetRegistry editors;
        ProcessingRouter       router;
        const QVariantMap      initial {
            {"rx",
             QVariantMap {{"type", "native.gate"},
                          {"overrides", QVariantMap {{"enabled", false}}},
                          {"custom", "retained"}}}
        };
        int                     calls = 0;
        QVariantMap             latest;
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      initial,
                                      [&](const QVariantMap& baseline, const QVariantMap&, QString*) {
                                          ++calls;
                                          latest = baseline;
                                          return true;
                                      });
        select(panel.findChild<QComboBox*>("rxProcessor"), "native.gate");
        QVERIFY(panel.applyDraft());
        QCOMPARE(calls, 0);
        auto* editor = panel.propertiesWidget()->findChild<SettingsFieldEditor*>("/enabled");
        QVERIFY(editor);
        editor->setValue(true);
        emit              editor->valueChanged();
        const QVariantMap external {
            {"rx", QVariantMap {{"type", "native.hex-encoder"}}}
        };
        panel.setConfiguration(external);
        editor->setValue(false);
        emit    editor->valueChanged();
        QString error;
        QVERIFY(!panel.applyDraft(&error));
        QVERIFY(!error.isEmpty());
        auto* revert = panel.propertiesWidget()->findChild<QPushButton*>("revertProcessing");
        QVERIFY(revert);
        revert->click();
        QCOMPARE(panel.findChild<QComboBox*>("rxProcessor")->currentData().toString(), QString("native.hex-encoder"));
        select(panel.findChild<QComboBox*>("txProcessor"), "native.gate");
        QVERIFY(panel.applyDraft());
        QCOMPARE(calls, 1);
        QCOMPARE(latest, external);
    }

    Q_SLOT void editorsAreReleasedSynchronously()
    {
        ProcessorRegistry      registry;
        ProcessorCatalog::Type type;
        type.id    = "custom";
        type.title = "Custom";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Custom\ncontent:\n  type: object\n  properties:\n    value:\n      type: boolean\n      default: true\n      x-widget: custom-widget\n";
        type.create = [](const QVariantMap&, const Processor::Limits&, QString*) {
            return std::unique_ptr<Processor> {};
        };
        registry.registerType("test", std::move(type));
        SettingsWidgetRegistry editors;
        editors.registerWidget("custom-widget", [](const SettingsSchema::Field&, QWidget* parent) {
            class Editor final : public SettingsFieldEditor
            {
            public:
                using SettingsFieldEditor::SettingsFieldEditor;
                QVariant value() const override
                {
                    return _value;
                }
                void setValue(const QVariant& value) override
                {
                    _value = value;
                }

            private:
                QVariant _value;
            };
            return new Editor(parent);
        });
        ProcessingRouter              router;
        TerminalProcessingPanel       panel(registry,
                                            editors,
                                            router,
                                            {
                                                {"rx", QVariantMap {{"type", "custom"}}}
        },
                                            [](const QVariantMap&, const QVariantMap&, QString*) { return true; });
        QPointer<SettingsFieldEditor> editor = panel.propertiesWidget()->findChild<SettingsFieldEditor*>("/value");
        QVERIFY(editor);
        panel.releaseEditors();
        QVERIFY(editor.isNull());
        QVERIFY(editors.unregisterWidget("custom-widget"));
    }

    Q_SLOT void applyMayCloseTerminal()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        SettingsWidgetRegistry   editors;
        ProcessingRouter         router;
        TerminalProcessingPanel* panel = nullptr;
        panel = new TerminalProcessingPanel(registry,
                                            editors,
                                            router,
                                            {},
                                            [&](const QVariantMap&, const QVariantMap&, QString*) {
                                                delete panel;
                                                panel = nullptr;
                                                return true;
                                            });
        select(panel->findChild<QComboBox*>("rxProcessor"), "native.gate");
        QString error;
        QVERIFY(!panel->applyDraft(&error));
        QVERIFY(!panel);
    }

    Q_SLOT void processorPopupTree()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        auto    definitions = ProcessorRegistry::parseDefinitions(
            {
                {"test.protocol", block()}
        },
            &error);
        QVERIFY(registry.publishDefinitions("workspace", *definitions, &error));
        ProcessorSelector selector;
        selector.resize(440, 28);
        selector.setCatalog(registry.types(), "native.hex-encoder");
        selector.show();
        selector.showPopup();
        auto* tree = selector.findChild<QTreeWidget*>("processorSelectorTree");
        QVERIFY(tree && tree->isVisible());
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("Bypass"));
        QVERIFY(tree->topLevelItem(1)->data(0, ProcessorCatalogTree::SeparatorRole).toBool());
        QCOMPARE(tree->currentItem()->data(0, Qt::UserRole).toString(), QString("native.hex-encoder"));
        QCOMPARE(tree->currentItem()->parent()->text(0), QString("Codecs"));
        QCOMPARE(tree->currentItem()->parent()->parent()->text(0), QString("Native"));
        auto* workspace = tree->topLevelItem(3);
        QCOMPARE(workspace->text(0), QString("Workspace"));
        QVERIFY(!(workspace->flags() & Qt::ItemIsSelectable));
        QSignalSpy activated(&selector, &QComboBox::activated);
        tree->setCurrentItem(workspace);
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(activated.count(), 0);
        QVERIFY(tree->isVisible());
        auto* search = selector.findChild<QLineEdit*>("processorSelectorSearch");
        search->setText("Device protocol");
        auto* leaf = tree->findItems("Workspace", Qt::MatchExactly).first();
        QVERIFY(!leaf->isHidden());
        leaf = leaf->child(0)->child(0);
        tree->setCurrentItem(leaf);
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(selector.currentData().toString(), QString("test.protocol"));
        QCOMPARE(selector.currentText(), QString("Device protocol"));
        QCOMPARE(activated.count(), 1);
        QVERIFY(!tree->isVisible());
        selector.showPopup();
        auto* bypass = tree->topLevelItem(0);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualItemRect(bypass).center());
        QCOMPARE(selector.currentData().toString(), QString("native.bypass"));
        QCOMPARE(activated.count(), 2);
        selector.showPopup();
        QTest::keyClick(tree, Qt::Key_Escape);
        QCOMPARE(activated.count(), 2);
        QVERIFY(!tree->isVisible());
        selector.setCatalog({}, "missing");
        selector.showPopup();
        QCOMPARE(tree->currentItem()->data(0, Qt::UserRole).toString(), QString("missing"));
        selector.hidePopup();
    }

    Q_SLOT void sharedFavoritesRefreshAllCatalogs()
    {
        Settings           settings(QString {});
        ProcessorFavorites favorites(settings.node("processing"));
        ProcessorRegistry  registry;
        registerNativeProcessors(registry, "native");
        ProcessorSelector receive(nullptr, &favorites);
        ProcessorSelector transmit(nullptr, &favorites);
        receive.setCatalog(registry.types(), "native.gate");
        transmit.setCatalog(registry.types(), "native.stub");
        auto* rxTree = receive.findChild<ProcessorCatalogTree*>();
        auto* txTree = transmit.findChild<ProcessorCatalogTree*>();
        QVERIFY(rxTree && txTree);
        QCOMPARE(rxTree->topLevelItem(0)->data(0, Qt::UserRole).toString(), QString("native.bypass"));
        QVERIFY(favorites.setFavorite("native.stub", true));
        QCOMPARE(rxTree->topLevelItem(1)->data(0, Qt::UserRole).toString(), QString("native.stub"));
        QCOMPARE(txTree->topLevelItem(1)->data(0, Qt::UserRole).toString(), QString("native.stub"));
        QCOMPARE(receive.currentData().toString(), QString("native.gate"));
        QCOMPARE(transmit.currentData().toString(), QString("native.stub"));
        QVERIFY(favorites.move("native.stub", -1));
        QCOMPARE(rxTree->topLevelItem(0)->data(0, Qt::UserRole).toString(), QString("native.stub"));
        QVERIFY(favorites.setIds({}));
        QCOMPARE(rxTree->topLevelItem(0)->text(0), QString("Native"));
        QCOMPARE(settings.value("processing/favorites").toList().size(), 0);
        receive.showPopup();
        auto* gate = rxTree->findItems("Gate", Qt::MatchExactly | Qt::MatchRecursive).first();
        rxTree->scrollToItem(gate);
        QTimer::singleShot(0, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            QVERIFY(menu);
            QTest::mouseClick(menu,
                              Qt::LeftButton,
                              Qt::NoModifier,
                              menu->actionGeometry(menu->actions().first()).center());
        });
        emit rxTree->customContextMenuRequested(rxTree->visualItemRect(gate).center());
        QVERIFY(favorites.contains("native.gate"));
        QCOMPARE(txTree->topLevelItem(0)->data(0, Qt::UserRole).toString(), QString("native.gate"));
        receive.hidePopup();
        QVERIFY(favorites.setIds({"native.bypass", "missing.processor"}));
        receive.showPopup();
        rxTree->setCurrentItem(rxTree->topLevelItem(1));
        QTest::keyClick(rxTree, Qt::Key_Return);
        QCOMPARE(receive.currentData().toString(), QString("missing.processor"));
    }

    Q_SLOT void popupVisualCapture()
    {
        const auto destination = qEnvironmentVariable("TERMASTER_SELECTOR_CAPTURE");
        if (destination.isEmpty())
        {
            QSKIP("Opt-in native capture");
        }
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        auto    definitions = ProcessorRegistry::parseDefinitions(
            {
                {"test.protocol", block()}
        },
            &error);
        QVERIFY(registry.publishDefinitions("workspace", *definitions, &error));
        QWidget window;
        window.setFixedSize(620, 460);
        auto* layout   = new QVBoxLayout(&window);
        auto* selector = new ProcessorSelector(&window);
        selector->setCatalog(registry.types(), "test.protocol");
        layout->addWidget(selector);
        layout->addStretch();
        window.show();
        selector->showPopup();
        auto* tree = selector->findChild<QTreeWidget*>("processorSelectorTree");
        tree->expandAll();
        QTest::qWait(80);
        QVERIFY(QDir().mkpath(destination));
        auto* popup = selector->findChild<QFrame*>("processorSelectorPopup");
        QVERIFY(popup);
        QVERIFY(popup->grab().save(QDir(destination).filePath("ProcessorSelector.png")));
        selector->hidePopup();
    }

    Q_SLOT void visualCapture()
    {
        const auto destination = qEnvironmentVariable("TERMASTER_PROCESSING_CAPTURE");
        if (destination.isEmpty())
        {
            QSKIP("Opt-in native capture");
        }
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        auto    definitions = ProcessorRegistry::parseDefinitions(
            {
                {"test.protocol", block()}
        },
            &error);
        QVERIFY(registry.publishDefinitions("workspace", *definitions, &error));
        SettingsWidgetRegistry editors;
        ProcessingRouter       router;
        const auto             receive = registry.prepare("test.protocol", {}, {}, &error);
        QVERIFY2(receive, qPrintable(error));
        auto rxInstance = receive->instantiate(
            {
                {"Hex encoding", QVariantMap {{"letterCase", "lower"}}}
        },
            &error);
        QVERIFY2(rxInstance, qPrintable(error));
        router.replace(ProcessingRouter::Direction::Rx, std::move(rxInstance));
        auto txInstance = registry.create("native.gate", {}, {}, &error);
        QVERIFY2(txInstance, qPrintable(error));
        router.replace(ProcessingRouter::Direction::Tx, std::move(txInstance));
        QWidget window;
        window.setFixedSize(1100, 650);
        auto* layout   = new QVBoxLayout(&window);
        auto* split    = new QSplitter(&window);
        auto* terminal = new TerminalDisplay(split);
        terminal->setAppearance({
            {"fontFamily", "Consolas"},
            {  "fontSize",         12},
            {"foreground",  "#e5e7eb"},
            {"background",  "#15191f"}
        });
        auto* panel = new TerminalProcessingPanel(
            registry,
            editors,
            router,
            {
                {"rx",
                 QVariantMap {{"type", "test.protocol"},
                 {"overrides", QVariantMap {{"Hex encoding", QVariantMap {{"letterCase", "lower"}}}}}}             },
                {"tx",                                                        QVariantMap {{"type", "native.gate"}}}
        },
            [](const QVariantMap&, const QVariantMap&, QString*) { return true; },
            &window);
        split->addWidget(panel->propertiesWidget());
        split->setSizes({650, 430});
        layout->addWidget(split, 1);
        layout->addWidget(panel);
        panel->findChild<QToolButton*>("processingPropertiesToggle")->setChecked(true);
        window.show();
        terminal->receive("\x1b[1;36mSynthetic processing preview\x1b[0m\r\n\r\n"
                          "\x1b[32mReady\x1b[0m  Receive: Device protocol\r\n"
                          "       Transmit: Gate\r\n\r\n"
                          "RX  41 54 0D 0A\r\nTX  AT\\r\\n\r\n");
        QTest::qWait(80);
        auto* caseEditor = panel->propertiesWidget()->findChild<SettingsFieldEditor*>("Hex encoding/letterCase");
        QVERIFY(caseEditor);
        auto* combo = caseEditor->findChild<QComboBox*>();
        auto* reset = panel->propertiesWidget()->findChild<QToolButton*>("reset_Hex encoding/letterCase");
        QVERIFY(combo && reset);
        QTRY_COMPARE(reset->height(), combo->height());
        QVERIFY(QDir().mkpath(destination));
        QVERIFY(window.grab().save(QDir(destination).filePath("TerminalProcessing.png")));
        bool rxCancelled = false;
        bool txCancelled = false;
        router.cancel(ProcessingRouter::Direction::Rx, [&] { rxCancelled = true; });
        router.cancel(ProcessingRouter::Direction::Tx, [&] { txCancelled = true; });
        QTRY_VERIFY(rxCancelled && txCancelled);
    }
};
QTEST_MAIN(TerminalProcessingTests)
#include "TerminalProcessingTests.moc"
