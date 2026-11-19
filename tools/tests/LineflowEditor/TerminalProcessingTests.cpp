#include "Gui/Processing/CodeflowEditorDialog.h"
#include "Gui/Processing/LineflowEditorDialog.h"
#include "Gui/Processing/ProcessorCatalogTree.h"
#include "Gui/Processing/ProcessorFavorites.h"
#include "Gui/Processing/ProcessorSelector.h"
#include "Gui/Terminal/TerminalProcessingPanel.h"
#include "Processing/TerminalProcessing.h"
#include "Terminal/ProcessingRouter.h"
#include "Terminal/TerminalLineRecorder.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/Processing/Codeflow.h>
#include <Termaster/Core/Processing/LineflowEndpoint.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>

namespace
{
void select(QComboBox* combo, const QString& id)
{
    QVERIFY(combo);
    const auto row = combo->findData(id);
    QVERIFY(row >= 0);
    combo->setCurrentIndex(row);
    emit combo->activated(row);
}
QVariantMap block(int channels = 1)
{
    return {
        {    "id",                 "test.protocol"                  },
        {  "name",                                 "Device protocol"},
        {  "kind",                                        "lineflow"},
        { "group",                                       "Protocols"},
        {"source",
         QVariantMap {{"channels", channels},
         {"nodes",
         QVariantList {QVariantMap {{"name", "Traffic gate"}, {"type", "native.gate"}, {"lane", "rx"}},
         QVariantMap {{"name", "Hex encoding"},
         {"type", "native.hex-encoder"},
         {"lane", "rx"}}}}}                                         }
    };
}
bool publish(ProcessorRegistry& registry, QString* error)
{
    const auto definitions = ProcessorRegistry::parseDefinitions(
        {
            {"test.protocol", block(2)}
    },
        error);
    return definitions && registry.publishDefinitions("workspace", *definitions, error);
}
QVariantMap configuration(bool enabled = true)
{
    return {
        {    "mode","lineflow"       },
        {"lineflow",
         QVariantMap {{"type", "test.protocol"},
         {"overrides",
         QVariantMap {{"Traffic gate", QVariantMap {{"enabled", enabled}, {"unknown", 17}}}}},
         {"custom", "retained"}}}
    };
}
} // namespace

class TerminalProcessingTests : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void directInlinePythonEditsTerminalDraftWithoutPublishing()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        if (!registry.type("native.duplex-python-code"))
        {
            registry.registerType("native", Codeflow::inlineType(2));
        }
        SettingsWidgetRegistry editors;
        ProcessingRouter       router;
        QVariantMap            applied;
        const QVariantMap      configuration {
            {    "mode",                                                   "lineflow"                        },
            {"lineflow",
             QVariantMap {{"type", "native.duplex-python-code"}, {"overrides", QVariantMap {{"future", 73}}}}}
        };
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      configuration,
                                      [&](const QVariantMap&, const QVariantMap& candidate, QString*) {
                                          applied = candidate;
                                          return true;
                                      });
        QTemporaryDir           directory;
        QVERIFY(directory.isValid());
        panel.setWorkspaceDirectory(directory.path());
        panel.show();
        panel.findChild<QToolButton*>("processingPropertiesToggle")->setChecked(true);
        auto* edit = panel.propertiesWidget()->findChild<QPushButton*>("editPythonCode_");
        QVERIFY(edit);
        edit->click();
        auto* dialog = panel.findChild<CodeflowEditorDialog*>("terminalInlinePythonEditor");
        QVERIFY(dialog);
        QVERIFY(dialog->findChild<QLineEdit*>("codeflowName")->isReadOnly());
        QVERIFY(!dialog->findChild<QLineEdit*>("codeflowFile")->isVisible());
        QVERIFY(applied.isEmpty());
        if (!Codeflow::defaultPython().isEmpty() && QFileInfo::exists(Codeflow::hostScript()))
        {
            auto* source = dialog->findChild<QPlainTextEdit*>("codeflowSource");
            QVERIFY(source);
            source->appendPlainText("# terminal occurrence draft");
            QAction* apply = nullptr;
            for (auto* action : dialog->findChildren<QAction*>())
            {
                if (action->text() == "Apply")
                {
                    apply = action;
                }
            }
            QVERIFY(apply);
            apply->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(dialog->findChild<QLabel*>("codeflowStatus")->text(),
                                      QString("Applied to diagram draft"),
                                      6000);
            QVERIFY(applied.isEmpty());
            QString error;
            QVERIFY2(panel.applyDraft(&error), qPrintable(error));
            const auto overrides = applied.value("lineflow").toMap().value("overrides").toMap();
            QCOMPARE(overrides.value("future").toInt(), 73);
            QVERIFY(overrides.value("source").toMap().value("code").toString().contains("terminal occurrence draft"));
            QVERIFY(!overrides.value("source").toMap().contains("file"));
        }
        panel.releaseEditors();
        QVERIFY(!panel.findChild<CodeflowEditorDialog*>());
    }

    Q_SLOT void lineRecorderDoesNotReplayHistoryOrCreateAFileForAnUnflushedScreen()
    {
        QTemporaryDir   temporary;
        TerminalDisplay display;
        display.resize(300, 100);
        display.show();
        QCoreApplication::processEvents();
        display.receive(QByteArray("old history\r\n").repeated(display.terminalSize().height() + 2));
        auto configuration = LineflowEndpoint::defaults(2);
        configuration.insert("enabled", true);
        configuration.insert("recordingMode", "lines");
        configuration.insert("direction", "rx");
        configuration.insert("flushScreen", false);
        const auto file = temporary.filePath("lines.txt");
        configuration.insert("file", file);
        TerminalLineRecorder recorder(display, configuration);
        display.receive("active");
        QString error;
        QVERIFY2(recorder.stop(&error), qPrintable(error));
        QVERIFY(!QFile::exists(file));
    }

    Q_SLOT void lineRecorderDropsIncompleteSoftWrapWithoutScreenFlush()
    {
        QTemporaryDir   temporary;
        TerminalDisplay display;
        display.resize(300, 100);
        display.show();
        QCoreApplication::processEvents();
        auto configuration = LineflowEndpoint::defaults(2);
        configuration.insert("enabled", true);
        configuration.insert("recordingMode", "lines");
        configuration.insert("direction", "rx");
        configuration.insert("flushScreen", false);
        const auto file = temporary.filePath("lines.txt");
        configuration.insert("file", file);
        TerminalLineRecorder recorder(display, configuration);
        const auto           size = display.terminalSize();
        display.receive(QByteArray(size.width() * (size.height() + 1) + 3, 'x'));
        QVERIFY(display.historyLineCount() > 0);
        QString error;
        QVERIFY2(recorder.stop(&error), qPrintable(error));
        QVERIFY(!QFile::exists(file));
    }

    Q_SLOT void lineRecorderFlushesAlternateSnapshotAndPreservesUntrimmedSpacing()
    {
        QTemporaryDir   temporary;
        TerminalDisplay display;
        display.resize(300, 100);
        display.show();
        QCoreApplication::processEvents();
        auto configuration = LineflowEndpoint::defaults(2);
        configuration.insert("enabled", true);
        configuration.insert("recordingMode", "lines");
        configuration.insert("direction", "rx");
        configuration.insert("lineEnding", "crlf");
        configuration.insert("trimTrailingSpaces", false);
        const auto file = temporary.filePath("lines-{direction}.txt");
        configuration.insert("file", file);
        TerminalLineRecorder recorder(display, configuration);
        display.receive("primary\x1b[?1049h\x1b[31mALT\x1b[0m");
        const auto expected = QByteArray("ALT") + QByteArray(display.terminalSize().width() - 3, ' ') + "\r\n";
        QString    error;
        QVERIFY2(recorder.stop(&error), qPrintable(error));
        QFile output(temporary.filePath("lines-rx.txt"));
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), expected);
        output.close();
        // Stopping must release the writer lease before another recording begins.
        configuration.insert("append", false);
        TerminalLineRecorder second(display, configuration);
        QVERIFY2(second.stop(&error), qPrintable(error));
    }

    Q_SLOT void renderedLogicalLineLimitFailsWithoutTruncatingOrCreatingAFile()
    {
        QTemporaryDir   temporary;
        TerminalDisplay display;
        display.setAppearance({
            {"scrollbackLines", 1}
        });
        display.resize(200, 80);
        display.show();
        QCoreApplication::processEvents();
        auto configuration = LineflowEndpoint::defaults(2);
        configuration.insert("enabled", true);
        configuration.insert("recordingMode", "lines");
        configuration.insert("direction", "rx");
        configuration.insert("flushScreen", false);
        const auto file = temporary.filePath("lines.txt");
        configuration.insert("file", file);
        TerminalLineRecorder recorder(display, configuration);
        QSignalSpy           failed(&recorder, &TerminalLineRecorder::error);
        display.receive(QByteArray(1024 * 1024 + 4096, 'x'));
        QTRY_COMPARE(failed.count(), 1);
        QString error;
        QVERIFY(!recorder.stop(&error));
        QVERIFY(error.contains("1 MiB"));
        QVERIFY(!QFile::exists(file));
    }

    Q_SLOT void validatesCanonicalModeAndComparesOnlyActiveLineflow()
    {
        QString error;
        QVERIFY(TerminalProcessing::validateMode({}, &error));
        QVERIFY(TerminalProcessing::validateMode(configuration(), &error));
        QVERIFY(!TerminalProcessing::validateMode(
            {
                {"mode", "invalid"}
        },
            &error));
        QVERIFY(!TerminalProcessing::validateMode(
            {
                {"mode", 1}
        },
            &error));
        auto first  = configuration();
        auto second = configuration(false);
        QVERIFY(!TerminalProcessing::sameActive(first, second));
        first.insert("mode", "bypass");
        second.insert("mode", "bypass");
        QVERIFY(TerminalProcessing::sameActive(first, second));
        const auto root = TerminalProcessing::rootId();
        QCOMPARE(root, QString("terminal.inplace.lineflow"));
        QCOMPARE(TerminalProcessing::diagram({}).value(root).toMap().value("source").toMap().value("channels"), 2);
    }

    Q_SLOT void selectorFiltersShapesAndRetainsDormantSelection()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        QVERIFY2(publish(registry, &error), qPrintable(error));
        SettingsWidgetRegistry  editors;
        ProcessingRouter        router;
        QVariantMap             applied;
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      {},
                                      [&](const QVariantMap&, const QVariantMap& draft, QString*) {
                                          applied = draft;
                                          return true;
                                      });
        panel.show();
        auto* mode     = panel.findChild<QComboBox*>("processingMode");
        auto* selector = panel.findChild<QComboBox*>("lineflowProcessor");
        QVERIFY(mode && selector);
        QCOMPARE(mode->count(), 2);
        QVERIFY(!panel.findChild<QComboBox*>("processingPropertyDirection"));
        QVERIFY(!panel.findChild<QComboBox*>("rxProcessor"));
        QVERIFY(!panel.findChild<QComboBox*>("txProcessor"));
        QCOMPARE(mode->currentData().toString(), QString("bypass"));
        QVERIFY(!selector->isVisible());
        QCOMPARE(selector->findData("native.hex-encoder"), -1);
        QVERIFY(selector->findData("test.protocol") >= 0);
        select(mode, "lineflow");
        QCOMPARE(applied.value("lineflow").toMap().value("type").toString(), QString("@inplace"));
        QVERIFY(applied.value("inplace").toMap().value("lineflow").toMap().contains(TerminalProcessing::rootId()));
        select(selector, "test.protocol");
        const auto selected = applied.value("lineflow");
        select(mode, "bypass");
        QCOMPARE(applied.value("lineflow"), selected);
        select(mode, "lineflow");
        QCOMPARE(applied.value("lineflow"), selected);
        QCOMPARE(selector->findData("native.bypass"), -1);
    }

    Q_SLOT void persistenceFailureAndExternalChangesRequireRevert()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        QVERIFY2(publish(registry, &error), qPrintable(error));
        SettingsWidgetRegistry  editors;
        ProcessingRouter        router;
        auto                    initial = configuration();
        QVariantMap             applied;
        bool                    accept = false;
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      initial,
                                      [&](const QVariantMap&, const QVariantMap& draft, QString* error) {
                                          if (!accept)
                                          {
                                              *error = "Synthetic persistence failure";
                                              return false;
                                          }
                                          applied = draft;
                                          return true;
                                      });
        auto*                   mode = panel.findChild<QComboBox*>("processingMode");
        select(mode, "bypass");
        QVERIFY(applied.isEmpty());
        QVERIFY(panel.propertiesWidget()->findChild<QLabel*>("processingError")->text().contains("Synthetic"));
        panel.propertiesWidget()->findChild<QPushButton*>("revertProcessing")->click();
        QCOMPARE(mode->currentData().toString(), QString("lineflow"));
        panel.propertiesWidget()->show();
        auto* editor = panel.propertiesWidget()->findChild<SettingsFieldEditor*>("Traffic gate/enabled");
        QVERIFY(editor);
        editor->setValue(false);
        emit editor->valueChanged();
        auto external = initial;
        external.insert("external", true);
        panel.setConfiguration(external);
        QVERIFY(!panel.applyDraft(&error));
        QVERIFY(error.contains("changed"));
        panel.propertiesWidget()->findChild<QPushButton*>("revertProcessing")->click();
        accept = true;
        select(mode, "bypass");
        QCOMPARE(applied.value("external"), true);
        QCOMPARE(applied.value("lineflow"), initial.value("lineflow"));
    }

    Q_SLOT void hiddenPropertiesUseLatestConfigurationAndRetainUnknownOverrides()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        QVERIFY2(publish(registry, &error), qPrintable(error));
        SettingsWidgetRegistry  editors;
        ProcessingRouter        router;
        QVariantMap             applied;
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      configuration(),
                                      [&](const QVariantMap&, const QVariantMap& draft, QString*) {
                                          applied = draft;
                                          return true;
                                      });
        auto*                   properties = panel.propertiesWidget();
        QVERIFY(properties->findChildren<SettingsFieldEditor*>().isEmpty());
        panel.setConfiguration(configuration(false));
        properties->show();
        auto* editor = properties->findChild<SettingsFieldEditor*>("Traffic gate/enabled");
        QVERIFY(editor);
        QCOMPARE(editor->value(), false);
        editor->setValue(true);
        emit editor->valueChanged();
        QVERIFY2(panel.applyDraft(&error), qPrintable(error));
        QCOMPARE(applied.value("lineflow")
                     .toMap()
                     .value("overrides")
                     .toMap()
                     .value("Traffic gate")
                     .toMap()
                     .value("unknown"),
                 17);
        QCOMPARE(applied.value("lineflow").toMap().value("custom").toString(), QString("retained"));
        panel.releaseEditors();
        QVERIFY(properties->findChildren<SettingsFieldEditor*>().isEmpty());
    }

    Q_SLOT void resetRemovesOnlyFieldOverrideAndReselectKeepsSelectionMetadata()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        QString error;
        QVERIFY2(publish(registry, &error), qPrintable(error));
        SettingsWidgetRegistry  editors;
        ProcessingRouter        router;
        QVariantMap             applied;
        int                     calls = 0;
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      configuration(false),
                                      [&](const QVariantMap&, const QVariantMap& draft, QString*) {
                                          ++calls;
                                          applied = draft;
                                          return true;
                                      });
        select(panel.findChild<QComboBox*>("lineflowProcessor"), "test.protocol");
        QCOMPARE(calls, 0);
        panel.propertiesWidget()->show();
        auto* reset = panel.propertiesWidget()->findChild<QToolButton*>("reset_Traffic gate/enabled");
        QVERIFY(reset);
        reset->click();
        QVERIFY(panel.applyDraft(&error));
        QCOMPARE(calls, 1);
        const auto selection = applied.value("lineflow").toMap();
        QCOMPARE(selection.value("custom").toString(), QString("retained"));
        const auto gate = selection.value("overrides").toMap().value("Traffic gate").toMap();
        QVERIFY(!gate.contains("enabled"));
        QCOMPARE(gate.value("unknown"), 17);
    }

    Q_SLOT void inplaceEditorKeepsTwoChannelsAndSavesLocally()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        SettingsWidgetRegistry  editors;
        ProcessingRouter        router;
        QVariantMap             applied;
        bool                    accept = true;
        int                     calls  = 0;
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      {},
                                      [&](const QVariantMap&, const QVariantMap& draft, QString* error) {
                                          ++calls;
                                          if (!accept)
                                          {
                                              *error = "Synthetic persistence failure";
                                              return false;
                                          }
                                          applied = draft;
                                          return true;
                                      });
        select(panel.findChild<QComboBox*>("processingMode"), "lineflow");
        const auto before      = applied;
        const auto beforeCalls = calls;
        auto*      button      = panel.findChild<QToolButton*>("lineflowInplace");
        QVERIFY(button);
        button->click();
        auto* editor = panel.findChild<LineflowEditorDialog*>("inplaceLineflowEditor");
        QVERIFY(editor);
        const auto root = TerminalProcessing::rootId();
        QCOMPARE(editor->model().selectedBlock(), root);
        QCOMPARE(editor->model().blockChannels(root), 2);
        QString error;
        QVERIFY(!editor->model().insertNode(root, 0, "native.gate", &error).isEmpty());
        editor->reject();
        QCOMPARE(applied, before);
        QCOMPARE(calls, beforeCalls);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        button->click();
        editor = panel.findChild<LineflowEditorDialog*>("inplaceLineflowEditor");
        QVERIFY(editor);
        QCOMPARE(editor->model().rowCount(), 0);
        QVERIFY(!editor->model().insertNode(root, 0, "native.gate", &error).isEmpty());
        accept = false;
        QVERIFY(!editor->applyDraft(&error));
        QCOMPARE(applied, before);
        accept = true;
        QVERIFY2(editor->applyDraft(&error), qPrintable(error));
        QVERIFY(!registry.type(root));
        QCOMPARE(applied.value("lineflow").toMap().value("type").toString(), QString("@inplace"));
        QCOMPARE(applied.value("inplace")
                     .toMap()
                     .value("lineflow")
                     .toMap()
                     .value(root)
                     .toMap()
                     .value("source")
                     .toMap()
                     .value("nodes")
                     .toList()
                     .size(),
                 1);
    }

    Q_SLOT void unavailableSelectionIsRetained()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry editors;
        ProcessingRouter       router;
        const QVariantMap      initial {
            {    "mode",                               "lineflow"},
            {"lineflow", QVariantMap {{"type", "missing.plugin"}}}
        };
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      initial,
                                      [](const QVariantMap&, const QVariantMap&, QString*) { return false; });
        QCOMPARE(panel.findChild<QComboBox*>("lineflowProcessor")->currentData().toString(), QString("missing.plugin"));
        panel.propertiesWidget()->show();
        QVERIFY(!panel.propertiesWidget()->findChild<QLabel*>("processingError")->text().isEmpty());
    }

    Q_SLOT void editorsAreReleasedSynchronously()
    {
        ProcessorRegistry      registry;
        ProcessorCatalog::Type type;
        type.inputCount  = 2;
        type.outputCount = 2;
        type.id          = "custom";
        type.title       = "Custom";
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
        ProcessingRouter        router;
        TerminalProcessingPanel panel(registry,
                                      editors,
                                      router,
                                      {
                                          {    "mode",                       "lineflow"},
                                          {"lineflow", QVariantMap {{"type", "custom"}}}
        },
                                      [](const QVariantMap&, const QVariantMap&, QString*) { return true; });
        panel.propertiesWidget()->show();
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
        select(panel->findChild<QComboBox*>("processingMode"), "lineflow");
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

    Q_SLOT void hiddenSelectorUpdatesAppearOnNextOpen()
    {
        Settings           settings(QString {});
        ProcessorFavorites favorites(settings.node("processing"));
        ProcessorRegistry  registry;
        registerNativeProcessors(registry, "native");
        ProcessorSelector selector(nullptr, &favorites);
        auto*             tree   = selector.findChild<ProcessorCatalogTree*>("processorSelectorTree");
        auto*             search = selector.findChild<QLineEdit*>("processorSelectorSearch");
        QVERIFY(tree && search);
        QSignalSpy resets(tree->model(), &QAbstractItemModel::modelReset);
        QSignalSpy changed(&selector, &QComboBox::currentIndexChanged);
        selector.setCatalog(registry.types(), "native.gate");
        selector.setInplaceAvailable(true);
        QVERIFY(favorites.setIds({"missing.processor"}));
        QCOMPARE(selector.currentData().toString(), QString("native.gate"));
        QVERIFY(selector.findData("native.stub") >= 0);
        QCOMPARE(selector.count(), 23);
        QCOMPARE(resets.count(), 0);
        selector.showPopup();
        QCOMPARE(tree->topLevelItem(0)->data(0, Qt::UserRole).toString(), QString("@inplace"));
        QCOMPARE(tree->topLevelItem(1)->data(0, Qt::UserRole).toString(), QString("missing.processor"));
        QCOMPARE(tree->currentItem()->data(0, Qt::UserRole).toString(), QString("native.gate"));
        search->setText("Stub");
        selector.hidePopup();
        const auto previousResets = resets.count();
        auto       types          = registry.types();
        for (auto& type : types)
        {
            if (type.id == "native.stub")
            {
                type.title             = "Updated stub";
                type.available         = false;
                type.unavailableReason = "Provider removed";
            }
        }
        selector.setCatalog(types, "native.stub");
        QVERIFY(favorites.setIds({"native.stub"}));
        QCOMPARE(selector.currentText(), QString("Updated stub"));
        QCOMPARE(selector.itemData(selector.currentIndex(), Qt::ToolTipRole).toString(), QString("Provider removed"));
        QCOMPARE(resets.count(), previousResets);
        selector.showPopup();
        QVERIFY(search->text().isEmpty());
        QCOMPARE(tree->currentItem()->data(0, Qt::UserRole).toString(), QString("native.stub"));
        QCOMPARE(tree->currentItem()->text(0), QString("Updated stub"));
        QCOMPARE(tree->currentItem()->toolTip(0), QString("Provider removed"));
        search->setText("Updated");
        QVERIFY(favorites.setIds({"missing.processor"}));
        QCOMPARE(search->text(), QString("Updated"));
        QVERIFY(!tree->currentItem()->isHidden());
        selector.setTopology(2);
        QCOMPARE(selector.currentData().toString(), QString("native.stub"));
        QVERIFY(selector.currentText().contains("Unavailable"));
        QCOMPARE(changed.count(), 0);
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
        receive.showPopup();
        QCOMPARE(rxTree->topLevelItem(0)->data(0, Qt::UserRole).toString(), QString("native.bypass"));
        QVERIFY(favorites.setFavorite("native.stub", true));
        QCOMPARE(rxTree->topLevelItem(1)->data(0, Qt::UserRole).toString(), QString("native.stub"));
        transmit.showPopup();
        QCOMPARE(txTree->topLevelItem(1)->data(0, Qt::UserRole).toString(), QString("native.stub"));
        transmit.hidePopup();
        receive.showPopup();
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
        receive.hidePopup();
        transmit.showPopup();
        QCOMPARE(txTree->topLevelItem(0)->data(0, Qt::UserRole).toString(), QString("native.gate"));
        transmit.hidePopup();
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
                {"test.protocol", block(2)}
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
        router.replaceDuplex(std::move(rxInstance));
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
                {    "mode",                                                 "lineflow"               },
                {"lineflow",
                 QVariantMap {{"type", "test.protocol"},
                 {"overrides", QVariantMap {{"Hex encoding", QVariantMap {{"letterCase", "lower"}}}}}}},
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
        QTRY_COMPARE(reset->height(), SettingsFieldEditor::controlVisualHeight(combo));
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
