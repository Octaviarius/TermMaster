#include "Gui/Processing/LineflowEditorDialog.h"
#include "Gui/Processing/ProcessorCatalogTree.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeView>
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

    void viewportDropReordersInsertsAndRejectsCycle()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        LineflowEditorDialog dialog(fixtureDefinitions(), registry, widgets, LineflowEditorDialog::Apply {});
        dialog.show();
        auto* tree = dialog.findChild<QTreeView*>("nodeTree");
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
        auto*                tree = dialog.findChild<QTreeView*>("nodeTree");
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
        auto* tree = dialog.findChild<QTreeView*>("nodeTree");
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
        auto* tree   = dialog->findChild<QTreeView*>("nodeTree");
        tree->setCurrentIndex(dialog->model().index(0, 0));
        QVERIFY(
            dynamic_cast<CustomEditor*>(dialog->findChild<SettingsFormWidget*>("nodeSettingsForm")->editor("prefix")));
        QVERIFY_THROWS_EXCEPTION(std::logic_error, widgets.unregisterWidget("fixture.custom"));
        // App closes the editor before unregistering plugin factories.
        dialog.reset();
        QVERIFY(widgets.unregisterWidget("fixture.custom"));
        QVERIFY(registry.removeOwner("fixture"));
        LineflowEditorDialog unavailable(definitions, registry, widgets, LineflowEditorDialog::Apply {});
        tree = unavailable.findChild<QTreeView*>("nodeTree");
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
        auto*                tree = dialog.findChild<QTreeView*>("nodeTree");
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
        auto*                tree = dialog.findChild<QTreeView*>("nodeTree");
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
        auto* tree = dialog.findChild<QTreeView*>("nodeTree");
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

    void nodeNamesKeepMostTableSpaceAcrossModelResets()
    {
        ProcessorRegistry      registry;
        SettingsWidgetRegistry widgets;
        registerNativeProcessors(registry, "native");
        LineflowEditorDialog dialog(fixtureDefinitions(),
                                    registry,
                                    widgets,
                                    [](const auto&, const auto&, const auto&, QString*) { return true; });
        dialog.setFixedSize(1100, 760);
        dialog.show();
        auto* tree = dialog.findChild<QTreeView*>("nodeTree");
        QTRY_VERIFY(tree->columnWidth(0) > tree->columnWidth(1));
        tree->header()->resizeSection(1, 110);
        QCOMPARE(tree->columnWidth(1), 110);
        dialog.model().setSelectedBlock("shared");
        QTRY_COMPARE(tree->columnWidth(1), 110);
        QVERIFY(tree->columnWidth(0) > tree->columnWidth(1));
        QVERIFY(dialog.model().renameNode("shared", "frame", "Readable operation name ending with an identity"));
        QTRY_COMPARE(tree->columnWidth(1), 110);
        QVERIFY(tree->columnWidth(0) > tree->columnWidth(1));
        QVERIFY(dialog.model().index(0, 0).data(Qt::ToolTipRole).toString().contains("Readable operation name"));
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
        auto* tree = dialog.findChild<QTreeView*>("nodeTree");
        tree->setCurrentIndex(dialog.model().index(0, 0));
        QTest::qWait(100);
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
        QCOMPARE(dialog.size(), QSize(1100, 760));
    }
};
QTEST_MAIN(LineflowEditorTests)
#include "LineflowEditorTests.moc"
