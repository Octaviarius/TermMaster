#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFile>
#include <QFontDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QStandardItemModel>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <Termaster/Core/Gui/Settings/SettingsFormWidget.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <stdexcept>

namespace
{
std::shared_ptr<const SettingsSchema> schema(const QByteArray& properties, const QByteArray& extra = {})
{
    QByteArray yaml
        = "version: 1\ntype: termaster.settings-schema\ndescription: Test form\ncontent:\n  $schema: http://json-schema.org/draft-07/schema#\n  type: object\n  properties:\n";
    for (const auto& line : properties.split('\n'))
    {
        yaml += "    " + line + '\n';
    }
    yaml += extra;
    return std::make_shared<const SettingsSchema>(yaml);
}

class PortEditor final : public SettingsFieldEditor
{
public:
    explicit PortEditor(QWidget* parent) : SettingsFieldEditor(parent)
    {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* combo = new QComboBox(this);
        layout->addWidget(combo);
        auto* model = new QStandardItemModel(combo);
        auto* item  = new QStandardItem(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon),
                                        "Synthetic COM fixture");
        item->setData(QString("COM_TEST"), Qt::UserRole);
        model->appendRow(item);
        combo->setModel(model);
        _combo = combo;
        connect(combo, &QComboBox::currentIndexChanged, this, [this] { emit valueChanged(); });
    }
    QVariant value() const override
    {
        return _combo->currentData();
    }
    void setValue(const QVariant& value) override
    {
        _combo->setCurrentIndex(_combo->findData(value));
    }

private:
    QComboBox* _combo;
};
} // namespace

class SettingsFormTests : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void acceptingInheritedColorCreatesExplicitOverride()
    {
        QTemporaryDir directory;
        Settings      base(directory.filePath("base.yaml"));
        Settings      local(directory.filePath("local.yaml"));
        QVERIFY(base.setValue("fg", "#112233"));
        auto                   descriptor = schema("fg: {type: string, default: '#112233', x-widget: color}");
        SettingsFormModel      model(descriptor, SettingsOverlay(base.node(""), local.node("")));
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        QVERIFY(!model.hasOverride("fg"));
        QTimer::singleShot(0, &form, [] {
            auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->setCurrentColor(QColor("#112233"));
            dialog->accept();
        });
        form.editor("fg")->findChild<QToolButton*>("settingsPicker")->click();
        QVERIFY(model.hasOverride("fg"));
        QCOMPARE(model.value("fg").toString(), QString("#112233"));
        QVERIFY(!local.contains("fg"));
        model.removeValue("fg");
        QVERIFY(!model.hasOverride("fg"));
    }

    Q_SLOT void pickerSurvivesFormDestruction()
    {
        QTemporaryDir directory;
        Settings      owner(directory.filePath("settings.yaml"));
        for (const QString hint : {QString("fontFamily"), QString("color")})
        {
            auto descriptor = schema(("value: {type: string, default: '#112233', x-widget: " + hint + "}").toUtf8());
            SettingsFormModel            model(descriptor, owner.node(""));
            SettingsWidgetRegistry       registry;
            QPointer<SettingsFormWidget> form   = new SettingsFormWidget(model, registry);
            auto*                        picker = form->editor("value")->findChild<QToolButton*>("settingsPicker");
            QVERIFY(picker);
            QPointer<QDialog> activeDialog;
            QTimer::singleShot(0, this, [&form, &activeDialog] {
                activeDialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(activeDialog);
                delete form.data();
            });
            picker->click();
            QVERIFY(!form);
            QVERIFY(!activeDialog);
            QVERIFY(!model.isDirty());
            QVERIFY(!owner.contains("value"));
        }
    }

    Q_SLOT void appearancePickersAndDraftPreview()
    {
        QTemporaryDir directory;
        Settings      base(directory.filePath("base.yaml"));
        Settings      local(directory.filePath("local.yaml"));
        QVERIFY(base.setValue("appearance/fontSize", 12));
        SettingsOverlay overlay(base.node(""), local.node(""));
        auto            descriptor = schema(
            "family: {type: string, default: Consolas, x-widget: fontFamily, x-settings-path: appearance/fontFamily}\n"
            "size: {type: number, default: 12, x-settings-path: appearance/fontSize}\n"
            "fg: {type: string, default: '#eeeeee', x-widget: color, x-settings-path: appearance/foreground}\n"
            "bg: {type: string, default: '#111111', x-widget: color, x-settings-path: appearance/background}",
            "  x-widget: terminalPreview\n  x-widget-options: {title: Draft preview}\n");
        QCOMPARE(descriptor->widget(), QString("terminalPreview"));
        QCOMPARE(descriptor->presentation().value("title").toString(), QString("Draft preview"));
        auto plainDescriptor = schema(
            "family: {type: string, default: Consolas, x-settings-path: appearance/fontFamily}\n"
            "size: {type: number, default: 12, x-settings-path: appearance/fontSize}\n"
            "fg: {type: string, default: '#eeeeee', x-settings-path: appearance/foreground}\n"
            "bg: {type: string, default: '#111111', x-settings-path: appearance/background}");
        SettingsFormModel      plainModel(plainDescriptor, overlay);
        SettingsWidgetRegistry plainRegistry;
        SettingsFormWidget     plainForm(plainModel, plainRegistry);
        QVERIFY(!plainForm.findChild<TerminalDisplay*>());
        SettingsFormModel      model(descriptor, overlay);
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        auto*                  preview = form.findChild<TerminalDisplay*>("settingsAppearancePreview");
        QVERIFY(preview);
        QCOMPARE(preview->focusPolicy(), Qt::NoFocus);
        QCOMPARE(preview->font().pointSizeF(), 12.0);
        model.setValue("size", 20);
        QCOMPARE(preview->font().pointSizeF(), 20.0);
        QVERIFY(!local.contains("appearance/fontSize"));
        model.removeValue("size");
        QCOMPARE(preview->font().pointSizeF(), 12.0);
        for (const QString key : {QString("family"), QString("fg")})
        {
            auto*      editor = form.editor(key);
            const auto before = model.value(key);
            QTimer::singleShot(0, &form, [] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                dialog->reject();
            });
            editor->findChild<QToolButton*>("settingsPicker")->click();
            QCOMPARE(model.value(key), before);
        }
        QTimer::singleShot(0, &form, [] {
            auto* dialog = qobject_cast<QFontDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QFont selected;
            selected.setFamily("Courier New");
            dialog->setCurrentFont(selected);
            dialog->accept();
        });
        form.editor("family")->findChild<QToolButton*>("settingsPicker")->click();
        QVERIFY(!model.value("family").toString().isEmpty());
        QCOMPARE(preview->font().family(), model.value("family").toString());
        QVERIFY(!local.contains("appearance/fontFamily"));
        QTimer::singleShot(0, &form, [] {
            auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->setCurrentColor(QColor("#123456"));
            dialog->accept();
        });
        form.editor("fg")->findChild<QToolButton*>("settingsPicker")->click();
        QCOMPARE(model.value("fg").toString(), "#123456");
        QVERIFY(!local.contains("appearance/foreground"));
        QVERIFY(model.apply());
        QCOMPARE(local.value("appearance/foreground").toString(), "#123456");
    }

    Q_SLOT void defaultsCancelAndAtomicApply()
    {
        QTemporaryDir directory;
        const auto    path = directory.filePath("settings.yaml");
        Settings      settings(path);
        QVERIFY(settings.setValue("plugin/unknown", "preserved"));
        auto descriptor = schema(
            "name: {type: string, default: initial, x-settings-path: nested/name}\ncount: {type: integer, minimum: 1, default: 2}");
        SettingsFormModel model(descriptor, settings.node("plugin"));
        QVERIFY(!settings.contains("plugin/nested/name"));
        QVERIFY(!model.isDirty());
        QCOMPARE(model.value("name").toString(), "initial");
        model.setValue("name", "discarded");
        model.reload();
        QCOMPARE(model.value("name").toString(), "initial");
        model.setValue("count", 0);
        QString error;
        QVERIFY(!model.apply(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!settings.contains("plugin/nested/name"));
        model.setValue("count", 5);
        QVERIFY(model.apply(&error));
        QCOMPARE(settings.value("plugin/count").toLongLong(), 5);
        QCOMPARE(settings.value("plugin/nested/name").toString(), "initial");
        QCOMPARE(settings.value("plugin/unknown").toString(), "preserved");
        QVERIFY(!QFile::exists(path));
    }

    Q_SLOT void staleScopeAndConcurrentEdits()
    {
        QTemporaryDir directory;
        auto          owner = std::make_unique<Settings>(directory.filePath("settings.yaml"));
        QVERIFY(owner->setValue("a", "first"));
        QVERIFY(owner->setValue("b", "first"));
        auto              descriptor = schema("a: {type: string}\nb: {type: string}");
        SettingsFormModel model(descriptor, owner->node(""));
        model.setValue("a", "draft");
        QVERIFY(owner->setValue("b", "external"));
        QVERIFY(model.apply());
        QCOMPARE(owner->value("b").toString(), "external");
        model.setValue("a", "next");
        QVERIFY(owner->setValue("a", "conflict"));
        QString error;
        QVERIFY(!model.apply(&error));
        QCOMPARE(owner->value("a").toString(), "conflict");
        owner.reset();
        QVERIFY(!model.apply(&error));
        QVERIFY(!error.isEmpty());
    }

    Q_SLOT void optionalReadOnlyAndPrecision()
    {
        QTemporaryDir directory;
        Settings      owner(directory.filePath("settings.yaml"));
        QVERIFY(owner.setValue("locked", "fixed"));
        auto descriptor = schema(
            "locked: {type: string, readOnly: true}\noptional: {}\nlarge: {type: integer, default: 9007199254740991}\nnumber: {type: number, default: 1.5}");
        SettingsFormModel      model(descriptor, owner.node(""));
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        model.setValue("locked", "changed");
        QCOMPARE(model.value("locked").toString(), "fixed");
        QVERIFY(!form.editor("locked")->isEnabled());
        model.setValue("optional", QVariant());
        QVERIFY(model.contains("optional"));
        QVERIFY(model.apply());
        QVERIFY(owner.contains("optional"));
        QVERIFY(!owner.value("optional").isValid());
        model.removeValue("optional");
        QVERIFY(model.apply());
        QVERIFY(!owner.contains("optional"));
        QVERIFY(model.apply());
        auto* line = form.editor("large")->findChild<QLineEdit*>();
        QVERIFY(line);
        line->setText("18446744073709551615");
        QVERIFY(!model.isValid());
        QVERIFY(!model.apply());
        QCOMPARE(owner.value("large").toLongLong(), qlonglong(9007199254740991LL));
        line->setText("9007199254740991");
        QVERIFY(model.isValid());
        QVERIFY(model.apply());
        QCOMPARE(owner.value("large").toLongLong(), qlonglong(9007199254740991LL));
        line->setText("18446744073709551616");
        QVERIFY(!model.isValid());
        QVERIFY(!model.apply());
        QCOMPARE(owner.value("large").toLongLong(), qlonglong(9007199254740991LL));
        line->setText("123");
        QVERIFY(model.isValid());
        form.editor("number")->findChild<QLineEdit*>()->setText("nan");
        QVERIFY(!model.isValid());
    }

    Q_SLOT void typedDraftAndConcurrentChanges()
    {
        QTemporaryDir directory;
        Settings      owner(directory.filePath("settings.yaml"));
        QVERIFY(owner.setValue("value", 1));
        auto              descriptor = schema("value: {enum: [1, '1', false, 0]}");
        SettingsFormModel model(descriptor, owner.node(""));
        model.setValue("value", QString("1"));
        QVERIFY(model.isDirty());
        QVERIFY(model.apply());
        QCOMPARE(owner.value("value").typeId(), QMetaType::QString);
        model.setValue("value", false);
        QVERIFY(owner.setValue("value", 1));
        QVERIFY(!model.apply());
        QCOMPARE(owner.value("value").toInt(), 1);
    }

    Q_SLOT void overlayAndInvalidOptionalRemoval()
    {
        QTemporaryDir directory;
        Settings      base(directory.filePath("base.yaml"));
        Settings      overrides(directory.filePath("overrides.yaml"));
        QVERIFY(base.setValue("name", "inherited"));
        QVERIFY(base.setValue("unknown", "keep"));
        SettingsOverlay        overlay(base.node(""), overrides.node(""));
        auto                   descriptor = schema("name: {type: string}\ncount: {type: integer}");
        SettingsFormModel      model(descriptor, overlay);
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        model.setValue("name", "override");
        QVERIFY(model.apply());
        QCOMPARE(base.value("name").toString(), "inherited");
        QCOMPARE(overrides.value("name").toString(), "override");
        QVERIFY(!overrides.contains("unknown"));
        model.setValue("count", 3);
        auto* line = form.editor("count")->findChild<QLineEdit*>();
        QVERIFY(line);
        line->setText("invalid");
        QVERIFY(!model.isValid());
        model.removeValue("count");
        QVERIFY(model.isValid());
        QVERIFY(model.apply());
        QVERIFY(!overrides.contains("count"));
    }

    Q_SLOT void overlayRemovalValidatesInheritedValue()
    {
        QTemporaryDir directory;
        Settings      base(directory.filePath("base.yaml"));
        Settings      overrides(directory.filePath("overrides.yaml"));
        QVERIFY(base.setValue("scope/count", 0));
        QVERIFY(overrides.setValue("scope/count", 3));
        SettingsOverlay   overlay(base.node(""), overrides.node(""));
        auto              descriptor = schema("count: {type: integer, minimum: 1}");
        SettingsFormModel model(descriptor, overlay.node("scope"));
        model.removeValue("count");
        QCOMPARE(model.value("count").toInt(), 0);
        QVERIFY(!model.isValid());
        QVERIFY(!model.apply());
        QCOMPARE(overrides.value("scope/count").toInt(), 3);
        QCOMPARE(base.value("scope/count").toInt(), 0);
        QVERIFY(base.setValue("scope/count", 4));
        model.removeValue("count");
        QVERIFY(model.isValid());
        QVERIFY(model.apply());
        QVERIFY(!overrides.contains("scope/count"));
        QCOMPARE(overlay.value("scope/count").toInt(), 4);
    }

    Q_SLOT void fourLayerExplicitFontInheritance()
    {
        QTemporaryDir directory;
        Settings      defaults(directory.filePath("defaults.yaml"));
        Settings      workspace(directory.filePath("workspace.yaml"));
        Settings      session(directory.filePath("session.yaml"));
        Settings      terminal(directory.filePath("terminal.yaml"));
        QVERIFY(defaults.setValue("appearance/fontSize", 12));
        QVERIFY(workspace.setValue("appearance/fontSize", 14));
        QVERIFY(session.setValue("appearance/fontSize", 16));
        SettingsOverlay workspaceView(defaults.node(""), workspace.node(""));
        SettingsOverlay sessionView(workspaceView, session.node(""));
        SettingsOverlay terminalView(sessionView, terminal.node(""));
        auto descriptor = schema("fontSize: {type: integer, minimum: 6, default: 12}", "  required: [fontSize]\n");
        SettingsFormModel      model(descriptor, terminalView.node("appearance"));
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        QVERIFY(model.supportsInheritance());
        QVERIFY(!model.hasOverride("fontSize"));
        QCOMPARE(model.value("fontSize").toInt(), 16);
        QVERIFY(form.editor("fontSize")->isEnabled());
        QVERIFY(form.editor("fontSize")->property("settingsInherited").toBool());
        QVERIFY(form.findChildren<QCheckBox*>().isEmpty());
        auto* reset = form.findChild<QToolButton*>("reset_fontSize");
        QVERIFY(reset);
        QVERIFY(!reset->icon().pixmap(16, 16).isNull());
        QVERIFY(reset->isHidden());
        QVERIFY(reset->toolTip().contains("16"));
        const auto    inheritedTextColor = form.editor("fontSize")->palette().color(QPalette::Text);
        const QString capture            = qEnvironmentVariable("TERMASTER_SETTINGS_INHERITANCE_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            form.resize(640, 220);
            form.show();
            QTest::qWait(50);
            QVERIFY(form.grab().save(capture + ".inherited.png"));
        }
        auto* input = form.editor("fontSize")->findChild<QLineEdit*>();
        QVERIFY(input);
        input->setText("17");
        QVERIFY(reset->isEnabled());
        QVERIFY(!reset->isHidden());
        QCOMPARE(form.editor("fontSize")->palette().color(QPalette::Text), inheritedTextColor);
        QVERIFY(!form.editor("fontSize")->property("settingsInherited").toBool());
        if (!capture.isEmpty())
        {
            QTest::qWait(50);
            QVERIFY(form.grab().save(capture + ".override.png"));
        }
        input->setText("16"); // Explicit edit back to inherited value still owns the value.
        QVERIFY(model.hasOverride("fontSize"));
        QVERIFY(model.isDirty());
        QVERIFY(model.apply());
        QVERIFY(terminalView.node("appearance").containsOverride("fontSize"));
        QCOMPARE(terminal.value("appearance/fontSize").toInt(), 16);
        QVERIFY(session.setValue("appearance/fontSize", 18));
        reset->click();
        QVERIFY(reset->isHidden());
        QCOMPARE(form.editor("fontSize")->palette().color(QPalette::Text), inheritedTextColor);
        QCOMPARE(model.value("fontSize").toInt(), 18);
        QVERIFY(model.isValid());
        QVERIFY(model.apply());
        QVERIFY(!terminal.contains("appearance/fontSize"));
        QCOMPARE(terminalView.value("appearance/fontSize").toInt(), 18);
        QVERIFY(!terminalView.node("appearance").containsOverride("../fontSize"));
        QVERIFY(!terminalView.node("appearance").containsOverride(""));
    }

    Q_SLOT void requiredInheritanceValidationAndOwnershipConflict()
    {
        QTemporaryDir directory;
        Settings      base(directory.filePath("base.yaml"));
        Settings      local(directory.filePath("local.yaml"));
        QVERIFY(base.setValue("fontSize", 0));
        QVERIFY(local.setValue("fontSize", 12));
        SettingsOverlay overlay(base.node(""), local.node(""));
        auto descriptor = schema("fontSize: {type: integer, minimum: 6, default: 12}", "  required: [fontSize]\n");
        SettingsFormModel model(descriptor, overlay);
        model.removeValue("fontSize");
        QVERIFY(!model.isValid());
        QVERIFY(!model.apply());
        QCOMPARE(local.value("fontSize").toInt(), 12);
        QVERIFY(base.setValue("fontSize", 12));
        model.reload();
        QVERIFY(local.remove("fontSize"));
        model.setValue("fontSize", 14);
        QVERIFY(!model.apply()); // Effective value stayed 12; ownership changed.
        model.reload();
        model.setValue("fontSize", 12);
        QVERIFY(local.setValue("fontSize", 12));
        QVERIFY(!model.apply());
        model.reload();
        QVERIFY(base.remove("fontSize"));
        model.removeValue("fontSize");
        QVERIFY(!model.hasOverride("fontSize")); // Descriptor default is a valid reset target.
        QCOMPARE(model.value("fontSize").toInt(), 12);
        QVERIFY(model.apply());
        QVERIFY(!local.contains("fontSize"));
        auto noDefault = schema("fontSize: {type: integer, minimum: 6}", "  required: [fontSize]\n");
        QVERIFY(local.setValue("fontSize", 14));
        SettingsFormModel noFallback(noDefault, overlay);
        QVERIFY(!noFallback.canReset("fontSize"));
        noFallback.removeValue("fontSize");
        QVERIFY(noFallback.hasOverride("fontSize"));
    }

    Q_SLOT void overlayDefaultsDoNotCreateOverrides()
    {
        QTemporaryDir     directory;
        Settings          base(directory.filePath("base.yaml"));
        Settings          local(directory.filePath("local.yaml"));
        SettingsOverlay   overlay(base.node(""), local.node(""));
        auto              descriptor = schema("fontSize: {type: integer, default: 12}", "  required: [fontSize]\n");
        SettingsFormModel model(descriptor, overlay);
        QVERIFY(!model.hasOverride("fontSize"));
        QVERIFY(!model.isDirty());
        QVERIFY(model.apply());
        QVERIFY(!local.contains("fontSize"));
        QVERIFY(!base.contains("fontSize"));
        model.setValue("fontSize", model.value("fontSize"));
        QVERIFY(model.hasOverride("fontSize"));
        QVERIFY(model.apply());
        QCOMPARE(local.value("fontSize").toInt(), 12);
        model.removeValue("fontSize");
        QVERIFY(model.apply());
        QVERIFY(!local.contains("fontSize"));
        model.setValue("fontSize", 13);
        model.removeValue("fontSize");
        QVERIFY(!model.isDirty());
        QCOMPARE(model.value("fontSize").toInt(), 12);
    }

    Q_SLOT void unionUsesTypedYamlInput()
    {
        QTemporaryDir          directory;
        Settings               owner(directory.filePath("settings.yaml"));
        auto                   descriptor = schema("value: {type: [integer, 'null'], default: null}");
        SettingsFormModel      model(descriptor, owner.node(""));
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        auto*                  text = form.editor("value")->findChild<QPlainTextEdit*>();
        QVERIFY(text);
        text->setPlainText("42");
        QVERIFY(model.isValid());
        QVERIFY(model.apply());
        QCOMPARE(owner.value("value").toLongLong(), 42);
        text->setPlainText("null");
        QVERIFY(model.isValid());
        QVERIFY(model.apply());
        QVERIFY(owner.contains("value"));
        QVERIFY(!owner.value("value").isValid());
    }

    Q_SLOT void searchPreservesDraftAndValidation()
    {
        QTemporaryDir directory;
        Settings      owner(directory.filePath("settings.yaml"));
        auto          descriptor = schema(
            "count: {type: integer, default: 3, title: Quantity, x-group: connection, x-settings-path: serial/rate}\n"
            "name: {type: string, default: initial, title: Display, description: Unicode Ωμέγα, x-group: connection}\n"
            "other: {type: string, default: other, x-group: appearance}",
            "  x-groups:\n    - {id: connection, title: Transport}\n    - {id: appearance, title: Appearance}\n");
        SettingsFormModel      model(descriptor, owner.node(""));
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        QCOMPARE(form.visibleFields(), QStringList({"count", "name", "other"}));
        form.setFilter("  TRANSPORT  ");
        QCOMPARE(form.visibleFields(), QStringList({"count", "name"}));
        form.setFilter("connection");
        QCOMPARE(form.visibleFields(), QStringList({"count", "name"}));
        form.setFilter("SERIAL/RATE");
        QCOMPARE(form.visibleFields(), QStringList({"count"}));
        form.setFilter(QString::fromUtf8("ωΜΈΓΑ"));
        QCOMPARE(form.visibleFields(), QStringList({"name"}));
        form.setFilter("quantity");
        QCOMPARE(form.visibleFields(), QStringList({"count"}));
        model.setValue("name", "draft");
        auto* line = form.editor("count")->findChild<QLineEdit*>();
        QVERIFY(line);
        line->setText("invalid");
        QVERIFY(!model.isValid());
        QVERIFY(model.isDirty());
        const auto errorCount = model.errors().size();
        form.setFilter("no matching text");
        QVERIFY(!form.hasMatches());
        QVERIFY(form.visibleFields().isEmpty());
        QVERIFY(form.editor("count")->isHidden() || form.editor("count")->parentWidget()->isHidden());
        QCOMPARE(line->text(), "invalid");
        QCOMPARE(model.errors().size(), errorCount);
        QVERIFY(!model.isValid());
        QVERIFY(model.isDirty());
        QCOMPARE(model.value("name").toString(), "draft");
        form.setFilter("");
        QVERIFY(form.hasMatches());
        QCOMPARE(form.visibleFields(), QStringList({"count", "name", "other"}));
        QCOMPARE(line->text(), "invalid");
        QVERIFY(!model.isValid());
        QVERIFY(!model.apply());
    }

    Q_SLOT void customRegistryLifecycleAndCapture()
    {
        SettingsWidgetRegistry registry;
        registry.registerWidget("fixturePort",
                                [](const SettingsSchema::Field&, QWidget* parent) { return new PortEditor(parent); });
        QVERIFY_EXCEPTION_THROWN(registry.registerWidget("fixturePort",
                                                         [](const auto&, QWidget*) -> SettingsFieldEditor* {
                                                             return nullptr;
                                                         }),
                                 std::invalid_argument);
        QTemporaryDir directory;
        Settings      owner(directory.filePath("settings.yaml"));
        auto          descriptor = schema(
            "port:\n  type: string\n  title: Synthetic serial port\n  default: COM_TEST\n  x-widget: fixturePort\nname:\n  type: string\n  title: Display name\n  description: Unicode display name for this synthetic configuration.\n  default: 'Terminal О©'\nenabled: {type: boolean, default: true}\nmode: {enum: [1, '1'], default: 1}");
        SettingsFormModel model(descriptor, owner.node(""));
        {
            SettingsFormWidget form(model, registry);
            QVERIFY(form.editor("port"));
            QVERIFY_EXCEPTION_THROWN(registry.unregisterWidget("fixturePort"), std::logic_error);
            auto* combo = form.editor("mode")->findChild<QComboBox*>();
            QVERIFY(combo);
            combo->setCurrentIndex(1);
            QCOMPARE(model.value("mode").typeId(), QMetaType::QString);
            form.resize(640, 620);
            form.show();
            QTest::qWait(30);
            const auto capture = qEnvironmentVariable("TERMASTER_SETTINGS_GUI_CAPTURE");
            if (!capture.isEmpty())
            {
                QVERIFY(form.grab().save(capture));
            }
        }
        QVERIFY(registry.unregisterWidget("fixturePort"));
        QVERIFY(!registry.unregisterWidget("fixturePort"));
        SettingsSchema::Field field;
        field.widget = "unknown";
        QVERIFY_EXCEPTION_THROWN(registry.create(field, nullptr), std::invalid_argument);
    }
};
QTEST_MAIN(SettingsFormTests)
#include "SettingsFormTests.moc"
