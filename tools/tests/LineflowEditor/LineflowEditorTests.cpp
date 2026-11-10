#include "../SettingsVariantFixture.h"
#include "Gui/Processing/LineflowCanvas.h"
#include "Gui/Processing/LineflowEditorDialog.h"
#include "Gui/Processing/ProcessorCatalogTree.h"
#include "Gui/Processing/ProcessorFavorites.h"
#include "Gui/Processing/ProcessorSelector.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMimeData>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QtTest>
#include <Termaster/Core/Gui/Settings/SettingsFormWidget.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <stdexcept>

namespace
{
QVariantMap definition(const QString& id, const QString& name, QVariantList nodes)
{
    return {
        {    "id",                             id},
        {  "name",                           name},
        {  "kind",                     "lineflow"},
        {"source", QVariantMap {{"nodes", nodes}}}
    };
}
QVariantMap operation(const QString& name, const QString& type, QVariantMap configuration = {})
{
    return {
        {         "name",          name},
        {         "type",          type},
        {"configuration", configuration}
    };
}
QVariantMap fixtureDefinitions()
{
    return {
        {  "root",
         definition("root",
         "Device protocol", {operation("decode", "native.hex-decoder"),
 QVariantMap {{"name", "framing"}, {"reference", "shared"}},
 operation("future", "unavailable.processor")})                                                  },
        {"shared", definition("shared", "Shared framing",                             {operation("frame", "native.framer")})}
    };
}
class CustomEditor final : public SettingsFieldEditor
{
public:
    explicit CustomEditor(QWidget* parent) : SettingsFieldEditor(parent)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        edit = new QLineEdit(this);
        edit->setObjectName("fixtureCustomEdit");
        layout->addWidget(edit);
        connect(edit, &QLineEdit::textChanged, this, [this] { emit valueChanged(); });
    }
    QVariant value() const override
    {
        return edit->text();
    }
    void setValue(const QVariant& value) override
    {
        const QSignalBlocker blocker(edit);
        edit->setText(value.toString());
    }
    QLineEdit* edit;
};
void registerCustom(ProcessorRegistry& registry, SettingsWidgetRegistry& widgets)
{
    ProcessorCatalog::Type type;
    type.id             = "fixture.configured";
    type.title          = "Configured processor";
    type.settingsSchema = QByteArray(
        "version: 1\ntype: termaster.settings-schema\ndescription: Editor fixture\ncontent:\n  type: object\n  properties:\n    prefix:\n      type: string\n      title: Prefix\n      maxLength: 8\n      default: ok\n      x-widget: fixture.custom\n");
    type.create = [](const QVariantMap&, const Processor::Limits&, QString*) -> std::unique_ptr<Processor> {
        qFatal("Editor must not construct processors");
        return {};
    };
    registry.registerType("fixture", std::move(type));
    widgets.registerWidget("fixture.custom",
                           [](const SettingsSchema::Field&, QWidget* parent) { return new CustomEditor(parent); });
}
void registerDuplex(ProcessorRegistry& registry)
{
    ProcessorCatalog::Type duplex;
    duplex.id             = "fixture.duplex";
    duplex.title          = "Shared protocol";
    duplex.inputCount     = 2;
    duplex.outputCount    = 2;
    duplex.inputNames     = {"Device RX", "Terminal TX"};
    duplex.outputNames    = {"Terminal RX", "Device TX"};
    duplex.settingsSchema = QByteArray(
        "version: 1\ntype: termaster.settings-schema\ndescription: Duplex fixture\ncontent:\n  type: object\n  properties:\n    prefix:\n      type: string\n      title: Prefix\n      default: empty\n      x-widget: stringLineEdit\n");
    duplex.create = [](const auto&, const auto&, QString*) -> std::unique_ptr<Processor> {
        qFatal("Editor must not instantiate duplex processors");
        return {};
    };
    registry.registerType("fixture", std::move(duplex));
}
} // namespace
class LineflowEditorTests final : public QObject
{
    Q_OBJECT
private slots:
    void modelCrudReorderNestedAndCycles()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        auto                definitions = fixtureDefinitions();
        LineflowEditorModel model(definitions, registry);
        model.setSelectedBlock("root");
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.rowCount(model.index(1, 0)), 1);
        QVERIFY(model.index(0, 0, model.index(1, 0)).data(LineflowEditorModel::LinkedRole).toBool());
        QVERIFY(!(model.flags(model.index(0, 0, model.index(1, 0))) & Qt::ItemIsDragEnabled));
        QVERIFY(model.index(2, 0).data(LineflowEditorModel::UnavailableRole).toBool());
        QVERIFY(model.index(2, 0).data(Qt::ToolTipRole).toString().contains("unavailable.processor"));
        std::unique_ptr<QMimeData> drag(model.mimeData({model.index(0, 0)}));
        QVERIFY(model.dropMimeData(drag.get(), Qt::MoveAction, 2, 0, {}));
        QCOMPARE(model.index(1, 0).data(LineflowEditorModel::NodeRole).toString(), QString("decode"));
        QVERIFY(!model.canDropMimeData(drag.get(), Qt::MoveAction, 0, 0, model.index(0, 0)));
        LineflowEditorModel foreign(definitions, registry);
        QVERIFY(!foreign.canDropMimeData(drag.get(), Qt::MoveAction, 0, 0, {}));
        QString    error;
        const auto before = model.definitions();
        QVERIFY(model.insertNode("shared", -1, "root", &error).isEmpty());
        QVERIFY(error.contains("cycle"));
        QCOMPARE(model.definitions(), before);
        QVERIFY(model.renameNode("shared", "frame", "packetizer", &error));
        QCOMPARE(model.renames().size(), 2);
        QCOMPARE(model.renames()[0].block, QString("shared"));
        QCOMPARE(model.renames()[0].from, QString("frame"));
        QCOMPARE(model.renames()[1].to, QString("packetizer"));
        const auto copy = model.duplicateBlock("shared", "Another framing", &error);
        QVERIFY2(!copy.isEmpty(), qPrintable(error));
        QVERIFY(copy != "shared");
        QVERIFY(model.renameBlock(copy, "Renamed framing", &error));
        QCOMPARE(model.blockName(copy), QString("Renamed framing"));
        QVERIFY(model.removeBlock("shared", &error));
        model.setSelectedBlock("root");
        QVERIFY(model.index(0, 0).data(LineflowEditorModel::UnavailableRole).toBool());
        const auto created = model.createBlock("New block", &error);
        QVERIFY(!created.isEmpty());
        QCOMPARE(model.selectedBlock(), created);
        QVERIFY(model.validate(&error));
    }

    void renameHistoryOnlyMigratesOriginalIdentities()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        LineflowEditorModel model(fixtureDefinitions(), registry);
        QVERIFY(model.renameNode("shared", "frame", "temporary"));
        QVERIFY(model.renameNode("shared", "temporary", "frame"));
        QVERIFY(model.renames().isEmpty());
        QVERIFY(model.renameNode("shared", "frame", "one"));
        QVERIFY(model.renameNode("shared", "one", "two"));
        QCOMPARE(model.renames().size(), 2);
        QCOMPARE(model.renames()[0].from, QString("frame"));
        QCOMPARE(model.renames()[1].to, QString("two"));
        const auto block = model.createBlock("New block");
        const auto node  = model.insertNode(block, 0, "native.gate");
        QVERIFY(!node.isEmpty());
        QVERIFY(model.renameNode(block, node, "newName"));
        QCOMPARE(model.renames().size(), 2);
        const auto duplicate = model.duplicateBlock("shared", "Copied block");
        QVERIFY(model.renameNode(duplicate, "two", "copiedName"));
        QCOMPARE(model.renames().size(), 2);
        model.markApplied();
        QVERIFY(model.renames().isEmpty());
    }

    void swapRenameRecordsTransformOccurrences()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        const QVariantMap definitions {
            {"root",
             definition("root",
             "Swap sequence", {operation("first", "native.gate"), operation("second", "native.gate")})}
        };
        LineflowEditorModel model(definitions, registry);
        QVERIFY(model.renameNode("root", "first", "temporary"));
        QVERIFY(model.renameNode("root", "second", "first"));
        QVERIFY(model.renameNode("root", "temporary", "second"));
        const QVariantMap objects {
            {"terminal",
             QVariantMap {
                 {"settings",
                  QVariantMap {
                      {"processing",
                       QVariantMap {{"rx",
                                     QVariantMap {{"type", "root"},
                                                  {"overrides",
                                                   QVariantMap {{"first", QVariantMap {{"marker", 1}}},
                                                                {"second", QVariantMap {{"marker", 2}}}}}}}}}}}}}
        };
        QString    error;
        const auto result = ProcessorRefactoring::transform(model.baseline(),
                                                            model.definitions(),
                                                            model.renames(),
                                                            objects,
                                                            &error);
        QVERIFY2(result.has_value(), qPrintable(error));
        const auto overrides = result->sessionObjects.value("terminal")
                                   .toMap()
                                   .value("settings")
                                   .toMap()
                                   .value("processing")
                                   .toMap()
                                   .value("rx")
                                   .toMap()
                                   .value("overrides")
                                   .toMap();
        QCOMPARE(overrides.value("first").toMap().value("marker").toInt(), 2);
        QCOMPARE(overrides.value("second").toMap().value("marker").toInt(), 1);
    }

    void insertedNodeNamesUseNumberedCatalogTitlesAcrossInsertionPaths()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        LineflowEditorModel model({}, registry);
        const auto          block = model.createBlock("Root");
        QCOMPARE(model.insertNodeAt(block, 0, "native.gate", "rx"), QString("Gate"));
        QCOMPARE(model.insertNode(block, 1, "native.gate"), QString("Gate 2"));
        QVERIFY(model.renameNode(block, "Gate 2", "Gate 3"));
        QCOMPARE(model.insertNode(block, 2, "native.gate"), QString("Gate 2"));
        QCOMPARE(model.insertNode(block, 3, "native.gate"), QString("Gate 4"));
        QCOMPARE(model.insertNode(block, 4, "native.hex-encoder"), QString("HEX Encoder"));
        QMimeData palette;
        palette.setData("application/x-termaster-processor", "native.gate");
        QVERIFY(model.canDropAt(&palette, 5, "rx"));
        QVERIFY(model.dropAt(&palette, 5, "rx"));
        QCOMPARE(model.index(5, 0).data(LineflowEditorModel::NodeRole).toString(), QString("Gate 5"));
        QVERIFY(model.canDropMimeData(&palette, Qt::CopyAction, 6, 0, {}));
        QVERIFY(model.dropMimeData(&palette, Qt::CopyAction, 6, 0, {}));
        QCOMPARE(model.index(6, 0).data(LineflowEditorModel::NodeRole).toString(), QString("Gate 6"));
        std::unique_ptr<QMimeData> move(model.mimeData({model.index(0, 0)}));
        QVERIFY(model.dropAt(move.get(), 7, "rx"));
        QCOMPARE(model.index(6, 0).data(LineflowEditorModel::NodeRole).toString(), QString("Gate"));
        std::unique_ptr<QMimeData> oldMove(model.mimeData({model.index(6, 0)}));
        QVERIFY(model.dropMimeData(oldMove.get(), Qt::MoveAction, 0, 0, {}));
        QCOMPARE(model.index(0, 0).data(LineflowEditorModel::NodeRole).toString(), QString("Gate"));

        const auto nested = model.createBlock("My block");
        model.setSelectedBlock(block);
        QCOMPARE(model.insertNode(block, 7, nested), QString("My block"));
        palette.setData("application/x-termaster-processor", nested.toUtf8());
        QVERIFY(model.dropAt(&palette, 8, "rx"));
        QCOMPARE(model.index(8, 0).data(LineflowEditorModel::NodeRole).toString(), QString("My block 2"));
        const auto longBlock = model.createBlock(QString(256, 'X'));
        const auto longName  = model.insertNode(block, 9, longBlock);
        QCOMPARE(longName.size(), 256);
        QCOMPARE(longName, QString(256, 'X'));
        QCOMPARE(model.insertNode(block, 10, longBlock), QString(254, 'X') + " 2");
        const auto slashBlock = model.createBlock("Wire / protocol");
        QCOMPARE(model.insertNode(block, 11, slashBlock), QString("Wire   protocol"));
        QString error;
        QVERIFY2(model.validate(&error), qPrintable(error));
    }

    void viewportDropReordersInsertsAndRejectsCycle()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        LineflowEditorDialog dialog(fixtureDefinitions(), registry, widgets, LineflowEditorDialog::Apply {});
        dialog.show();
        auto* tree = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        tree->expandAll();
        QTest::qWait(20);
        const auto drop = [&](QMimeData* mime, Qt::DropAction action) {
            const auto      last = dialog.model().index(dialog.model().rowCount() - 1, 0);
            const QPoint    position(24, tree->visualRect(last).bottom() + 18);
            QDragEnterEvent enter(position, action, mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &enter);
            if (!enter.isAccepted())
            {
                return false;
            }
            QDragMoveEvent move(position, action, mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &move);
            if (!move.isAccepted())
            {
                return false;
            }
            QDropEvent event(QPointF(position), action, mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &event);
            return event.isAccepted();
        };
        std::unique_ptr<QMimeData> reorder(dialog.model().mimeData({dialog.model().index(0, 0)}));
        QVERIFY(drop(reorder.get(), Qt::MoveAction));
        QCOMPARE(dialog.model().index(2, 0).data(LineflowEditorModel::NodeRole).toString(), QString("decode"));
        QMimeData catalog;
        catalog.setData("application/x-termaster-processor", "native.gate");
        QVERIFY(drop(&catalog, Qt::CopyAction));
        QCOMPARE(dialog.model().rowCount(), 4);
        const auto before = dialog.model().definitions();
        catalog.setData("application/x-termaster-processor", "root");
        QVERIFY(!drop(&catalog, Qt::CopyAction));
        QCOMPARE(dialog.model().definitions(), before);
    }

    void draftFormsValidationRenameApplyAndUnknownFields()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerCustom(registry, widgets);
        QVariantMap original {
            {"root",
             definition("root",
             "Configured sequence", {operation("first", "fixture.configured", {{"future", QVariantMap {{"retained", 17}}}}),
                         operation("second", "fixture.configured")})}
        };
        QVariantMap                        persisted = original;
        int                                applies   = 0;
        QList<LineflowEditorModel::Rename> renames;
        bool                               baselineMatched = false;
        LineflowEditorDialog dialog(original,
                                    registry,
                                    widgets,
                                    [&](const auto& baseline, const auto& draft, const auto& changes, QString*) {
                                        baselineMatched = baseline == persisted;
                                        persisted       = draft;
                                        renames         = changes;
                                        ++applies;
                                        return true;
                                    });
        auto*                tree = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        dialog.show();
        tree->setCurrentIndex(dialog.model().index(0, 0));
        auto* form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(form);
        auto* custom = dynamic_cast<CustomEditor*>(form->editor("prefix"));
        QVERIFY(custom);
        QCOMPARE(custom->value().toString(), QString("ok"));
        QVERIFY_THROWS_EXCEPTION(std::logic_error, widgets.unregisterWidget("fixture.custom"));
        custom->edit->setText("invalid-too-long");
        tree->setCurrentIndex(dialog.model().index(1, 0));
        tree->setCurrentIndex(dialog.model().index(0, 0));
        form   = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        custom = dynamic_cast<CustomEditor*>(form->editor("prefix"));
        QCOMPARE(custom->value().toString(), QString("invalid-too-long"));
        QString error;
        QVERIFY(!dialog.applyDraft(&error));
        QCOMPARE(applies, 0);
        QCOMPARE(persisted, original);
        custom->edit->setText("new");
        auto* name = dialog.findChild<QLineEdit*>("nodeName");
        name->setText("renamed");
        QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
        QCOMPARE(dialog.model().node("root", "renamed").value("name").toString(), QString("renamed"));
        QVERIFY2(dialog.applyDraft(&error), qPrintable(error));
        QCOMPARE(applies, 1);
        QVERIFY(baselineMatched);
        QCOMPARE(renames.size(), 2);
        const auto config = dialog.model().node("root", "renamed").value("configuration").toMap();
        QCOMPARE(config.value("prefix").toString(), QString("new"));
        QCOMPARE(config.value("future").toMap().value("retained").toInt(), 17);
        QVERIFY(!dialog.model().node("root", "second").value("configuration").toMap().contains("prefix"));
    }

    void dirtyFormSurvivesStructuralResets()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerCustom(registry, widgets);
        const QVariantMap definitions {
            {"root",
             definition("root",
             "Draft sequence", {operation("first", "fixture.configured"), operation("second", "fixture.configured")})}
        };
        LineflowEditorDialog dialog(definitions,
                                    registry,
                                    widgets,
                                    [](const auto&, const auto&, const auto&, QString*) { return true; });
        dialog.show();
        auto* tree = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        tree->setCurrentIndex(dialog.model().index(0, 0));
        auto* form   = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        auto* editor = dynamic_cast<CustomEditor*>(form->editor("prefix"));
        editor->edit->setText("changed");
        dialog.findChild<QAction*>("moveDown")->trigger();
        QCOMPARE(dialog.model().index(1, 0).data(LineflowEditorModel::NodeRole).toString(), QString("first"));
        form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(form);
        QCOMPARE(form->editor("prefix")->value().toString(), QString("changed"));
        QVERIFY(!dialog.model().insertNode("root", 0, "fixture.configured").isEmpty());
        tree->setCurrentIndex(dialog.model().index(2, 0));
        form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QCOMPARE(form->editor("prefix")->value().toString(), QString("changed"));
        const auto extra = dialog.model().createBlock("Other draft");
        QVERIFY(!extra.isEmpty());
        dialog.model().setSelectedBlock("root");
        tree->setCurrentIndex(dialog.model().index(2, 0));
        form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QCOMPARE(form->editor("prefix")->value().toString(), QString("changed"));
        QString error;
        QVERIFY2(dialog.applyDraft(&error), qPrintable(error));
        QCOMPARE(dialog.model().node("root", "first").value("configuration").toMap().value("prefix").toString(),
                 QString("changed"));
    }

    void cancelAndPersistenceFailurePreserveDraft()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        const auto           original = fixtureDefinitions();
        int                  attempts = 0;
        LineflowEditorDialog dialog(original,
                                    registry,
                                    widgets,
                                    [&](const auto&, const auto&, const auto&, QString* error) {
                                        ++attempts;
                                        if (error)
                                        {
                                            *error = "Workspace changed concurrently";
                                        }
                                        return false;
                                    });
        QVERIFY(dialog.model().renameNode("shared", "frame", "changed"));
        const auto draft = dialog.model().definitions();
        QString    error;
        QVERIFY(!dialog.applyDraft(&error));
        QCOMPARE(error, QString("Workspace changed concurrently"));
        QCOMPARE(dialog.model().definitions(), draft);
        QCOMPARE(dialog.model().baseline(), original);
        dialog.reject();
        QCOMPARE(attempts, 1);
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
    }

    void pluginEditorRemovalAndUnavailablePreservation()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerCustom(registry, widgets);
        QVariantMap definitions {
            {"root",
             definition("root",
             "Plugin configuration", {operation("plugin", "fixture.configured", {{"future", 42}})})}
        };
        auto  dialog = std::make_unique<LineflowEditorDialog>(definitions,
                                                              registry,
                                                              widgets,
                                                              LineflowEditorDialog::Apply {});
        auto* tree   = dialog->findChild<LineflowCanvas*>("lineflowCanvas");
        tree->setCurrentIndex(dialog->model().index(0, 0));
        QVERIFY(
            dynamic_cast<CustomEditor*>(dialog->findChild<SettingsFormWidget*>("nodeSettingsForm")->editor("prefix")));
        QVERIFY_THROWS_EXCEPTION(std::logic_error, widgets.unregisterWidget("fixture.custom"));
        // App closes the editor before unregistering plugin factories.
        dialog.reset();
        QVERIFY(widgets.unregisterWidget("fixture.custom"));
        QVERIFY(registry.removeOwner("fixture"));
        LineflowEditorDialog unavailable(definitions, registry, widgets, LineflowEditorDialog::Apply {});
        tree = unavailable.findChild<LineflowCanvas*>("lineflowCanvas");
        tree->setCurrentIndex(unavailable.model().index(0, 0));
        QVERIFY(unavailable.model().index(0, 0).data(LineflowEditorModel::UnavailableRole).toBool());
        QCOMPARE(unavailable.model().definitions(), definitions);
        QVERIFY(!unavailable.findChild<SettingsFormWidget*>());
    }

    void releasingEditorsKeepsValidAndInvalidDrafts()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerCustom(registry, widgets);
        const QVariantMap definitions {
            {"root",
             definition("root",
             "Plugin draft", {operation("first", "fixture.configured"), operation("second", "fixture.configured")})}
        };
        LineflowEditorDialog dialog(definitions,
                                    registry,
                                    widgets,
                                    [](const auto&, const auto&, const auto&, QString*) { return true; });
        auto*                tree = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        tree->setCurrentIndex(dialog.model().index(0, 0));
        auto* form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        dynamic_cast<CustomEditor*>(form->editor("prefix"))->edit->setText("keep");
        dialog.releaseEditors();
        QVERIFY(!dialog.findChild<SettingsFormWidget*>());
        QCOMPARE(dialog.model().node("root", "first").value("configuration").toMap().value("prefix").toString(),
                 QString("keep"));
        QVERIFY(widgets.unregisterWidget("fixture.custom"));
        tree->setCurrentIndex(dialog.model().index(1, 0));
        QVERIFY(!dialog.findChild<SettingsFormWidget*>());
        QVERIFY(dialog.findChild<QLabel*>("editorError")->text().contains("Unknown settings widget"));
        widgets.registerWidget("fixture.custom", [](const auto&, QWidget* parent) { return new CustomEditor(parent); });
        tree->setCurrentIndex(dialog.model().index(0, 0));
        form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        dynamic_cast<CustomEditor*>(form->editor("prefix"))->edit->setText("invalid-too-long");
        dialog.releaseEditors();
        QVERIFY(widgets.unregisterWidget("fixture.custom"));
        widgets.registerWidget("fixture.custom", [](const auto&, QWidget* parent) { return new CustomEditor(parent); });
        tree->setCurrentIndex(dialog.model().index(1, 0));
        tree->setCurrentIndex(dialog.model().index(0, 0));
        form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(form);
        QCOMPARE(form->editor("prefix")->value().toString(), QString("invalid-too-long"));
        QString error;
        QVERIFY(!dialog.applyDraft(&error));
        dialog.releaseEditors();
        QVERIFY(widgets.unregisterWidget("fixture.custom"));
        QVERIFY(registry.removeOwner("fixture"));
        QCOMPARE(dialog.model().node("root", "first").value("configuration").toMap().value("prefix").toString(),
                 QString("keep"));
        QVERIFY(dialog.model().index(0, 0).data(LineflowEditorModel::UnavailableRole).toBool());
    }

    void canonicalApplySnapshotBecomesNextBaseline()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        auto root = definition("root", "Canonical sequence", {operation("first", "native.gate")});
        root.insert("defaults",
                    QVariantMap {
                        {"overrides", QVariantMap {{"first", QVariantMap {{"marker", 7}}}}}
        });
        QVariantMap persisted {
            {"root", root}
        };
        int                  attempts = 0;
        LineflowEditorDialog dialog(persisted,
                                    registry,
                                    widgets,
                                    [&](const QVariantMap&                        baseline,
                                        const QVariantMap&                        draft,
                                        const QList<LineflowEditorModel::Rename>& renames,
                                        QString* error) -> LineflowEditorDialog::ApplyResult {
                                        if (baseline != persisted)
                                        {
                                            if (error)
                                            {
                                                *error = "Baseline mismatch";
                                            }
                                            return false;
                                        }
                                        const auto transformed = ProcessorRefactoring::transform(baseline,
                                                                                                 draft,
                                                                                                 renames,
                                                                                                 {},
                                                                                                 error);
                                        if (!transformed)
                                        {
                                            return false;
                                        }
                                        persisted = transformed->definitions;
                                        if (++attempts == 1)
                                        {
                                            persisted.insert("external",
                                                             definition("external", "Concurrent block", {}));
                                        }
                                        return {true, persisted};
                                    });
        QVERIFY(dialog.model().renameNode("root", "first", "renamed"));
        QString error;
        QVERIFY2(dialog.applyDraft(&error), qPrintable(error));
        QCOMPARE(dialog.model().baseline(), persisted);
        QCOMPARE(dialog.model().definitions(), persisted);
        const auto overrides = persisted.value("root").toMap().value("defaults").toMap().value("overrides").toMap();
        QVERIFY(overrides.contains("renamed"));
        QVERIFY(!overrides.contains("first"));
        auto* blocks = dialog.findChild<QComboBox*>("blockSelector");
        QVERIFY(blocks->findData("external") >= 0);
        QVERIFY2(dialog.applyDraft(&error), qPrintable(error));
        QCOMPARE(attempts, 2);
    }

    void pluginSchemaReloadMigratesPendingValuesAndDefaults()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerCustom(registry, widgets);
        const QVariantMap definitions {
            {"root",
             definition("root",
             "Reload sequence", {operation("first", "fixture.configured", {{"unknown", 19}}),
                         operation("second", "fixture.configured")})}
        };
        LineflowEditorDialog dialog(definitions,
                                    registry,
                                    widgets,
                                    [](const auto&, const auto&, const auto&, QString*) { return true; });
        auto*                tree = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        tree->setCurrentIndex(dialog.model().index(1, 0));
        tree->setCurrentIndex(dialog.model().index(0, 0));
        auto* form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        dynamic_cast<CustomEditor*>(form->editor("prefix"))->edit->setText("pending-long-value");
        dialog.releaseEditors();
        QVERIFY(widgets.unregisterWidget("fixture.custom"));
        QVERIFY(registry.removeOwner("fixture"));
        widgets.registerWidget("fixture.custom", [](const auto&, QWidget* parent) { return new CustomEditor(parent); });
        ProcessorCatalog::Type type;
        type.id             = "fixture.configured";
        type.title          = "Updated processor";
        type.settingsSchema = QByteArray(
            "version: 1\ntype: termaster.settings-schema\ndescription: Reloaded editor fixture\ncontent:\n  type: object\n  properties:\n    prefix:\n      type: string\n      title: Prefix\n      maxLength: 32\n      default: new-default\n      x-widget: fixture.custom\n    added:\n      type: string\n      title: Added field\n      default: added-default\n      x-widget: fixture.custom\n");
        type.create = [](const auto&, const auto&, QString*) -> std::unique_ptr<Processor> {
            qFatal("Editor must not instantiate reloaded processors");
            return {};
        };
        registry.registerType("fixture", std::move(type));
        tree->setCurrentIndex(dialog.model().index(0, 0));
        form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(form);
        QVERIFY(form->editor("added"));
        QCOMPARE(form->editor("prefix")->value().toString(), QString("pending-long-value"));
        QCOMPARE(form->editor("added")->value().toString(), QString("added-default"));
        tree->setCurrentIndex(dialog.model().index(1, 0));
        form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QCOMPARE(form->editor("prefix")->value().toString(), QString("new-default"));
        QVERIFY(form->editor("added"));
        QString error;
        QVERIFY2(dialog.applyDraft(&error), qPrintable(error));
        const auto config = dialog.model().node("root", "first").value("configuration").toMap();
        QCOMPARE(config.value("prefix").toString(), QString("pending-long-value"));
        QCOMPARE(config.value("unknown").toInt(), 19);
        QVERIFY(!dialog.model().node("root", "second").value("configuration").toMap().contains("prefix"));
    }

    void initialGeometryFitsAvailableScreen()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        LineflowEditorDialog dialog(fixtureDefinitions(),
                                    registry,
                                    widgets,
                                    [](const auto&, const auto&, const auto&, QString*) { return true; });
        dialog.show();
        const auto available = QGuiApplication::primaryScreen()->availableGeometry().size();
        QVERIFY(dialog.width() <= available.width());
        QVERIFY(dialog.height() <= available.height());
    }

    void catalogsFollowTopologyWithoutChangingSelection()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        registerDuplex(registry);
        SettingsWidgetRegistry widgets;
        LineflowEditorDialog   dialog(fixtureDefinitions(),
                                      registry,
                                      widgets,
                                      [](const auto&, const auto&, const auto&, QString*) { return true; });
        auto*                  catalog  = dialog.findChild<QTreeWidget*>("processorCatalog");
        const auto             contains = [](QTreeWidget* tree, const QString& id) {
            for (QTreeWidgetItemIterator it(tree); *it; ++it)
            {
                if ((*it)->data(0, Qt::UserRole).toString() == id)
                {
                    return true;
                }
            }
            return false;
        };
        QVERIFY(contains(catalog, "native.hex-encoder"));
        QVERIFY(!contains(catalog, "fixture.duplex"));
        const auto paired = dialog.model().createBlock("Paired", 2);
        dialog.model().setSelectedBlock(paired);
        QVERIFY(contains(catalog, "native.hex-encoder"));
        QVERIFY(contains(catalog, "fixture.duplex"));
        dialog.model().setSelectedBlock("root");
        QVERIFY(!contains(catalog, "fixture.duplex"));

        QTemporaryDir      temporary;
        Settings           preferences(temporary.filePath("preferences.yaml"));
        ProcessorFavorites favorites(preferences.node("processing"));
        QVERIFY(favorites.setIds({"native.hex-encoder", "fixture.duplex"}));
        ProcessorSelector selector(nullptr, &favorites);
        selector.setCatalog(registry.types(), "native.hex-encoder");
        auto* tree = selector.findChild<QTreeWidget*>("processorSelectorTree");
        QVERIFY(contains(tree, "native.hex-encoder"));
        QVERIFY(!contains(tree, "fixture.duplex"));
        QSignalSpy changed(&selector, &QComboBox::currentIndexChanged);
        selector.setTopology(2);
        QCOMPARE(selector.currentData().toString(), QString("native.hex-encoder"));
        QVERIFY(selector.currentText().contains("Unavailable"));
        QVERIFY(!contains(tree, "native.hex-encoder"));
        QVERIFY(contains(tree, "fixture.duplex"));
        QCOMPARE(changed.count(), 0);
        selector.setInplaceAvailable(true);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(tree->topLevelItem(0)->data(0, Qt::UserRole).toString(), QString("@inplace"));
        QVERIFY(!favorites.contains("@inplace"));
        selector.setCatalog(registry.types(), "@inplace");
        QCOMPARE(selector.currentText(), QString("Inplace"));
        selector.setTopology(1);
        QCOMPARE(selector.currentData().toString(), QString("@inplace"));
        QCOMPARE(selector.currentText(), QString("Inplace"));
        QVERIFY(!contains(tree, "fixture.duplex"));
        QCOMPARE(changed.count(), 0);
    }

    void catalogOriginsGroupsAndParameterizedForms()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        auto definitions = fixtureDefinitions();
        auto root        = definitions.value("root").toMap();
        root.insert("group", "Protocols");
        definitions.insert("root", root);
        LineflowEditorDialog dialog(definitions,
                                    registry,
                                    widgets,
                                    [](const auto&, const auto&, const auto&, QString*) { return true; });
        auto*                catalog = dialog.findChild<QTreeWidget*>("processorCatalog");
        QVERIFY(catalog);
        QCOMPARE(catalog->topLevelItem(0)->data(0, Qt::UserRole).toString(), QString("native.bypass"));
        QVERIFY(catalog->topLevelItem(1)->data(0, ProcessorCatalogTree::SeparatorRole).toBool());
        QCOMPARE(catalog->topLevelItemCount(), 4);
        QCOMPARE(catalog->topLevelItem(2)->text(0), QString("Native"));
        QCOMPARE(catalog->topLevelItem(3)->text(0), QString("Workspace"));
        const auto findGroup = [](QTreeWidgetItem* origin, const QString& name) {
            for (int row = 0; row < origin->childCount(); ++row)
            {
                if (origin->child(row)->text(0) == name)
                {
                    return origin->child(row);
                }
            }
            return static_cast<QTreeWidgetItem*>(nullptr);
        };
        auto* codecs = findGroup(catalog->topLevelItem(2), "Codecs");
        QVERIFY(codecs);
        QCOMPARE(codecs->childCount(), 4);
        QVERIFY(!codecs->isExpanded());
        QVERIFY(!(codecs->flags() & Qt::ItemIsDragEnabled));
        QVERIFY(!(catalog->topLevelItem(2)->flags() & Qt::ItemIsSelectable));
        QVERIFY(findGroup(catalog->topLevelItem(3), "Protocols"));
        QVERIFY(dialog.model().setBlockGroup("root", "Devices"));
        QVERIFY(findGroup(catalog->topLevelItem(3), "Devices"));
        QCOMPARE(dialog.findChild<QComboBox*>("blockGroup")->currentText(), QString("Devices"));
        auto* search = dialog.findChild<QLineEdit*>("catalogSearch");
        search->setText("base64");
        codecs = findGroup(catalog->topLevelItem(0), "Codecs");
        QVERIFY(codecs);
        QCOMPARE(codecs->childCount(), 2);
        QVERIFY(catalog->topLevelItem(1)->isHidden());
        search->clear();

        const auto block = dialog.model().createBlock("Codec example");
        const auto node  = dialog.model().insertNode(block, 0, "native.hex-encoder");
        QVERIFY(!node.isEmpty());
        dialog.model().setSelectedBlock(block);
        auto* tree = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        dialog.show();
        tree->setCurrentIndex(dialog.model().index(0, 0));
        auto* form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(form);
        QVERIFY(form->editor("letterCase"));
        QVERIFY(form->editor("separator"));
        const auto gateName = dialog.model().insertNode(block, 1, "native.gate");
        QVERIFY(!gateName.isEmpty());
        tree->setCurrentIndex(dialog.model().index(1, 0));
        form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(form && form->editor("enabled"));
        QCOMPARE(form->editor("enabled")->value().toBool(), true);
    }

    void groupDraftMustValidateBeforeApply()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        int                  applies = 0;
        LineflowEditorDialog dialog(fixtureDefinitions(),
                                    registry,
                                    widgets,
                                    [&](const auto&, const auto&, const auto&, QString*) {
                                        ++applies;
                                        return true;
                                    });
        auto*                group = dialog.findChild<QComboBox*>("blockGroup");
        group->setEditText(" padded ");
        QString error;
        QVERIFY(!dialog.applyDraft(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(applies, 0);
        group->setEditText("Protocols");
        QVERIFY2(dialog.applyDraft(&error), qPrintable(error));
        QCOMPARE(applies, 1);
        QCOMPARE(dialog.model().blockGroup("root"), QString("Protocols"));
    }

    void docksResizeFloatAndRestoreWithoutLosingSchemaDraft()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerCustom(registry, widgets);
        LineflowEditorDialog dialog(
            {
                {"root", definition("root", "Configured", {operation("first", "fixture.configured")})}
        },
            registry,
            widgets,
            [](const auto&, const auto&, const auto&, QString*) { return true; });
        dialog.setFixedSize(1100, 760);
        dialog.show();
        auto* canvas     = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        auto* host       = dialog.findChild<QMainWindow*>("lineflowDockHost");
        auto* palette    = dialog.findChild<QDockWidget*>("lineflowPaletteDock");
        auto* properties = dialog.findChild<QDockWidget*>("lineflowPropertiesDock");
        QVERIFY(canvas && host && palette && properties);
        QVERIFY(!host->isWindow());
        QTRY_VERIFY(host->isVisibleTo(&dialog));
        QVERIFY(canvas->isVisibleTo(&dialog));
        QVERIFY(palette->isVisibleTo(&dialog));
        QVERIFY(properties->isVisibleTo(&dialog));
        QVERIFY(host->height() > 300);
        QVERIFY(canvas->viewport()->width() > 250);
        QVERIFY(canvas->viewport()->height() > 250);
        QCOMPARE(host->dockWidgetArea(palette), Qt::LeftDockWidgetArea);
        QCOMPARE(host->dockWidgetArea(properties), Qt::RightDockWidgetArea);
        canvas->setCurrentIndex(dialog.model().index(0, 0));
        auto* form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(form);
        dynamic_cast<CustomEditor*>(form->editor("prefix"))->edit->setText("pending");
        const auto initial = dialog.dockLayout();
        QSignalSpy layouts(&dialog, &LineflowEditorDialog::dockLayoutChanged);
        host->resizeDocks({palette, properties}, {250, 280}, Qt::Horizontal);
        properties->setFloating(true);
        properties->resize(320, 430);
        QTRY_VERIFY(properties->isFloating());
        QTRY_VERIFY(!layouts.isEmpty());
        QCOMPARE(form->editor("prefix")->value().toString(), QString("pending"));
        QVERIFY(!dialog.restoreDockLayout(QByteArray(65537, 'x')));
        QVERIFY(dialog.restoreDockLayout(initial));
        QTRY_VERIFY(!properties->isFloating());
        QCOMPARE(host->dockWidgetArea(properties), Qt::RightDockWidgetArea);
        palette->setFloating(true);
        properties->setFloating(true);
        auto* reset = dialog.findChild<QAction*>("restorePanels");
        QVERIFY(reset);
        reset->trigger();
        QTRY_VERIFY(!palette->isFloating() && !properties->isFloating());
        QCOMPARE(host->dockWidgetArea(palette), Qt::LeftDockWidgetArea);
        QCOMPARE(host->dockWidgetArea(properties), Qt::RightDockWidgetArea);
        QString error;
        QVERIFY2(dialog.applyDraft(&error), qPrintable(error));
        QCOMPARE(dialog.model().node("root", "first").value("configuration").toMap().value("prefix").toString(),
                 QString("pending"));
    }

    void pairedCanvasDropPlacementMovesAndCleanIdle()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        registerDuplex(registry);
        LineflowEditorDialog dialog({}, registry, widgets, [](const auto&, const auto&, const auto&, QString*) {
            return true;
        });
        const auto           block = dialog.model().createBlock("Paired", 2);
        QVERIFY(!block.isEmpty());
        dialog.setFixedSize(1100, 760);
        dialog.show();
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        QVERIFY(!canvas->dropIndicatorVisible());
        const auto drop = [&](QMimeData* mime, int x, int row, Qt::DropAction action) {
            const int y = row < dialog.model().rowCount() ?
                              canvas->visualRect(dialog.model().index(row, 0)).top() - 10 :
                          dialog.model().rowCount() ?
                              canvas->visualRect(dialog.model().index(dialog.model().rowCount() - 1, 0)).bottom() + 18 :
                              60;
            const QPoint    position(x, y);
            QDragEnterEvent enter(position, action, mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &enter);
            if (!enter.isAccepted())
            {
                return false;
            }
            if (!canvas->dropIndicatorVisible())
            {
                return false;
            }
            QDropEvent event(QPointF(position), action, mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &event);
            return event.isAccepted() && !canvas->dropIndicatorVisible();
        };
        QMimeData native;
        native.setData("application/x-termaster-processor", "native.gate");
        QVERIFY(drop(&native, canvas->viewport()->width() / 4, 0, Qt::CopyAction));
        QVERIFY(drop(&native, canvas->viewport()->width() * 3 / 4, 1, Qt::CopyAction));
        QCOMPARE(dialog.model().index(0, 0).data(LineflowEditorModel::LaneRole).toString(), QString("rx"));
        QCOMPARE(dialog.model().index(1, 0).data(LineflowEditorModel::LaneRole).toString(), QString("tx"));
        QCOMPARE(canvas->visualRect(dialog.model().index(0, 0)).top(),
                 canvas->visualRect(dialog.model().index(1, 0)).top());
        native.setData("application/x-termaster-processor", "fixture.duplex");
        QVERIFY(drop(&native, canvas->viewport()->width() / 2, 2, Qt::CopyAction));
        QCOMPARE(dialog.model().index(2, 0).data(LineflowEditorModel::LaneRole).toString(), QString("duplex"));
        QVERIFY(canvas->visualRect(dialog.model().index(2, 0)).width()
                > canvas->visualRect(dialog.model().index(0, 0)).width());
        std::unique_ptr<QMimeData> move(dialog.model().mimeData({dialog.model().index(0, 0)}));
        QVERIFY(drop(move.get(), canvas->viewport()->width() * 3 / 4, 3, Qt::MoveAction));
        QCOMPARE(dialog.model().index(2, 0).data(LineflowEditorModel::LaneRole).toString(), QString("tx"));
        QCOMPARE(dialog.model().index(2, 0).data(LineflowEditorModel::NodeRole).toString(), QString("Gate"));
        std::unique_ptr<QMimeData> moveDuplex(dialog.model().mimeData({dialog.model().index(1, 0)}));
        QVERIFY(drop(moveDuplex.get(), canvas->viewport()->width() / 4, 3, Qt::MoveAction));
        QCOMPARE(dialog.model().index(2, 0).data(LineflowEditorModel::LaneRole).toString(), QString("duplex"));
        QVERIFY(canvas->visualRect(dialog.model().index(2, 0)).width()
                > canvas->visualRect(dialog.model().index(0, 0)).width());
        const auto before = dialog.model().definitions();
        QString    error;
        QVERIFY(!dialog.model().setBlockChannels(block, 1, &error));
        QCOMPARE(dialog.model().definitions(), before);
        QVERIFY(!error.isEmpty());
        QDragLeaveEvent leave;
        QApplication::sendEvent(canvas->viewport(), &leave);
        QVERIFY(!canvas->dropIndicatorVisible());
        const auto single = dialog.model().createBlock("Single", 1);
        QVERIFY(!single.isEmpty());
        QVERIFY(!dialog.model().canDropAt(&native, 0, "duplex"));
        const auto nested = dialog.model().insertNodeAt(block, 0, single, "rx", &error);
        QVERIFY2(!nested.isEmpty(), qPrintable(error));
        const auto paired = dialog.model().createBlock("Nested paired", 2);
        QVERIFY(!dialog.model().insertNodeAt(paired, 0, block, "duplex", &error).isEmpty());
        QCOMPARE(dialog.model().processorShape(block), qMakePair(2, 2));
    }

    void narrowCanvasDropsFollowVisibleLaneEdges()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        LineflowEditorDialog dialog({}, registry, widgets, LineflowEditorDialog::Apply {});
        const auto           block = dialog.model().createBlock("Paired", 2);
        QVERIFY(!dialog.model().insertNodeAt(block, 0, "native.gate", "rx").isEmpty());
        QVERIFY(!dialog.model().insertNodeAt(block, 1, "native.gate", "tx").isEmpty());
        dialog.show();
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        canvas->setFixedWidth(300);
        QTRY_VERIFY(canvas->viewport()->width() <= 300);
        const auto rxRect = canvas->visualRect(dialog.model().index(0, 0));
        const auto txRect = canvas->visualRect(dialog.model().index(1, 0));
        const auto drop   = [&](int x) {
            QMimeData palette;
            palette.setData("application/x-termaster-processor", "native.gate");
            const QPoint    position(x, rxRect.top() - 4);
            QDragEnterEvent enter(position, Qt::CopyAction, &palette, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &enter);
            if (!enter.isAccepted())
            {
                return false;
            }
            QDropEvent event(QPointF(position), Qt::CopyAction, &palette, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &event);
            return event.isAccepted();
        };
        QVERIFY(drop(rxRect.right() - 2));
        QCOMPARE(dialog.model().node(block, "Gate 3").value("lane").toString(), QString("rx"));
        QVERIFY(drop(txRect.left() + 2));
        QCOMPARE(dialog.model().node(block, "Gate 4").value("lane").toString(), QString("tx"));
    }

    void largeFontCanvasCanScrollToWholeTerminalFooter()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        LineflowEditorDialog dialog({}, registry, widgets, LineflowEditorDialog::Apply {});
        const auto           block = dialog.model().createBlock("Long flow");
        for (int row = 0; row < 6; ++row)
        {
            QVERIFY(!dialog.model().insertNode(block, row, "native.gate").isEmpty());
        }
        dialog.show();
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        canvas->setFixedHeight(220);
        auto font = canvas->font();
        font.setPointSizeF(font.pointSizeF() * 2);
        canvas->setFont(font);
        QTRY_VERIFY(canvas->verticalScrollBar()->maximum() > 0);
        canvas->verticalScrollBar()->setValue(canvas->verticalScrollBar()->maximum());
        const auto lastCard     = canvas->visualRect(dialog.model().index(5, 0));
        const int  footerBottom = lastCard.bottom() + qMax(44, canvas->fontMetrics().height() + 32) + 35;
        QVERIFY2(footerBottom < canvas->viewport()->height(), "The Terminal footer must be fully scrollable into view");
    }

    void duplexDropPartitionsIndependentLaneOrderAtVisualPosition()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        registerDuplex(registry);
        LineflowEditorDialog dialog({}, registry, widgets, LineflowEditorDialog::Apply {});
        const auto           block    = dialog.model().createBlock("Paired", 2);
        const auto           rxFirst  = dialog.model().insertNodeAt(block, 0, "native.gate", "rx");
        const auto           rxSecond = dialog.model().insertNodeAt(block, 1, "native.gate", "rx");
        const auto           txFirst  = dialog.model().insertNodeAt(block, 2, "native.gate", "tx");
        QVERIFY(!rxFirst.isEmpty() && !rxSecond.isEmpty() && !txFirst.isEmpty());
        dialog.setFixedSize(1100, 760);
        dialog.show();
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        QTest::qWait(20);
        const auto firstRect  = canvas->visualRect(dialog.model().index(0, 0));
        const auto secondRect = canvas->visualRect(dialog.model().index(1, 0));
        const auto txRect     = canvas->visualRect(dialog.model().index(2, 0));
        QCOMPARE(firstRect.top(), txRect.top());
        QVERIFY(secondRect.top() > firstRect.bottom());
        QMimeData mime;
        mime.setData("application/x-termaster-processor", "fixture.duplex");
        const QPoint    position(canvas->viewport()->width() / 2, (firstRect.bottom() + secondRect.top()) / 2);
        QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(position), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(dialog.model().index(0, 0).data(LineflowEditorModel::NodeRole).toString(), rxFirst);
        QCOMPARE(dialog.model().index(1, 0).data(LineflowEditorModel::NodeRole).toString(), txFirst);
        QCOMPARE(dialog.model().index(2, 0).data(LineflowEditorModel::LaneRole).toString(), QString("duplex"));
        QCOMPARE(dialog.model().index(3, 0).data(LineflowEditorModel::NodeRole).toString(), rxSecond);
        const auto barrier = canvas->visualRect(dialog.model().index(2, 0));
        QVERIFY(barrier.top() > canvas->visualRect(dialog.model().index(0, 0)).bottom());
        QVERIFY(barrier.top() > canvas->visualRect(dialog.model().index(1, 0)).bottom());
        QVERIFY(canvas->visualRect(dialog.model().index(3, 0)).top() > barrier.bottom());
        QVERIFY(!canvas->dropIndicatorVisible());
    }

    void namedPortsDirectionsFlipAndNestedMetadata()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        registerDuplex(registry);
        LineflowEditorDialog dialog({}, registry, widgets, LineflowEditorDialog::Apply {});
        const auto           block  = dialog.model().createBlock("Paired", 2);
        const auto           rx     = dialog.model().insertNodeAt(block, 0, "native.gate", "rx");
        const auto           duplex = dialog.model().insertNodeAt(block, 1, "fixture.duplex", "duplex");
        const auto           tx     = dialog.model().insertNodeAt(block, 2, "native.gate", "tx");
        QVERIFY(!rx.isEmpty() && !duplex.isEmpty() && !tx.isEmpty());
        dialog.setFixedSize(1100, 760);
        dialog.show();
        auto*      canvas  = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        const auto rxPorts = canvas->ports(dialog.model().index(0, 0));
        QCOMPARE(rxPorts.size(), 2);
        QVERIFY(rxPorts[0].top && rxPorts[0].input);
        QVERIFY(!rxPorts[1].top && !rxPorts[1].input);
        const auto txPorts = canvas->ports(dialog.model().index(2, 0));
        QCOMPARE(txPorts.size(), 2);
        for (int row = 0; row < dialog.model().rowCount(); ++row)
        {
            const auto card         = canvas->visualRect(dialog.model().index(row, 0));
            const auto portGeometry = canvas->ports(dialog.model().index(row, 0));
            for (const auto& port : portGeometry)
            {
                QVERIFY(card.contains(port.label));
                QCOMPARE(port.marker.center(), port.point);
                QVERIFY(card.intersects(port.marker));
                QVERIFY(port.marker.left() <= port.point.x() && port.marker.right() >= port.point.x());
                if (port.top)
                {
                    QVERIFY(port.marker.top() < card.top());
                    QVERIFY(port.marker.bottom() > card.top());
                }
                else
                {
                    QVERIFY(port.marker.top() < card.bottom());
                    QVERIFY(port.marker.bottom() > card.bottom());
                }
                QCOMPARE(port.point.y(), port.top ? card.top() : card.bottom());
                QVERIFY(!port.label.contains(port.point));
            }
            for (int left = 0; left < portGeometry.size(); ++left)
            {
                for (int right = left + 1; right < portGeometry.size(); ++right)
                {
                    QVERIFY(!portGeometry[left].label.intersects(portGeometry[right].label));
                }
            }
        }
        QVERIFY(txPorts[0].top && !txPorts[0].input);
        QVERIFY(!txPorts[1].top && txPorts[1].input);
        const auto original = canvas->ports(dialog.model().index(1, 0));
        QCOMPARE(original.size(), 4);
        QCOMPARE(rxPorts[0].point.x(), original[0].point.x());
        QCOMPARE(rxPorts[1].point.x(), original[2].point.x());
        QCOMPARE(txPorts[0].point.x(), original[1].point.x());
        QCOMPARE(txPorts[1].point.x(), original[3].point.x());
        QVERIFY(original[0].point.x() < original[1].point.x());
        QCOMPARE(original[0].name, QString("Device RX"));
        QCOMPARE(original[1].name, QString("Device TX"));
        QCOMPARE(original[2].name, QString("Terminal RX"));
        QCOMPARE(original[3].name, QString("Terminal TX"));
        QVERIFY(original[0].input && original[0].top);
        QVERIFY(!original[1].input && original[1].top);
        QVERIFY(!original[2].input && !original[2].top);
        QVERIFY(original[3].input && !original[3].top);
        for (const auto& left : rxPorts)
        {
            for (const auto& right : original)
            {
                QVERIFY(!left.label.intersects(right.label));
            }
        }
        const auto configuration = dialog.model().node(block, duplex).value("configuration");
        QString    error;
        QVERIFY2(dialog.model().setNodeFlipped(block, duplex, true, &error), qPrintable(error));
        const auto flipped = canvas->ports(dialog.model().index(1, 0));
        QCOMPARE(flipped[0].name, QString("Terminal TX"));
        QCOMPARE(flipped[1].name, QString("Terminal RX"));
        QCOMPARE(flipped[2].name, QString("Device TX"));
        QCOMPARE(flipped[3].name, QString("Device RX"));
        for (int port = 0; port < 4; ++port)
        {
            QCOMPARE(flipped[port].index, original[port].index ^ 1);
            QCOMPARE(flipped[port].point, original[port].point);
            QCOMPARE(flipped[port].input, original[port].input);
        }
        QCOMPARE(dialog.model().node(block, duplex).value("configuration"), configuration);
        QCOMPARE(dialog.model().node(block, duplex).value("lane").toString(), QString("duplex"));
        QVERIFY(dialog.model().index(1, 0).data(LineflowEditorModel::FlippedRole).toBool());
        QVERIFY(!dialog.model().setNodeFlipped(block, rx, true, &error));
        QVERIFY(!dialog.model().node(block, rx).contains("flipped"));
        const auto nested = dialog.model().createBlock("Nested", 2);
        QVERIFY(!dialog.model().insertNodeAt(nested, 0, block, "duplex").isEmpty());
        QCOMPARE(dialog.model().index(0, 0).data(LineflowEditorModel::InputNamesRole).toStringList(),
                 QStringList({"Device Rx", "Terminal Tx"}));
        QCOMPARE(dialog.model().index(0, 0).data(LineflowEditorModel::OutputNamesRole).toStringList(),
                 QStringList({"Terminal Rx", "Device Tx"}));
        const auto preview = dialog.model().index(0, 0, dialog.model().index(0, 0));
        QVERIFY(preview.data(LineflowEditorModel::LinkedRole).toBool());
        QVERIFY(canvas->ports(preview).isEmpty());
        const auto previewRect = canvas->visualRect(preview);
        for (const auto& port : canvas->ports(dialog.model().index(0, 0)))
        {
            QVERIFY(!port.label.intersects(previewRect));
        }
        auto customNames = dialog.model().definitions();
        auto namedBlock  = customNames.value(block).toMap();
        namedBlock.insert("inputNames", QStringList {"Wire", "User"});
        namedBlock.insert("outputNames", QStringList {"Screen", "Cable"});
        customNames.insert(block, namedBlock);
        LineflowEditorModel customModel(customNames, registry);
        customModel.setSelectedBlock(nested);
        QCOMPARE(customModel.index(0, 0).data(LineflowEditorModel::InputNamesRole).toStringList(),
                 QStringList({"Wire", "User"}));
        QCOMPARE(customModel.index(0, 0).data(LineflowEditorModel::OutputNamesRole).toStringList(),
                 QStringList({"Screen", "Cable"}));
        QVERIFY2(customModel.validate(&error), qPrintable(error));
        auto invalid = dialog.model().definitions();
        auto root    = invalid.value(block).toMap();
        auto source  = root.value("source").toMap();
        auto nodes   = source.value("nodes").toList();
        auto entry   = nodes[0].toMap();
        entry.insert("flipped", true);
        nodes[0] = entry;
        source.insert("nodes", nodes);
        root.insert("source", source);
        invalid.insert(block, root);
        LineflowEditorModel invalidModel(invalid, registry);
        QVERIFY(!invalidModel.validate(&error));
    }

    void duplexPortLabelsStayInsideNarrowBlocksAtLargeFonts()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        registerDuplex(registry);
        LineflowEditorDialog dialog({}, registry, widgets, LineflowEditorDialog::Apply {});
        const auto           block = dialog.model().createBlock("Narrow paired flow", 2);
        QVERIFY(!dialog.model().insertNodeAt(block, 0, "fixture.duplex", "duplex").isEmpty());
        dialog.show();
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        canvas->setFixedWidth(300);
        QTRY_VERIFY(canvas->viewport()->width() <= 300);
        for (int scale = 1; scale <= 2; ++scale)
        {
            const auto index = dialog.model().index(0, 0);
            const auto card  = canvas->visualRect(index);
            const auto ports = canvas->ports(index);
            QCOMPARE(ports.size(), 4);
            for (const auto& port : ports)
            {
                QVERIFY(card.adjusted(6, 6, -6, -6).contains(port.label));
                QCOMPARE(port.marker.center(), port.point);
                QVERIFY(card.intersects(port.marker));
                QVERIFY(!port.name.isEmpty());
            }
            if (scale == 1)
            {
                auto font = canvas->font();
                font.setPointSizeF(font.pointSizeF() * 2);
                canvas->setFont(font);
            }
        }
    }

    void flipActionKeepsOrientationAndSchemaDraftAfterApplyFailure()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        registerDuplex(registry);
        LineflowEditorDialog dialog({}, registry, widgets, [](const auto&, const auto&, const auto&, QString* error) {
            if (error)
            {
                *error = "Synthetic persistence failure";
            }
            return false;
        });
        const auto           block = dialog.model().createBlock("Paired", 2);
        const auto           name  = dialog.model().insertNodeAt(block, 0, "fixture.duplex", "duplex");
        const auto           unary = dialog.model().insertNodeAt(block, 1, "native.gate", "rx");
        QVERIFY(!name.isEmpty() && !unary.isEmpty());
        dialog.show();
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        canvas->setCurrentIndex(dialog.model().index(0, 0));
        auto* form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(form && form->editor("prefix"));
        auto* edit = form->editor("prefix")->findChild<QLineEdit*>();
        QVERIFY(edit);
        edit->setText("pending-prefix");
        auto* flip = dialog.findChild<QAction*>("flipOperation");
        QVERIFY(flip && flip->isEnabled());
        flip->trigger();
        QVERIFY(flip->isChecked());
        QVERIFY(dialog.model().node(block, name).value("flipped").toBool());
        form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(form);
        QCOMPARE(form->editor("prefix")->value().toString(), QString("pending-prefix"));
        QString error;
        QVERIFY(!dialog.applyDraft(&error));
        QCOMPARE(error, QString("Synthetic persistence failure"));
        QVERIFY(flip->isChecked());
        QCOMPARE(dialog.model().node(block, name).value("configuration").toMap().value("prefix").toString(),
                 QString("pending-prefix"));
        canvas->setCurrentIndex(dialog.model().index(1, 0));
        QVERIFY(!flip->isEnabled());
        canvas->setCurrentIndex(dialog.model().index(0, 0));
        QVERIFY(flip->isChecked());
        flip->trigger();
        QVERIFY(!flip->isChecked());
        QVERIFY(!dialog.model().node(block, name).value("flipped").toBool());
    }

    void canvasFlipButtonSelectsAndTogglesOnlyItsDuplexNode()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        registerDuplex(registry);
        LineflowEditorDialog dialog({}, registry, widgets, LineflowEditorDialog::Apply {});
        const auto           block  = dialog.model().createBlock("Paired", 2);
        const auto           duplex = dialog.model().insertNodeAt(block, 0, "fixture.duplex", "duplex");
        const auto           unary  = dialog.model().insertNodeAt(block, 1, "native.gate", "rx");
        QVERIFY(!duplex.isEmpty() && !unary.isEmpty());
        dialog.show();
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        canvas->setCurrentIndex(dialog.model().index(1, 0));
        const auto configuration = dialog.model().node(block, duplex).value("configuration");
        const auto clickFlip     = [&] {
            const auto card       = canvas->visualRect(dialog.model().index(0, 0));
            const int  bodyHeight = qMax(62, 3 * canvas->fontMetrics().height() + 14);
            QTest::mouseClick(canvas->viewport(),
                              Qt::LeftButton,
                              Qt::NoModifier,
                              QPoint(card.right() - 17, card.top() + bodyHeight / 2));
        };
        clickFlip();
        QCOMPARE(canvas->currentIndex(), dialog.model().index(0, 0));
        QVERIFY(dialog.model().node(block, duplex).value("flipped").toBool());
        QCOMPARE(dialog.model().node(block, duplex).value("configuration"), configuration);
        QVERIFY(!dialog.model().node(block, unary).contains("flipped"));
        clickFlip();
        QVERIFY(!dialog.model().node(block, duplex).value("flipped").toBool());
        QCOMPARE(dialog.model().node(block, duplex).value("configuration"), configuration);
    }

    void unselectedCanvasCardsMaskWiresWithTransparentStyleColors()
    {
        const auto previous = QApplication::palette();
        const auto restore  = qScopeGuard([&] { QApplication::setPalette(previous); });
        auto       colors   = previous;
        colors.setColor(QPalette::Button, QColor(240, 245, 250, 0));
        colors.setColor(QPalette::AlternateBase, QColor(240, 245, 250, 0));
        QApplication::setPalette(colors);
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        LineflowEditorDialog dialog(
            {
                {"root", definition("root", "Opaque nodes", {operation("gate", "native.gate")})}
        },
            registry,
            widgets,
            LineflowEditorDialog::Apply {});
        dialog.show();
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        canvas->setCurrentIndex({});
        QTest::qWait(20);
        const auto   rect   = canvas->visualRect(dialog.model().index(0, 0));
        const auto   pixmap = canvas->viewport()->grab();
        const auto   image  = pixmap.toImage();
        const qreal  scale  = pixmap.devicePixelRatioF();
        const QPoint sample(qRound(rect.center().x() * scale), qRound((rect.top() + 4) * scale));
        const QPoint adjacent(qRound((rect.center().x() + 8) * scale), qRound((rect.top() + 4) * scale));
        QCOMPARE(image.pixelColor(sample), image.pixelColor(adjacent));
        QCOMPARE(image.pixelColor(sample).alpha(), 255);
        QVERIFY(image.pixelColor(sample) != colors.color(QPalette::Mid));
    }

    void selectedCanvasIconsKeepContrastAndCardsFollowFontMetrics()
    {
        const auto previous = QApplication::palette();
        const auto restore  = qScopeGuard([&] { QApplication::setPalette(previous); });
        auto       colors   = previous;
        colors.setColor(QPalette::Highlight, QColor(20, 45, 90));
        colors.setColor(QPalette::HighlightedText, Qt::white);
        colors.setColor(QPalette::ButtonText, Qt::black);
        QApplication::setPalette(colors);
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        registerDuplex(registry);
        LineflowEditorDialog dialog(
            {
                {"root", definition("root", "Font metrics", {operation("gate", "native.gate")})}
        },
            registry,
            widgets,
            LineflowEditorDialog::Apply {});
        QVERIFY(dialog.model().setBlockChannels("root", 2));
        QVERIFY(!dialog.model().insertNodeAt("root", 1, "fixture.duplex", "duplex").isEmpty());
        dialog.show();
        auto*      canvas        = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        const auto initialHeight = canvas->visualRect(dialog.model().index(0, 0)).height();
        QVERIFY(initialHeight >= 62);
        QVERIFY(initialHeight <= qMax(62, 3 * canvas->fontMetrics().height() + 14));
        QCOMPARE(canvas->visualRect(dialog.model().index(1, 0)).height(), initialHeight);
        for (int row = 0; row < 2; ++row)
        {
            const auto index = dialog.model().index(row, 0);
            canvas->setCurrentIndex(index);
            QTest::qWait(20);
            const auto  rect   = canvas->visualRect(index);
            const auto  pixmap = canvas->viewport()->grab();
            const auto  image  = pixmap.toImage();
            const qreal scale  = pixmap.devicePixelRatioF();
            const QRect iconRect(qRound((rect.left() + 10) * scale),
                                 qRound((rect.center().y() - 9) * scale),
                                 qRound(18 * scale),
                                 qRound(18 * scale));
            int         contrastPixels = 0;
            for (int y = iconRect.top(); y <= iconRect.bottom(); ++y)
            {
                for (int x = iconRect.left(); x <= iconRect.right(); ++x)
                {
                    const auto color = image.pixelColor(x, y);
                    if (color.red() < 150 && color.green() < 150 && color.blue() < 150)
                    {
                        ++contrastPixels;
                    }
                }
            }
            QVERIFY(contrastPixels > 5);
        }
        const auto index = dialog.model().index(0, 0);
        auto       font  = canvas->font();
        font.setPointSizeF(font.pointSizeF() * 2);
        canvas->setFont(font);
        QTRY_VERIFY(canvas->visualRect(index).height() >= 2 * canvas->fontMetrics().height());
        QVERIFY(canvas->visualRect(index).height() > initialHeight);
    }

    void canvasKeyboardSelectionAndDeletionPreserveLinkedDefinitions()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        LineflowEditorDialog dialog(fixtureDefinitions(), registry, widgets, LineflowEditorDialog::Apply {});
        dialog.show();
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        canvas->setCurrentIndex(dialog.model().index(0, 0));
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Down);
        QCOMPARE(canvas->currentIndex(), dialog.model().index(1, 0));
        QTest::keyClick(canvas, Qt::Key_Down);
        QVERIFY(canvas->currentIndex().data(LineflowEditorModel::LinkedRole).toBool());
        const auto before = dialog.model().definitions();
        QTest::keyClick(canvas, Qt::Key_Delete);
        QCOMPARE(dialog.model().definitions(), before);
        canvas->setCurrentIndex(dialog.model().index(0, 0));
        QTest::keyClick(canvas, Qt::Key_Delete);
        QCOMPARE(dialog.model().rowCount(), 2);
        QVERIFY(dialog.model().node("root", "decode").isEmpty());
        QVERIFY(!dialog.model().node("shared", "frame").isEmpty());
    }

    void variantNodeFormsAndRuntimeProjection()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        ProcessorCatalog::Type type;
        type.id             = "fixture.variant";
        type.title          = "Packet encoding";
        type.settingsSchema = settingsVariantFixture();
        // Keep the captured packet example compact; YAML draft retention is covered by SettingsFormTests.
        type.settingsSchema.replace(
            "        options: {type: object, title: Options, default: {}, x-widget: yaml, x-group: format, x-order: 2}\n",
            "");
        QVariantMap instantiated;
        type.create =
            [&instantiated](const QVariantMap& configuration, const auto&, QString*) -> std::unique_ptr<Processor> {
            instantiated = configuration;
            return {};
        };
        registry.registerType("fixture", std::move(type));
        QString    error;
        const auto variable = registry.resolveConfiguration("fixture.variant",
                                                            {
                                                                {"encoding", "varint"}
        },
                                                            {},
                                                            &error);
        QVERIFY2(variable.has_value(), qPrintable(error));
        QCOMPARE(variable->value("maxBytes").toInt(), 5);
        QVERIFY(!variable->contains("width") && !variable->contains("options"));
        const auto prepared = registry.prepare("fixture.variant", {}, {}, &error);
        QVERIFY2(prepared != nullptr, qPrintable(error));
        prepared->instantiate(
            {
                {"encoding", "varint"}
        },
            &error);
        QCOMPARE(instantiated.value("maxBytes").toInt(), 5);
        QVERIFY(!instantiated.contains("width"));
        const auto properties = registry.properties("fixture.variant",
                                                    {
        },
                                                    {{"encoding", "varint"}},
                                                    &error);
        QVERIFY2(properties.has_value(), qPrintable(error));
        QVERIFY(!properties->first().configuration.contains("width"));
        QCOMPARE(properties->first().configuration.value("maxBytes").toInt(), 5);

        const QVariantMap original {
            {"root",
             definition("root",
             "Variant settings", {operation("packet", "fixture.variant"), operation("other", "fixture.variant")})}
        };
        LineflowEditorDialog dialog(original, registry, widgets, [](const auto&, const auto&, const auto&, QString*) {
            return true;
        });
        dialog.setFixedSize(1100, 760);
        dialog.show();
        auto* host = dialog.findChild<QMainWindow*>("lineflowDockHost");
        auto* dock = dialog.findChild<QDockWidget*>("lineflowPropertiesDock");
        QVERIFY(host && dock);
        // Exercise the real resizable properties panel at both native scales.
        host->resizeDocks({dock}, {430}, Qt::Horizontal);
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        QVERIFY(canvas);
        canvas->setCurrentIndex(dialog.model().index(0, 0));
        auto* form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(form);
        auto* selector = form->editor("encoding")->findChild<QComboBox*>();
        QVERIFY(selector);
        QVERIFY(form->visibleFields().contains("width"));
        const auto destination = qEnvironmentVariable("TERMASTER_VARIANT_CAPTURE");
        if (!destination.isEmpty())
        {
            QVERIFY(QDir().mkpath(destination));
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            QTest::qWait(100);
            QVERIFY(dialog.grab().save(QDir(destination).filePath("Fixed.png")));
        }
        selector->setCurrentIndex(1);
        QVERIFY(form->visibleFields().contains("maxBytes"));
        QVERIFY(!form->visibleFields().contains("width"));
        if (!destination.isEmpty())
        {
            QTest::qWait(100);
            QVERIFY(dialog.grab().save(QDir(destination).filePath("Variable.png")));
        }
        canvas->setCurrentIndex(dialog.model().index(1, 0));
        canvas->setCurrentIndex(dialog.model().index(0, 0));
        form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QCOMPARE(form->editor("encoding")->value().toString(), QString("varint"));
        QVERIFY2(dialog.applyDraft(&error), qPrintable(error));
        const auto config = dialog.model().node("root", "packet").value("configuration").toMap();
        QCOMPARE(config.value("encoding").toString(), QString("varint"));
        QVERIFY(!config.contains("width") && !config.contains("options"));
    }

    void lengthVariantFormsAndRuntime()
    {
        const auto             destination = qEnvironmentVariable("TERMASTER_LENGTH_CAPTURE");
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        QVariantMap          saved;
        LineflowEditorDialog dialog(fixtureDefinitions(),
                                    registry,
                                    widgets,
                                    [&saved](const auto& definitions, const auto&, const auto&, QString*) {
                                        saved = definitions;
                                        return true;
                                    });
        dialog.setFixedSize(1100, 760);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        const auto block      = dialog.model().createBlock("Packet lengths", 1);
        const auto wrapName   = dialog.model().insertNodeAt(block, 0, "native.length-wrap", "rx");
        const auto unwrapName = dialog.model().insertNodeAt(block, 1, "native.length-unwrap", "rx");
        QVERIFY(!wrapName.isEmpty() && !unwrapName.isEmpty());
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        QVERIFY(canvas);
        if (!destination.isEmpty())
        {
            QVERIFY(QDir().mkpath(destination));
        }
        for (int row = 0; row < 2; ++row)
        {
            canvas->setCurrentIndex(dialog.model().index(row, 0));
            auto* form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
            QVERIFY(form);
            auto* selector = form->editor("encoding")->findChild<QComboBox*>();
            QVERIFY(selector);
            QCOMPARE(selector->count(), 2);
            QVERIFY(form->visibleFields().contains("width"));
            QVERIFY(!form->visibleFields().contains("maxBytes"));
            auto* width = form->editor("width")->findChild<QSpinBox*>();
            QVERIFY(width);
            QCOMPARE(width->minimum(), 1);
            QCOMPARE(width->maximum(), 8);
            width->setValue(3);
            const auto name  = row == 0 ? "LengthWrap" : "LengthUnwrap";
            const auto scale = dialog.devicePixelRatioF() > 1.5 ? "2x.png" : ".png";
            if (!destination.isEmpty())
            {
                QTest::qWait(100);
                QVERIFY(dialog.grab().save(QDir(destination).filePath(QString(name) + "Fixed" + scale)));
            }
            selector->setCurrentIndex(1);
            QVERIFY(form->visibleFields().contains("maxBytes"));
            QVERIFY(form->visibleFields().contains("includeField"));
            QVERIFY(form->visibleFields().contains("adjustment"));
            for (const auto* key : {"position", "width", "endianness"})
            {
                QVERIFY(!form->visibleFields().contains(key));
            }
            auto* maximum = form->editor("maxBytes")->findChild<QSpinBox*>();
            QVERIFY(maximum);
            QCOMPARE(maximum->value(), 10);
            maximum->setValue(4);
            selector->setCurrentIndex(0);
            QCOMPARE(width->value(), 3);
            selector->setCurrentIndex(1);
            QCOMPARE(maximum->value(), 4);
            if (!destination.isEmpty())
            {
                QTest::qWait(100);
                QVERIFY(dialog.grab().save(QDir(destination).filePath(QString(name) + "Varint" + scale)));
            }
        }
        // Re-selecting a node preserves its active variant, independently of the other node.
        canvas->setCurrentIndex(dialog.model().index(0, 0));
        auto* form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QCOMPARE(form->editor("encoding")->value().toString(), QString("varint"));
        QString error;
        QVERIFY2(dialog.applyDraft(&error), qPrintable(error));
        QVERIFY(!saved.isEmpty());
        for (const auto& name : {wrapName, unwrapName})
        {
            const auto config = dialog.model().node(block, name).value("configuration").toMap();
            QCOMPARE(config.value("encoding").toString(), QString("varint"));
            QCOMPARE(config.value("maxBytes").toInt(), 4);
            QVERIFY(!config.contains("width") && !config.contains("position") && !config.contains("endianness"));
            const auto id      = name == wrapName ? "native.length-wrap" : "native.length-unwrap";
            const auto runtime = registry.create(id, config, {}, &error);
            QVERIFY2(runtime != nullptr, qPrintable(error));
            QCOMPARE(runtime->configuration().value("maxBytes").toInt(), 4);
        }
    }
    void checksumVariantFormsAndRuntime()
    {
        const auto             destination = qEnvironmentVariable("TERMASTER_CHECKSUM_CAPTURE");
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        QVariantMap          saved;
        LineflowEditorDialog dialog(fixtureDefinitions(),
                                    registry,
                                    widgets,
                                    [&saved](const auto& definitions, const auto&, const auto&, QString*) {
                                        saved = definitions;
                                        return true;
                                    });
        dialog.setFixedSize(1100, 760);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto* host = dialog.findChild<QMainWindow*>("lineflowDockHost");
        auto* dock = dialog.findChild<QDockWidget*>("lineflowPropertiesDock");
        QVERIFY(host && dock);
        host->resizeDocks({dock}, {420}, Qt::Horizontal);
        const auto block      = dialog.model().createBlock("Packet checksums", 1);
        const auto wrapName   = dialog.model().insertNodeAt(block, 0, "native.checksum-wrap", "rx");
        const auto unwrapName = dialog.model().insertNodeAt(block, 1, "native.checksum-unwrap", "rx");
        QVERIFY(!wrapName.isEmpty() && !unwrapName.isEmpty());
        auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        QVERIFY(canvas);
        if (!destination.isEmpty())
        {
            QVERIFY(QDir().mkpath(destination));
        }
        for (int row = 0; row < 2; ++row)
        {
            canvas->setCurrentIndex(dialog.model().index(row, 0));
            auto* form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
            QVERIFY(form);
            auto* selector = form->editor("algorithm")->findChild<QComboBox*>();
            QVERIFY(selector);
            QCOMPARE(selector->count(), 8);
            const auto verifyResets = [&] {
                QCoreApplication::processEvents();
                for (const auto& key : form->visibleFields())
                {
                    auto* reset   = form->findChild<QToolButton*>("reset_" + key);
                    auto* control = form->editor(key)->primaryControl();
                    QVERIFY(reset && control);
                    QCOMPARE(reset->height(), SettingsFieldEditor::controlVisualHeight(control));
                }
            };
            verifyResets();
            QCOMPARE(selector->currentText(), QString("CRC-16/MODBUS"));
            QVERIFY(form->visibleFields().contains("position"));
            QVERIFY(form->visibleFields().contains("endianness"));
            QVERIFY(!form->visibleFields().contains("polynomial"));
            const auto scale = dialog.devicePixelRatioF() > 1.5 ? "2x.png" : ".png";
            if (row == 0 && !destination.isEmpty())
            {
                QTest::qWait(100);
                QVERIFY(dialog.grab().save(QDir(destination).filePath(QString("ChecksumWrapPreset") + scale)));
            }
            selector->setCurrentIndex(7);
            for (const auto* key : {"width", "polynomial", "init", "refin", "refout", "xorout"})
            {
                QVERIFY(form->visibleFields().contains(key));
            }
            auto* width = form->editor("width")->findChild<QComboBox*>();
            QVERIFY(width);
            width->setCurrentIndex(2);
            verifyResets();
            auto* polynomial = form->editor("polynomial")->findChild<QLineEdit*>();
            auto* init       = form->editor("init")->findChild<QLineEdit*>();
            auto* xorout     = form->editor("xorout")->findChild<QLineEdit*>();
            QVERIFY(polynomial && init && xorout);
            QCOMPARE(polynomial->text(), QString("0x8005"));
            QCOMPARE(init->text(), QString("0xFFFF"));
            QCOMPARE(xorout->text(), QString("0x0"));
            polynomial->setText("0x1EDC6F41");
            init->setText("0xFFFFFFFF");
            xorout->setText("0xFFFFFFFF");
            selector->setCurrentIndex(0);
            QVERIFY(!form->visibleFields().contains("polynomial"));
            QVERIFY(!form->visibleFields().contains("endianness"));
            selector->setCurrentIndex(7);
            QCOMPARE(polynomial->text(), QString("0x1EDC6F41"));
            QCOMPARE(init->text(), QString("0xFFFFFFFF"));
            QCOMPARE(xorout->text(), QString("0xFFFFFFFF"));
            QCOMPARE(width->currentIndex(), 2);
            if (row == 1 && !destination.isEmpty())
            {
                QTest::qWait(100);
                QVERIFY(dialog.grab().save(QDir(destination).filePath(QString("ChecksumUnwrapCustom") + scale)));
            }
            if (row == 0)
            {
                selector->setCurrentIndex(3);
            }
        }
        canvas->setCurrentIndex(dialog.model().index(0, 0));
        auto* form = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QCOMPARE(form->editor("algorithm")->value().toString(), QString("crc16-modbus"));
        QString error;
        QVERIFY2(dialog.applyDraft(&error), qPrintable(error));
        QVERIFY(!saved.isEmpty());
        const auto wrapConfig = dialog.model().node(block, wrapName).value("configuration").toMap();
        QCOMPARE(wrapConfig.value("algorithm").toString(), QString("crc16-modbus"));
        for (const auto* key : {"width", "polynomial", "init", "refin", "refout", "xorout"})
        {
            QVERIFY(!wrapConfig.contains(key));
        }
        const auto customConfig = dialog.model().node(block, unwrapName).value("configuration").toMap();
        QCOMPARE(customConfig.value("algorithm").toString(), QString("crc-custom"));
        QCOMPARE(customConfig.value("width").toInt(), 32);
        QCOMPARE(customConfig.value("init").toULongLong(), qulonglong(4294967295));
        const auto runtime = registry.create("native.checksum-unwrap", customConfig, {}, &error);
        QVERIFY2(runtime, qPrintable(error));
        QCOMPARE(runtime->configuration().value("xorout").toULongLong(), qulonglong(4294967295));
    }

    void visualCapture()
    {
        const auto destination = qEnvironmentVariable("TERMASTER_LINEFLOW_CAPTURE");
        if (destination.isEmpty())
        {
            QSKIP("Set TERMASTER_LINEFLOW_CAPTURE for editor visual acceptance");
        }
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        auto definitions = fixtureDefinitions();
        auto root        = definitions.value("root").toMap();
        auto source      = root.value("source").toMap();
        auto list        = source.value("nodes").toList();
        list.prepend(operation("encode", "native.hex-encoder"));
        source.insert("nodes", list);
        root.insert("source", source);
        root.insert("group", "Protocols");
        definitions.insert("root", root);
        LineflowEditorDialog dialog(definitions,
                                    registry,
                                    widgets,
                                    [](const auto&, const auto&, const auto&, QString*) { return true; });
        dialog.setFixedSize(1100, 760);
        dialog.show();
        auto* tree = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
        tree->setCurrentIndex(dialog.model().index(0, 0));
        QTest::qWait(100);
        auto* host       = dialog.findChild<QMainWindow*>("lineflowDockHost");
        auto* palette    = dialog.findChild<QDockWidget*>("lineflowPaletteDock");
        auto* properties = dialog.findChild<QDockWidget*>("lineflowPropertiesDock");
        QVERIFY(host && palette && properties);
        QVERIFY(!host->isWindow());
        QVERIFY(host->isVisibleTo(&dialog));
        QVERIFY(tree->isVisibleTo(&dialog));
        QVERIFY(palette->isVisibleTo(&dialog));
        QVERIFY(properties->isVisibleTo(&dialog));
        QVERIFY(host->height() > 300);
        QVERIFY(tree->viewport()->width() > 250);
        QVERIFY(tree->viewport()->height() > 250);
        const QRect dialogContent = dialog.rect();
        QVERIFY(dialogContent.contains(QRect(tree->mapTo(&dialog, QPoint()), tree->size())));
        QVERIFY(dialogContent.contains(QRect(palette->mapTo(&dialog, QPoint()), palette->size())));
        QVERIFY(dialogContent.contains(QRect(properties->mapTo(&dialog, QPoint()), properties->size())));
        QVERIFY(QDir().mkpath(destination));
        const auto path = QDir(destination)
                              .filePath(dialog.devicePixelRatioF() > 1.5 ? QString("Editor2x.png") :
                                                                           QString("Editor.png"));
        QVERIFY(dialog.grab().save(path));
        auto* selector = dialog.findChild<QComboBox*>("blockSelector");
        selector->setCurrentIndex(selector->findData("shared"));
        tree->setCurrentIndex(dialog.model().index(0, 0));
        QTest::qWait(50);
        const auto framerPath = QDir(destination)
                                    .filePath(dialog.devicePixelRatioF() > 1.5 ? QString("Framer2x.png") :
                                                                                 QString("Framer.png"));
        QVERIFY(dialog.grab().save(framerPath));
        auto* framerForm = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(framerForm && framerForm->editor("frameSize"));
        QCOMPARE(framerForm->editor("frameSize")->value().toInt(), 0);
        QVERIFY(!framerForm->editor("mode"));
        QVERIFY(!framerForm->editor("packetTail"));
        QVERIFY(!framerForm->editor("maxFrameSize"));
        const auto timed = dialog.model().createBlock("Idle-gap framing", 1);
        QVERIFY(!dialog.model().insertNodeAt(timed, 0, "native.timed-framer", "rx").isEmpty());
        tree->setCurrentIndex(dialog.model().index(0, 0));
        QTest::qWait(50);
        auto* timedForm = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(timedForm && timedForm->editor("gapMs"));
        QCOMPARE(timedForm->editor("gapMs")->value().toInt(), 20);
        QVERIFY(dialog.grab().save(
            QDir(destination).filePath(dialog.devicePixelRatioF() > 1.5 ? "TimedFramer2x.png" : "TimedFramer.png")));

        const auto patterned     = dialog.model().createBlock("Pattern framing", 1);
        const auto patternedName = dialog.model().insertNodeAt(patterned, 0, "native.pattern-framer", "rx");
        QVERIFY(!patternedName.isEmpty());
        QVERIFY(dialog.model().setNodeConfiguration(patterned,
                                                    patternedName,
                                                    {
                                                        {       "pattern", "<(?<header>[A-Z]):(?<payload>[^>]+)>"},
                                                        {"outputTemplate",   "${payload}abcdef${header}END\\r\\n"}
        }));
        tree->setCurrentIndex(dialog.model().index(0, 0));
        QTest::qWait(50);
        auto* patternForm = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(patternForm && patternForm->editor("outputTemplate"));
        QVERIFY(patternForm->editor("outputTemplate")->value().toString().startsWith("${payload}"));
        QVERIFY(dialog.grab().save(
            QDir(destination)
                .filePath(dialog.devicePixelRatioF() > 1.5 ? "PatternFramer2x.png" : "PatternFramer.png")));
        const auto timestamped = dialog.model().createBlock("Packet timestamps", 1);
        QVERIFY(!dialog.model().insertNodeAt(timestamped, 0, "native.timestamp", "rx").isEmpty());
        tree->setCurrentIndex(dialog.model().index(0, 0));
        QTest::qWait(50);
        auto* timestampForm = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(timestampForm && timestampForm->editor("template"));
        QCOMPARE(timestampForm->editor("template")->value().toString(), QString("[{datetime}] {packet}"));
        QVERIFY(dialog.grab().save(
            QDir(destination).filePath(dialog.devicePixelRatioF() > 1.5 ? "Timestamp2x.png" : "Timestamp.png")));
        auto* timestampTemplate = timestampForm->editor("template")->findChild<QLineEdit*>();
        QVERIFY(timestampTemplate);
        timestampTemplate->setText(R"(\e[38;2;255;160;70m[{datetime}]\e[0m {packet})");
        QTest::qWait(50);
        QVERIFY(dialog.grab().save(
            QDir(destination)
                .filePath(dialog.devicePixelRatioF() > 1.5 ? "TimestampColors2x.png" : "TimestampColors.png")));

        const auto compression = dialog.model().createBlock("Packet compression", 1);
        QVERIFY(!dialog.model().insertNodeAt(compression, 0, "native.compress", "rx").isEmpty());
        QVERIFY(!dialog.model().insertNodeAt(compression, 1, "native.uncompress", "rx").isEmpty());
        tree->setCurrentIndex(dialog.model().index(0, 0));
        QTest::qWait(50);
        auto* compressForm = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(compressForm && compressForm->editor("level"));
        QCOMPARE(compressForm->editor("level")->value().toInt(), -1);
        QVERIFY(dialog.grab().save(QDir(destination).filePath("Compress.png")));
        tree->setCurrentIndex(dialog.model().index(1, 0));
        QTest::qWait(50);
        auto* uncompressForm = dialog.findChild<SettingsFormWidget*>("nodeSettingsForm");
        QVERIFY(uncompressForm && uncompressForm->editor("maxOutputBytes"));
        QCOMPARE(uncompressForm->editor("format")->value().toString(), QString("auto"));
        QVERIFY(dialog.grab().save(QDir(destination).filePath("Uncompress.png")));

        registerDuplex(registry);
        const auto paired = dialog.model().createBlock("Device protocol · Rx / Tx", 2);
        QVERIFY(!paired.isEmpty());
        QVERIFY(!dialog.model().insertNodeAt(paired, 0, "native.hex-decoder", "rx").isEmpty());
        QVERIFY(!dialog.model().insertNodeAt(paired, 1, "fixture.duplex", "duplex").isEmpty());
        QVERIFY(!dialog.model().insertNodeAt(paired, 2, "native.pacer", "tx").isEmpty());
        tree->setCurrentIndex(dialog.model().index(2, 0));
        QTest::qWait(100);
        QVERIFY(!tree->dropIndicatorVisible());
        for (int row = 0; row < dialog.model().rowCount(); ++row)
        {
            const auto index = dialog.model().index(row, 0);
            const auto card  = tree->visualRect(index);
            for (const auto& port : tree->ports(index))
            {
                QVERIFY(card.adjusted(6, 6, -6, -6).contains(port.label));
                QCOMPARE(port.marker.center(), port.point);
                QVERIFY(card.intersects(port.marker));
            }
        }
        const auto pairedPath = QDir(destination)
                                    .filePath(dialog.devicePixelRatioF() > 1.5 ? "Paired2x.png" : "Paired.png");
        QVERIFY(dialog.grab().save(pairedPath));
        const auto duplexName = dialog.model().index(1, 0).data(LineflowEditorModel::NodeRole).toString();
        QVERIFY(dialog.model().setNodeFlipped(paired, duplexName, true));
        tree->setCurrentIndex(dialog.model().index(1, 0));
        QTest::qWait(100);
        auto* flip = dialog.findChild<QAction*>("flipOperation");
        QVERIFY(flip && flip->isChecked());
        const auto flippedPath = QDir(destination)
                                     .filePath(dialog.devicePixelRatioF() > 1.5 ? "Flipped2x.png" : "Flipped.png");
        QVERIFY(dialog.grab().save(flippedPath));
        QMimeData drag;
        drag.setData("application/x-termaster-processor", "native.gate");
        const QPoint position(tree->viewport()->width() / 4, tree->visualRect(dialog.model().index(1, 0)).top() - 12);
        QDragEnterEvent enter(position, Qt::CopyAction, &drag, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QVERIFY(tree->dropIndicatorVisible());
        const auto dragPath = QDir(destination)
                                  .filePath(dialog.devicePixelRatioF() > 1.5 ? "Dragging2x.png" : "Dragging.png");
        QVERIFY(dialog.grab().save(dragPath));
        QDragLeaveEvent leave;
        QApplication::sendEvent(tree->viewport(), &leave);
        QVERIFY(!tree->dropIndicatorVisible());
        const auto codecs = dialog.model().createBlock("Duplex codecs", 2);
        QVERIFY(!dialog.model().insertNodeAt(codecs, 0, "native.hex-codec", "duplex").isEmpty());
        QVERIFY(!dialog.model().insertNodeAt(codecs, 1, "native.base64-codec", "duplex").isEmpty());
        tree->setCurrentIndex(dialog.model().index(0, 0));
        QTest::qWait(50);
        QVERIFY(dialog.grab().save(
            QDir(destination).filePath(dialog.devicePixelRatioF() > 1.5 ? "HexCodec2x.png" : "HexCodec.png")));
        tree->setCurrentIndex(dialog.model().index(1, 0));
        QTest::qWait(50);
        QVERIFY(dialog.grab().save(
            QDir(destination).filePath(dialog.devicePixelRatioF() > 1.5 ? "Base64Codec2x.png" : "Base64Codec.png")));
        QCOMPARE(dialog.size(), QSize(1100, 760));
    }
};
QTEST_MAIN(LineflowEditorTests)
#include "LineflowEditorTests.moc"
