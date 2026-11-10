#include "../SettingsVariantFixture.h"

#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QAccessible>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <Termaster/Core/Gui/Settings/SettingsFormWidget.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
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
    Q_SLOT void initTestCase()
    {
#ifdef Q_OS_WIN
        // The offscreen plugin has no system font discovery; use installed, bounded fixtures.
        if (QFontDatabase::families().isEmpty())
        {
            const QString root = qEnvironmentVariable("SystemRoot");
            QVERIFY(QFontDatabase::addApplicationFont(root + "/Fonts/consola.ttf") >= 0);
            QVERIFY(QFontDatabase::addApplicationFont(root + "/Fonts/arial.ttf") >= 0);
        }
#endif
    }

    Q_SLOT void variantDraftsValidationAndAtomicApply()
    {
        Settings settings(QString {});
        QVERIFY(settings.setValue("future/retained", 42));
        auto                   descriptor = std::make_shared<const SettingsSchema>(settingsVariantFixture());
        SettingsFormModel      model(descriptor, settings.node(""));
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        form.resize(700, 500);
        form.show();
        auto* selector = form.editor("encoding")->findChild<QComboBox*>();
        QVERIFY(selector);
        QCOMPARE(selector->itemText(0), QString("Fixed width"));
        QCOMPARE(selector->itemText(1), QString("Variable width"));
        QVERIFY(form.visibleFields().contains("width"));
        QVERIFY(!form.visibleFields().contains("maxBytes"));
        auto* options = form.editor("options")->findChild<QPlainTextEdit*>();
        QVERIFY(options);
        options->setPlainText("[");
        QVERIFY(!model.isValid());
        selector->setCurrentIndex(1);
        QVERIFY(model.isValid());
        QVERIFY(form.visibleFields().contains("maxBytes"));
        QVERIFY(!form.visibleFields().contains("width"));
        form.setFilter("Width");
        QVERIFY(!form.visibleFields().contains("width"));
        form.setFilter({});
        selector->setCurrentIndex(0);
        QCOMPARE(options->toPlainText(), QString("["));
        QVERIFY(!model.isValid());
        options->setPlainText("{enabled: true}");
        model.setValue("width", 8);
        selector->setCurrentIndex(1);
        selector->setCurrentIndex(0);
        QCOMPARE(model.value("width").toInt(), 8);
        QCOMPARE(options->toPlainText(), QString("{enabled: true}"));
        QString error;
        QVERIFY2(model.apply(&error), qPrintable(error));
        QCOMPARE(settings.value("width").toInt(), 8);
        selector->setCurrentIndex(1);
        model.setValue("maxBytes", 9);
        QVERIFY2(model.apply(&error), qPrintable(error));
        QCOMPARE(settings.value("encoding").toString(), QString("varint"));
        QCOMPARE(settings.value("maxBytes").toInt(), 9);
        QVERIFY(!settings.contains("width") && !settings.contains("options"));
        QCOMPARE(settings.value("future/retained").toInt(), 42);
        QVERIFY(!model.isDirty());
        QVERIFY(model.isValid());
        QVERIFY(settings.setValue("encoding", "unknown"));
        model.reload();
        QCOMPARE(model.value("encoding").toString(), QString("unknown"));
        QCOMPARE(form.editor("encoding")->value().toString(), QString("unknown"));
        QVERIFY(!model.isDirty() && !model.isValid());
        QVERIFY(!model.isActive("width") && !model.isActive("maxBytes"));
    }

    Q_SLOT void inactiveReadOnlyValuesAreRemovedAtomically()
    {
        Settings settings(QString {});
        QVERIFY(settings.setValue("encoding", "fixed"));
        QVERIFY(settings.setValue("width", 3));
        QVERIFY(settings.setValue("options",
                                  QVariantMap {
                                      {"retained", true}
        }));
        auto bytes = settingsVariantFixture();
        bytes.replace("type: object, title: Options,", "type: object, title: Options, readOnly: true,");
        auto              descriptor = std::make_shared<const SettingsSchema>(bytes);
        SettingsFormModel model(descriptor, settings.node(""));
        model.setValue("options", QVariantMap {});
        QCOMPARE(model.value("options").toMap().value("retained").toBool(), true);
        model.setValue("encoding", "varint");
        QString error;
        QVERIFY2(model.apply(&error), qPrintable(error));
        QVERIFY(!settings.contains("options"));
        QVERIFY(!settings.contains("width"));
    }

    Q_SLOT void inactiveVariantKeepsCustomEditorLease()
    {
        Settings settings(QString {});
        auto     bytes = settingsVariantFixture();
        bytes.replace("x-widget: yaml", "x-widget: fixture.variant");
        auto                   descriptor = std::make_shared<const SettingsSchema>(bytes);
        SettingsFormModel      model(descriptor, settings.node(""));
        SettingsWidgetRegistry registry;
        int                    created = 0;
        registry.registerWidget("fixture.variant", [&](const auto&, QWidget* parent) {
            ++created;
            return new PortEditor(parent);
        });
        auto  form   = std::make_unique<SettingsFormWidget>(model, registry);
        auto* custom = form->editor("options");
        model.setValue("encoding", "varint");
        QCOMPARE(form->editor("options"), custom);
        QVERIFY_THROWS_EXCEPTION(std::logic_error, registry.unregisterWidget("fixture.variant"));
        model.setValue("encoding", "fixed");
        QCOMPARE(form->editor("options"), custom);
        QCOMPARE(created, 1);
        form.reset();
        QVERIFY(registry.unregisterWidget("fixture.variant"));
    }

    Q_SLOT void variantInheritanceAndExternalSelectorConflict()
    {
        Settings base(QString {});
        Settings local(QString {});
        QVERIFY(base.setValue("encoding", "fixed"));
        QVERIFY(base.setValue("width", 3));
        auto              descriptor = std::make_shared<const SettingsSchema>(settingsVariantFixture());
        SettingsFormModel model(descriptor, SettingsOverlay(base.node(""), local.node("")));
        QVERIFY(!model.hasOverride("encoding"));
        model.setValue("encoding", "varint");
        model.setValue("maxBytes", 7);
        QVERIFY(model.isActive("maxBytes"));
        model.removeValue("encoding");
        QVERIFY(model.isActive("width"));
        QCOMPARE(model.value("width").toInt(), 3);
        model.setValue("encoding", "varint");
        QCOMPARE(model.value("maxBytes").toInt(), 7);
        QString error;
        QVERIFY2(model.apply(&error), qPrintable(error));
        QCOMPARE(local.value("encoding").toString(), QString("varint"));
        QVERIFY(!local.contains("width"));
        QCOMPARE(base.value("width").toInt(), 3);
        model.removeValue("encoding");
        QVERIFY2(model.apply(&error), qPrintable(error));
        QVERIFY(!local.contains("encoding") && !local.contains("maxBytes"));
        QVERIFY(model.isActive("width"));

        // Both branches have defaults: validation alone must not mask selector concurrency.
        model.setValue("width", 4);
        QVERIFY(base.setValue("encoding", "varint"));
        QVERIFY(!model.apply(&error));
        QVERIFY(error.contains("encoding"));
        QVERIFY(!local.contains("width"));
        QCOMPARE(base.value("encoding").toString(), QString("varint"));
    }

    Q_SLOT void timestampPreviewFollowsDraftAndVisibility()
    {
        ProcessorRegistry processors;
        registerNativeProcessors(processors, "native");
        auto descriptor = std::make_shared<const SettingsSchema>(processors.type("native.timestamp")->settingsSchema);
        Settings               settings(QString {});
        SettingsFormModel      model(descriptor, settings.node(""));
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        form.resize(640, 480);
        form.show();
        auto* sample  = form.findChild<QLineEdit*>("timestampSamplePacket");
        auto* display = form.findChild<TerminalDisplay*>("timestampPreviewDisplay");
        auto* timer   = form.findChild<QTimer*>("timestampPreviewTimer");
        auto* error   = form.findChild<QLabel*>("timestampPreviewError");
        QVERIFY(sample && display && timer && error);
        sample->setText("PAYLOAD");
        model.setValue("format", "'FIXED'");
        QTRY_VERIFY(display->text().startsWith("[FIXED] PAYLOAD"));
        QVERIFY(display->screen().lines().first().cells.first().defaultForeground);
        QCOMPARE(display->screen().lines().first().cells.first().attributes, quint32(0));
        QVERIFY(timer->isActive());
        QVERIFY(timer->interval() >= 100 && timer->interval() <= 1000);
        QVERIFY(!settings.contains("template"));

        model.setValue("template", R"(\e[38;2;18;52;86m{datetime}\e[0m {packet})");
        QTRY_VERIFY(display->text().startsWith("FIXED PAYLOAD"));
        QCOMPARE(display->screen().lines().first().cells.first().foreground, QColor("#123456"));
        QVERIFY(display->screen().lines().first().cells.at(6).defaultForeground);

        model.setValue("template", "{unknown}");
        QVERIFY(!error->text().isEmpty());
        QVERIFY(!timer->isActive());
        model.setValue("template", "{datetime} {packet}");
        model.setValue("format", "HH:mm:ss.zzz");
        const auto initial = display->text();
        QTRY_VERIFY_WITH_TIMEOUT(display->text() != initial, 2500);
        QVERIFY(error->text().isEmpty());
        form.hide();
        QVERIFY(!timer->isActive());
    }

    Q_SLOT void colorSwatchTracksDraftAndReset()
    {
        QTemporaryDir directory;
        Settings      base(directory.filePath("base.yaml"));
        Settings      local(directory.filePath("local.yaml"));
        QVERIFY(base.setValue("fg", "#112233"));
        SettingsFormModel      model(schema("fg: {type: string, x-widget: color}"),
                                     SettingsOverlay(base.node(""), local.node("")));
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        auto*                  editor = form.editor("fg");
        auto*                  picker = editor->findChild<QToolButton*>("settingsPicker");
        QVERIFY(picker);
        QVERIFY(!editor->findChild<QLineEdit*>());
        QVERIFY(!editor->findChild<QLabel*>("settingsColorSwatch"));
        QCOMPARE(editor->findChildren<QToolButton*>().size(), 1);
        QCOMPARE(picker->text(), QString("#112233"));
        QVERIFY(!picker->icon().isNull());
        QCOMPARE(picker->icon().pixmap(picker->iconSize()).toImage().pixelColor(10, 8), QColor("#112233"));
        QVERIFY(!model.isDirty());
        model.setValue("fg", "#abcdef");
        QCOMPARE(picker->text(), QString("#abcdef"));
        QCOMPARE(picker->icon().pixmap(picker->iconSize()).toImage().pixelColor(10, 8), QColor("#abcdef"));
        model.setValue("fg", "invalid");
        QCOMPARE(picker->text(), QString("invalid"));
        QCOMPARE(editor->value().toString(), QString("invalid"));
        form.findChild<QToolButton*>("reset_fg")->click();
        QCOMPARE(picker->text(), QString("#112233"));
        QVERIFY(!local.contains("fg"));
        editor->setValue(42);
        QCOMPARE(editor->value().typeId(), QMetaType::Int);
        QCOMPARE(picker->text(), QString("42"));
    }

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
        for (const QString hint : {QString("color")})
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

    Q_SLOT void fontDropdownPreservesDraftAndFilters()
    {
        QTemporaryDir directory;
        Settings      base(directory.filePath("base.yaml"));
        Settings      local(directory.filePath("local.yaml"));
        const auto    families = QFontDatabase::families();
        QVERIFY(!families.isEmpty());
        const QString inherited = families.first();
        QVERIFY(base.setValue("family", inherited));
        SettingsFormModel      model(schema("family: {type: string, x-widget: fontFamily}"),
                                     SettingsOverlay(base.node(""), local.node("")));
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        auto*                  editor = form.editor("family");
        auto*                  combo  = editor->findChild<QComboBox*>("settingsFontFamily");
        auto*                  filter = editor->findChild<QComboBox*>("settingsFontFilter");
        QVERIFY(combo);
        QVERIFY(filter);
        QVERIFY(!editor->findChild<QToolButton*>());
        QVERIFY(!editor->findChild<QLineEdit*>());
        QCOMPARE(combo->labelDrawingMode(), QComboBox::LabelDrawingMode::UseDelegate);
        QCOMPARE(filter->count(), 3);
        for (int kind = 0; kind < filter->count(); ++kind)
        {
            filter->setCurrentIndex(kind);
            QCOMPARE(model.value("family").toString(), inherited);
            QCOMPARE(combo->currentText(), inherited);
            QVERIFY(!model.isDirty());
            QVERIFY(!model.hasOverride("family"));
            for (int row = 0; row < combo->count(); ++row)
            {
                const auto family = combo->itemText(row);
                if (kind != 0 && family != inherited)
                {
                    QCOMPARE(QFontDatabase::isFixedPitch(family), kind == 1);
                }
            }
        }
        filter->setCurrentIndex(0);
        QStyleOptionViewItem option;
        option.initFrom(combo);
        option.font       = combo->font();
        const auto index  = combo->model()->index(combo->currentIndex(), 0);
        QFont      sample = option.font;
        sample.setFamily(inherited);
        const QSize rowSize = combo->itemDelegate()->sizeHint(option, index);
        QVERIFY(rowSize.width() >= QFontMetrics(option.font).horizontalAdvance(inherited)
                                       + QFontMetrics(sample).horizontalAdvance(inherited) + 24);
        QVERIFY(rowSize.height() >= QFontMetrics(sample).height());
        QVERIFY(combo->view()->minimumWidth() >= rowSize.width());
        combo->activated(combo->currentIndex());
        QVERIFY(model.hasOverride("family"));
        QCOMPARE(model.value("family").toString(), inherited);
        QVERIFY(!local.contains("family"));
        QVERIFY(model.apply());
        QCOMPARE(local.value("family").toString(), inherited);
        form.findChild<QToolButton*>("reset_family")->click();
        QVERIFY(!model.hasOverride("family"));
        QCOMPARE(combo->currentText(), inherited);
        QVERIFY(model.apply());
        QVERIFY(!local.contains("family"));
        const QString unavailable = "Unavailable Termaster Font Fixture";
        model.setValue("family", unavailable);
        filter->setCurrentIndex(1);
        QCOMPARE(combo->currentText(), unavailable);
        QCOMPARE(editor->value().toString(), unavailable);
        editor->setValue(42);
        filter->setCurrentIndex(2);
        QCOMPARE(editor->value().typeId(), QMetaType::Int);
        QCOMPARE(combo->currentText(), QString("42"));
        form.resize(900, 300);
        form.show();
        QTest::qWait(20);
        auto* reset = form.findChild<QToolButton*>("reset_family");
        QCOMPARE(reset->height(), SettingsFieldEditor::controlVisualHeight(combo));
        QFont larger = combo->font();
        larger.setPointSize(18);
        combo->setFont(larger);
        QTest::qWait(20);
        QCOMPARE(reset->height(), SettingsFieldEditor::controlVisualHeight(combo));
        QVERIFY(!QApplication::activeModalWidget());
    }

    Q_SLOT void terminalPreviewRetainsRichSampleAfterResize()
    {
        QTemporaryDir     directory;
        Settings          owner(directory.filePath("settings.yaml"));
        auto              descriptor = schema("size: {type: number, default: 12, x-settings-path: appearance/fontSize}",
                                              "  x-widget: terminalPreview\n");
        SettingsFormModel model(descriptor, owner.node(""));
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        auto*                  preview = form.findChild<TerminalDisplay*>("settingsAppearancePreview");
        QVERIFY(preview);
        QSignalSpy output(preview, &TerminalDisplay::outputBytes);
        form.resize(300, 650);
        form.show();
        QTest::qWait(20);
        form.resize(1000, 700);
        QTRY_VERIFY(preview->text().contains("abcdefghijklmnopqrstuvwxyz 0123456789"));
        QVERIFY(preview->text().contains("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
        QVERIFY(preview->text().contains(QStringLiteral("Кириллица: Привет мир!")));
        QVERIFY(preview->text().contains(QStringLiteral("┌─┬─┐")));
        const auto& lines = preview->screen().lines();
        QVERIFY(lines.size() >= 7);
        const auto& styles = lines.at(4).cells;
        QVERIFY(styles.at(0).attributes & TerminalScreen::Bold);
        QVERIFY(styles.at(5).attributes & TerminalScreen::Italic);
        QVERIFY(styles.at(12).attributes & TerminalScreen::Underline);
        QVERIFY(styles.at(22).attributes & TerminalScreen::Strike);
        QVERIFY(styles.at(29).attributes & TerminalScreen::Inverse);
        QVERIFY(lines.at(0).cells.at(0).defaultForeground);
        QVERIFY(lines.at(0).cells.at(0).defaultBackground);
        for (int index = 0; index < 16; ++index)
        {
            QCOMPARE(lines.at(5).cells.at(3 + index * 2).foregroundIndex, index);
            QCOMPARE(lines.at(6).cells.at(3 + index * 2).backgroundIndex, index);
        }
        QVERIFY(!preview->screen().cursorVisible());
        QVERIFY(!model.isDirty());
        model.setValue("size", 20);
        form.resize(1200, 800);
        QTRY_VERIFY(preview->text().contains("abcdefghijklmnopqrstuvwxyz 0123456789"));
        QVERIFY(preview->terminalSize().height() >= 7);
        QVERIFY(preview->text().contains("Bold Italic Underline Strike Reverse"));
        QCOMPARE(output.count(), 0);
        QVERIFY(!owner.contains("appearance/fontSize"));
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
        for (const QString key : {QString("fg")})
        {
            auto*      editor = form.editor(key);
            const auto before = model.value(key);
            QVERIFY(!editor->findChild<QLineEdit*>());
            QCOMPARE(editor->findChildren<QToolButton*>().size(), 1);
            QCOMPARE(editor->findChild<QToolButton*>("settingsPicker")->text(), before.toString());
            QTimer::singleShot(0, &form, [] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                dialog->reject();
            });
            editor->findChild<QToolButton*>("settingsPicker")->click();
            QCOMPARE(model.value(key), before);
        }
        auto* family = form.editor("family")->findChild<QComboBox*>("settingsFontFamily");
        QVERIFY(family);
        const int selected = family->findText(QFontDatabase::families().first());
        QVERIFY(selected >= 0);
        family->setCurrentIndex(selected);
        family->activated(selected);
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

    Q_SLOT void hexadecimalIntegerDraftsAndInheritance()
    {
        QTemporaryDir directory;
        Settings      base(directory.filePath("base.yaml"));
        Settings      local(directory.filePath("local.yaml"));
        QVERIFY(base.setValue("hex", qulonglong(65535)));
        QVERIFY(base.setValue("unknown", QString("unavailable")));
        SettingsOverlay overlay(base.node(""), local.node(""));
        auto            descriptor = schema(
            "hex: {type: integer, minimum: 0, maximum: 4294967295, default: 0, x-widget: hexInteger}\n"
            "unknown: {type: integer, minimum: 0, maximum: 255, x-widget: hexInteger}");
        SettingsFormModel      model(descriptor, overlay);
        SettingsWidgetRegistry registry;
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 registry.registerWidget("hexInteger",
                                                         [](const auto&, QWidget*) -> SettingsFieldEditor* {
                                                             return nullptr;
                                                         }));
        SettingsFormWidget form(model, registry);
        form.resize(640, 260);
        form.show();
        QTest::qWait(30);
        auto* editor  = form.editor("hex");
        auto* input   = editor->findChild<QLineEdit*>();
        auto* unknown = form.editor("unknown")->findChild<QLineEdit*>();
        QCOMPARE(input->text(), QString("0xFFFF"));
        QCOMPARE(unknown->text(), QString("unavailable"));
        unknown->setFocus();
        QTest::keyClick(unknown, Qt::Key_Return);
        input->setFocus();
        QCOMPARE(model.value("unknown"), QVariant(QString("unavailable")));
        QVERIFY(!model.isDirty());
        QVERIFY(!model.apply());
        unknown->setText("0x7f");
        QCOMPARE(model.value("unknown").toULongLong(), qulonglong(127));
        for (const auto& text : {QString("0"), QString("0Xffffffff"), QString("ABCDEF"), QString("0xFFFFFFFF")})
        {
            input->setText(text);
            QVERIFY(editor->hasAcceptableInput());
            QCOMPARE(model.value("hex").typeId(), QMetaType::ULongLong);
            bool ok = false;
            QCOMPARE(model.value("hex").toULongLong(), text.toULongLong(&ok, 16));
            QVERIFY(ok);
        }
        QVERIFY(!local.contains("hex"));
        QVERIFY(model.apply());
        QCOMPARE(local.value("hex").typeId(), QMetaType::LongLong);
        QCOMPARE(local.value("hex").toULongLong(), qulonglong(4294967295));
        QVERIFY(local.sync());
        Settings reloaded(directory.filePath("local.yaml"));
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.value("hex").toULongLong(), qulonglong(4294967295));
        for (const auto& invalid : {QString("0x100000000"), QString("0x"), QString("-1"), QString("0xGG"), QString()})
        {
            input->setText(invalid);
            QVERIFY(!editor->hasAcceptableInput());
            QVERIFY(!model.apply());
            QCOMPARE(local.value("hex").toULongLong(), qulonglong(4294967295));
        }
        input->setText("abcd");
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(input->text(), QString("0xABCD"));
        QCOMPARE(model.value("hex").toULongLong(), qulonglong(43981));
        auto* reset = form.findChild<QToolButton*>("reset_hex");
        reset->click();
        QCOMPARE(input->text(), QString("0xFFFF"));
        QVERIFY(!model.hasOverride("hex"));
        QVERIFY(model.apply());
        QVERIFY(!local.contains("hex"));
        for (const QVariant& original :
             {QVariant(-1), QVariant(4294967296LL), QVariant(1.5), QVariant(true), QVariant(QString("0xAA"))})
        {
            editor->setValue(original);
            input->setFocus();
            QTest::keyClick(input, Qt::Key_Return);
            unknown->setFocus();
            QCOMPARE(editor->value(), original);
            QCOMPARE(editor->value().typeId(), original.typeId());
        }
    }

    Q_SLOT void resetHeightsFollowVisibleFrames()
    {
        Settings        base(QString {});
        Settings        local(QString {});
        SettingsOverlay overlay(base.node(""), local.node(""));
        auto            descriptor = schema(
            "combo: {type: string, enum: [a, b], default: a}\n"
            "line: {type: string, default: sample}\n"
            "spin: {type: integer, minimum: 0, maximum: 100, default: 4}\n"
            "check: {type: boolean, default: true}\n"
            "hex: {type: integer, minimum: 0, maximum: 4294967295, default: 0, x-widget: hexInteger}");
        SettingsFormModel      model(descriptor, overlay);
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        form.resize(640, 400);
        form.show();
        auto verify = [&] {
            QTest::qWait(20);
            for (const auto& key :
                 {QString("combo"), QString("line"), QString("spin"), QString("check"), QString("hex")})
            {
                auto* control = form.editor(key)->primaryControl();
                auto* reset   = form.findChild<QToolButton*>("reset_" + key);
                QVERIFY(control && reset);
                const int expected = SettingsFieldEditor::controlVisualHeight(control);
                QCOMPARE(reset->height(), expected);
                QVERIFY(reset->iconSize().height() <= expected - 4);
                if (key == "check")
                {
                    QVERIFY(reset->height() <= control->height());
                }
                else if (control->style()->objectName() == "windows11")
                {
                    QCOMPARE(reset->height(), control->height() - 2);
                }
            }
        };
        verify();
        auto* combo = form.editor("combo")->findChild<QComboBox*>();
        if (combo->style()->objectName() == "windows11")
        {
            // The style's public frame rect includes margins: verify its actual alpha-painted frame.
            QImage frame(combo->size(), QImage::Format_ARGB32_Premultiplied);
            frame.fill(Qt::transparent);
            QStyleOptionComboBox option;
            option.initFrom(combo);
            option.rect         = combo->rect();
            option.frame        = true;
            option.subControls  = QStyle::SC_ComboBoxFrame;
            option.state       &= ~(QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange);
            QPainter painter(&frame);
            combo->style()->drawComplexControl(QStyle::CC_ComboBox, &option, &painter, combo);
            painter.end();
            int first = frame.height();
            int last  = -1;
            for (int y = 0; y < frame.height(); ++y)
            {
                if (qAlpha(frame.pixel(frame.width() / 2, y)) != 0)
                {
                    first = std::min(first, y);
                    last  = y;
                }
            }
            QCOMPARE(first, 1);
            QCOMPARE(last, combo->height() - 2);
            QCOMPARE(form.findChild<QToolButton*>("reset_combo")->height(), last - first + 1);
        }
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            model.setValue("combo", QString("b"));
            model.setValue("check", false);
            model.setValue("hex", qulonglong(0xffffffff));
            verify();
            for (const auto* key : {"combo", "check", "hex"})
            {
                form.findChild<QToolButton*>("reset_" + QString(key))->click();
            }
            verify();
        }
        QFont font = form.font();
        font.setPointSize(font.pointSize() + 6);
        form.setFont(font);
        form.resize(800, 500);
        verify();
        form.hide();
        form.show();
        verify();
    }

    Q_SLOT void numericSpinBoxesPreserveBoundsAndLoadedValues()
    {
        QTemporaryDir directory;
        Settings      owner(directory.filePath("settings.yaml"));
        QVERIFY(owner.setValue("wide", qlonglong(9007199254740991LL)));
        QVERIFY(owner.setValue("fraction", 0.12345678901234566));
        QVERIFY(owner.setValue("invalid", QString("unavailable")));
        auto descriptor = schema("small: {type: integer, minimum: -2.5, maximum: 4.5, default: 2}\n"
                                 "wide: {type: integer}\n"
                                 "fraction: {type: number, minimum: -1, maximum: 1}\n"
                                 "invalid: {type: integer, minimum: 1, maximum: 9}\n"
                                 "choice: {type: integer, enum: [1, 2], default: 1}\n"
                                 "exclusive: {type: integer, exclusiveMinimum: 2, exclusiveMaximum: 6, default: 3}");
        SettingsFormModel      model(descriptor, owner.node(""));
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        auto*                  small = form.editor("small")->findChild<QSpinBox*>();
        QVERIFY(small);
        QCOMPARE(small->minimum(), -2);
        QCOMPARE(small->maximum(), 4);
        auto* wide = form.editor("wide")->findChild<QDoubleSpinBox*>();
        QVERIFY(wide);
        QCOMPARE(wide->decimals(), 0);
        QCOMPARE(form.editor("wide")->value().toLongLong(), qlonglong(9007199254740991LL));
        wide->stepDown();
        QCOMPARE(model.value("wide").toLongLong(), qlonglong(9007199254740990LL));
        auto* fraction = form.editor("fraction")->findChild<QDoubleSpinBox*>();
        QVERIFY(fraction);
        QCOMPARE(fraction->minimum(), -1.0);
        QCOMPARE(fraction->maximum(), 1.0);
        QCOMPARE(form.editor("fraction")->value().toDouble(), 0.12345678901234566);
        QCOMPARE(fraction->findChild<QLineEdit*>()->text(), QString::number(0.12345678901234566, 'g', 17));
        auto* fractionInput = fraction->findChild<QLineEdit*>();
        fractionInput->selectAll();
        QTest::keyClicks(fractionInput, "-2.5e-12");
        QCOMPARE(model.value("fraction").toDouble(), -2.5e-12);
        QVERIFY(fraction->hasAcceptableInput());
        fraction->setSingleStep(1e-12);
        fraction->stepUp();
        QVERIFY(std::abs(model.value("fraction").toDouble() - (-1.5e-12)) < 1e-27);
        model.setValue("fraction", 1e-20);
        QCOMPARE(form.editor("fraction")->value().toDouble(), 1e-20);
        QVERIFY(fraction->hasAcceptableInput());
        fraction->setSingleStep(1e-20);
        fraction->stepUp();
        QCOMPARE(model.value("fraction").toDouble(), 2e-20);
        QCOMPARE(form.editor("invalid")->value().toString(), QString("unavailable"));
        QCOMPARE(form.editor("invalid")->findChild<QLineEdit*>()->text(), QString("unavailable"));
        QVERIFY(!model.isValid());
        QVERIFY(!model.apply());
        QCOMPARE(owner.value("invalid").toString(), QString("unavailable"));
        QVERIFY(form.editor("choice")->findChild<QComboBox*>());
        QVERIFY(!form.editor("choice")->findChild<QAbstractSpinBox*>());
        auto* exclusive = form.editor("exclusive")->findChild<QSpinBox*>();
        QVERIFY(exclusive);
        QCOMPARE(exclusive->minimum(), 3);
        QCOMPARE(exclusive->maximum(), 5);
    }

    Q_SLOT void numericValueSettersUpdateDrafts()
    {
        QTemporaryDir directory;
        Settings      base(directory.filePath("base.yaml"));
        Settings      local(directory.filePath("local.yaml"));
        QVERIFY(base.setValue("integer", 2));
        QVERIFY(base.setValue("number", 0.25));
        SettingsOverlay        overlay(base.node(""), local.node(""));
        auto                   descriptor = schema("integer: {type: integer, minimum: 1, maximum: 9}\n"
                                                   "number: {type: number, minimum: 0, maximum: 1}");
        SettingsFormModel      model(descriptor, overlay);
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        auto*                  integer = form.editor("integer")->findChild<QSpinBox*>();
        integer->setValue(5);
        QCOMPARE(model.value("integer").toInt(), 5);
        QVERIFY(model.hasOverride("integer"));
        auto* number     = form.editor("number")->findChild<QDoubleSpinBox*>();
        auto* accessible = QAccessible::queryAccessibleInterface(number);
        QVERIFY(accessible);
        auto* value = accessible->valueInterface();
        QVERIFY(value);
        value->setCurrentValue(0.75);
        QCOMPARE(model.value("number").toDouble(), 0.75);
        QVERIFY(model.hasOverride("number"));
        QVERIFY(!local.contains("integer"));
        QVERIFY(!local.contains("number"));
        QVERIFY(model.apply());
        QCOMPARE(local.value("integer").toInt(), 5);
        QCOMPARE(local.value("number").toDouble(), 0.75);
    }

    Q_SLOT void invalidLoadedNumbersSurviveFocusAndConfirmation()
    {
        QTemporaryDir directory;
        Settings      base(directory.filePath("base.yaml"));
        Settings      local(directory.filePath("local.yaml"));
        QVERIFY(base.setValue("integer", 50));
        QVERIFY(base.setValue("number", QString("unavailable")));
        QVERIFY(base.setValue("invalidInteger", QString("unavailable")));
        SettingsOverlay        overlay(base.node(""), local.node(""));
        auto                   descriptor = schema("integer: {type: integer, minimum: 1, maximum: 9}\n"
                                                   "number: {type: number, minimum: 0, maximum: 1}\n"
                                                   "invalidInteger: {type: integer, minimum: 1, maximum: 9}");
        SettingsFormModel      model(descriptor, overlay);
        SettingsWidgetRegistry registry;
        SettingsFormWidget     form(model, registry);
        form.resize(640, 260);
        form.show();
        QTest::qWait(30);
        for (const auto& key : {QString("integer"), QString("number"), QString("invalidInteger")})
        {
            auto*          editor       = form.editor(key);
            auto*          input        = editor->findChild<QLineEdit*>();
            const QVariant original     = model.value(key);
            const QString  originalText = input->text();
            QCOMPARE(originalText, original.toString());
            input->setFocus();
            QTest::keyClick(input, Qt::Key_Return);
            form.editor(key == "number" ? "integer" : "number")->findChild<QLineEdit*>()->setFocus();
            QCoreApplication::processEvents();
            QCOMPARE(input->text(), originalText);
            QCOMPARE(editor->value(), original);
            QCOMPARE(model.value(key), original);
            QVERIFY(!model.hasOverride(key));
            QVERIFY(!model.isDirty());
        }
        QVERIFY(!model.apply());
        QVERIFY(!local.contains("integer"));
        QVERIFY(!local.contains("number"));
        QVERIFY(!local.contains("invalidInteger"));
        form.editor("invalidInteger")->findChild<QSpinBox*>()->stepUp();
        QVERIFY(model.hasOverride("invalidInteger"));
        auto* integer = form.editor("integer")->findChild<QSpinBox*>();
        integer->stepDown();
        QVERIFY(model.hasOverride("integer"));
        QVERIFY(model.value("integer").toInt() >= 1 && model.value("integer").toInt() <= 9);
        auto* numberInput = form.editor("number")->findChild<QLineEdit*>();
        numberInput->selectAll();
        QTest::keyClicks(numberInput, "0.5");
        QCOMPARE(model.value("number").toDouble(), 0.5);
        QVERIFY(model.apply());
        QCOMPARE(local.value("number").toDouble(), 0.5);
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
        form.resize(640, 220);
        form.show();
        QTest::qWait(30);
        auto* valueControl = form.editor("fontSize")->findChild<QAbstractSpinBox*>();
        QVERIFY(valueControl);
        QCOMPARE(reset->height(), SettingsFieldEditor::controlVisualHeight(valueControl));
        QVERIFY(reset->styleSheet().contains("#f0a13b"));
        const int originalHeight = reset->height();
        QFont     largerFont     = form.font();
        largerFont.setPointSize(largerFont.pointSize() + 6);
        form.setFont(largerFont);
        QTest::qWait(30);
        QCOMPARE(reset->height(), SettingsFieldEditor::controlVisualHeight(valueControl));
        QVERIFY(reset->height() > originalHeight);
        const auto controlGeometry = valueControl->geometry();
        reset->hide();
        QTest::qWait(10);
        QCOMPARE(valueControl->geometry(), controlGeometry);
        reset->show();
        const auto resetCenter = reset->mapTo(&form, reset->rect().center());
        const auto inputCenter = valueControl->mapTo(&form, valueControl->rect().center());
        QVERIFY(std::abs(resetCenter.y() - inputCenter.y()) <= 1);
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
