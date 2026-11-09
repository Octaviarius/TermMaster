#include "Gui/Processing/LineflowCanvas.h"
#include "Gui/Processing/LineflowEditorDialog.h"
#include "Gui/Processing/ProcessorCatalogTree.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
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
#include <QSignalBlocker>
#include <QSignalSpy>
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
    duplex.settingsSchema = QByteArray(
        "version: 1\ntype: termaster.settings-schema\ndescription: Duplex fixture\ncontent:\n  type: object\n  properties: {}\n");
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
        QCOMPARE(dialog.model().index(2, 0).data(LineflowEditorModel::NodeRole).toString(), QString("operation"));
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

    void unselectedCanvasCardsMaskWiresWithTransparentStyleColors()
    {
        const auto   previous = QApplication::palette();
        const auto   restore  = qScopeGuard([&] { QApplication::setPalette(previous); });
        auto         colors   = previous;
        const QColor cardColor(240, 245, 250);
        colors.setColor(QPalette::Button, QColor(240, 245, 250, 0));
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
        const QPoint sample(qRound(rect.center().x() * scale), qRound((rect.bottom() - 8) * scale));
        QCOMPARE(image.pixelColor(sample), cardColor);
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
                                 qRound((rect.top() + 12) * scale),
                                 qRound(18 * scale),
                                 qRound(18 * scale));
            int         lightPixels = 0;
            for (int y = iconRect.top(); y <= iconRect.bottom(); ++y)
            {
                for (int x = iconRect.left(); x <= iconRect.right(); ++x)
                {
                    const auto color = image.pixelColor(x, y);
                    if (color.red() > 200 && color.green() > 200 && color.blue() > 200)
                    {
                        ++lightPixels;
                    }
                }
            }
            QVERIFY(lightPixels > 5);
        }
        const auto index = dialog.model().index(0, 0);
        auto       font  = canvas->font();
        font.setPointSizeF(font.pointSizeF() * 2);
        canvas->setFont(font);
        QTRY_VERIFY(canvas->visualRect(index).height() >= 2 * canvas->fontMetrics().height() + 24);
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
        registerDuplex(registry);
        const auto paired = dialog.model().createBlock("Device protocol · Rx / Tx", 2);
        QVERIFY(!paired.isEmpty());
        QVERIFY(!dialog.model().insertNodeAt(paired, 0, "native.hex-decoder", "rx").isEmpty());
        QVERIFY(!dialog.model().insertNodeAt(paired, 1, "fixture.duplex", "duplex").isEmpty());
        QVERIFY(!dialog.model().insertNodeAt(paired, 2, "native.pacer", "tx").isEmpty());
        tree->setCurrentIndex(dialog.model().index(2, 0));
        QTest::qWait(100);
        QVERIFY(!tree->dropIndicatorVisible());
        const auto pairedPath = QDir(destination)
                                    .filePath(dialog.devicePixelRatioF() > 1.5 ? "Paired2x.png" : "Paired.png");
        QVERIFY(dialog.grab().save(pairedPath));
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
        QCOMPARE(dialog.size(), QSize(1100, 760));
    }
};
QTEST_MAIN(LineflowEditorTests)
#include "LineflowEditorTests.moc"
