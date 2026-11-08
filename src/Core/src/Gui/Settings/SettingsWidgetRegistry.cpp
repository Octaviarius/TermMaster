#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <cmath>
#include <stdexcept>

namespace
{
class TerminalPreviewEditor final : public SettingsFieldEditor
{
public:
    TerminalPreviewEditor(const SettingsSchema::Field& field, QWidget* parent) : SettingsFieldEditor(parent)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* box      = new QGroupBox(field.presentation.value("title", tr("Terminal preview")).toString(), this);
        auto* contents = new QVBoxLayout(box);
        _display       = new TerminalDisplay(box);
        _display->setObjectName("settingsAppearancePreview");
        _display->setAccessibleName(tr("Terminal appearance draft preview"));
        _display->setFocusPolicy(Qt::NoFocus);
        _display->setAttribute(Qt::WA_TransparentForMouseEvents);
        _display->viewport()->setAttribute(Qt::WA_TransparentForMouseEvents);
        _display->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _display->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _display->setFixedHeight(132);
        contents->addWidget(_display);
        layout->addWidget(box);
    }
    QVariant value() const override
    {
        return {};
    }
    void setValue(const QVariant&) override
    {
    }
    void setContext(const QVariantMap& context) override
    {
        QVariantMap appearance {
            {"scrollbackLines", 1}
        };
        for (const auto& key :
             {QString("fontFamily"), QString("fontSize"), QString("foreground"), QString("background")})
        {
            appearance.insert(key, context.value("appearance/" + key));
        }
        _display->setAppearance(appearance);
        _display->clear();
        _display->receive("Aa Bb 0123456789  \x1b[1mBold\x1b[0m\x1b[?25l");
    }

private:
    TerminalDisplay* _display = nullptr;
};
class BuiltinEditor final : public SettingsFieldEditor
{
public:
    BuiltinEditor(const SettingsSchema::Field& field, QWidget* parent) : SettingsFieldEditor(parent), _field(field)
    {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        const auto hint = field.widget;
        if (hint == "enumCombo" || (hint.isEmpty() && field.definition.contains("enum")))
        {
            _combo = new QComboBox(this);
            for (const auto& item : field.definition.value("enum").toList())
            {
                _combo->addItem(item.isValid() ? item.toString() : "null", item);
            }
            layout->addWidget(_combo);
            connect(_combo, &QComboBox::currentIndexChanged, this, [this] { _changed(); });
        }
        else if (hint == "boolCheckBox" || (hint.isEmpty() && field.type == "boolean"))
        {
            _check = new QCheckBox(this);
            layout->addWidget(_check);
            setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
            connect(_check, &QCheckBox::toggled, this, [this] { _changed(); });
        }
        else if (hint == "multiline" || _yaml())
        {
            _text = new QPlainTextEdit(this);
            _text->setMinimumHeight(90);
            _text->setMaximumHeight(180);
            layout->addWidget(_text);
            connect(_text, &QPlainTextEdit::textChanged, this, [this] { _changed(); });
        }
        else
        {
            _line = new QLineEdit(this);
            if (hint == "password")
            {
                _line->setEchoMode(QLineEdit::Password);
            }
            layout->addWidget(_line);
            connect(_line, &QLineEdit::textChanged, this, [this] { _changed(); });
            if (hint == "fontFamily" || hint == "color")
            {
                auto* picker = new QToolButton(this);
                picker->setObjectName("settingsPicker");
                picker->setText(tr("Choose…"));
                picker->setToolTip(hint == "fontFamily" ? tr("Choose font family") : tr("Choose color"));
                layout->addWidget(picker);
                connect(picker, &QToolButton::clicked, this, [this, hint] {
                    const QPointer<BuiltinEditor> editor(this);
                    if (hint == "fontFamily")
                    {
                        QFont initial;
                        initial.setFamily(_line->text());
                        QPointer<QFontDialog> dialog = new QFontDialog(initial, this);
                        dialog->setOption(QFontDialog::DontUseNativeDialog);
                        dialog->setWindowTitle(tr("Choose font family"));
                        dialog->adjustSize();
                        const int result = dialog->exec();
                        if (editor && dialog && result == QDialog::Accepted)
                        {
                            const auto selected = dialog->selectedFont().family();
                            if (editor->_line->text() == selected)
                            {
                                editor->_changed();
                            }
                            else
                            {
                                editor->_line->setText(selected);
                            }
                        }
                        delete dialog.data();
                    }
                    else
                    {
                        QPointer<QColorDialog> dialog = new QColorDialog(QColor(_line->text()), this);
                        dialog->setOption(QColorDialog::DontUseNativeDialog);
                        dialog->setWindowTitle(tr("Choose color"));
                        dialog->adjustSize();
                        const int result = dialog->exec();
                        if (editor && dialog && result == QDialog::Accepted && dialog->selectedColor().isValid())
                        {
                            const auto selected = dialog->selectedColor().name(QColor::HexRgb);
                            if (editor->_line->text() == selected)
                            {
                                editor->_changed();
                            }
                            else
                            {
                                editor->_line->setText(selected);
                            }
                        }
                        delete dialog.data();
                    }
                });
            }
        }
    }

    QVariant value() const override
    {
        if (_untouched)
        {
            return _value;
        }
        if (_combo)
        {
            return _combo->currentData();
        }
        if (_check)
        {
            return _check->isChecked();
        }
        const QString text = _line ? _line->text() : _text->toPlainText();
        if (_yaml())
        {
            try
            {
                return SettingsSchema::parseValue(text.toUtf8());
            }
            catch (const std::exception&)
            {
                return text;
            }
        }
        if (_field.type == "integer")
        {
            bool       ok          = false;
            const auto signedValue = text.toLongLong(&ok);
            if (ok)
            {
                return QVariant::fromValue(signedValue);
            }
            if (!text.startsWith('-'))
            {
                const auto unsignedValue = text.toULongLong(&ok);
                if (ok)
                {
                    return QVariant::fromValue(unsignedValue);
                }
            }
            return text;
        }
        if (_field.type == "number")
        {
            bool         ok     = false;
            const double number = text.toDouble(&ok);
            if (ok && std::isfinite(number))
            {
                return number;
            }
            return text;
        }
        return text;
    }

    void setValue(const QVariant& value) override
    {
        _value     = value;
        _untouched = true;
        if (_combo)
        {
            const QSignalBlocker blocker(_combo);
            int                  index = -1;
            for (int i = 0; i < _combo->count(); ++i)
            {
                if (_combo->itemData(i).typeId() == value.typeId() && _combo->itemData(i) == value)
                {
                    index = i;
                    break;
                }
            }
            // Integer widths are not persisted; retain numeric enum identity across reloads.
            if (index < 0 && value.typeId() != QMetaType::QString)
            {
                index = _combo->findData(value);
            }
            _combo->setCurrentIndex(index);
        }
        if (_check)
        {
            const QSignalBlocker blocker(_check);
            _check->setChecked(value.toBool());
        }
        if (_line)
        {
            const QSignalBlocker blocker(_line);
            _line->setText(value.toString());
        }
        if (_text)
        {
            const QSignalBlocker blocker(_text);
            _text->setPlainText((_yaml()) ? QString::fromUtf8(SettingsSchema::serializeValue(value)) :
                                            value.toString());
        }
    }

    bool hasAcceptableInput() const override
    {
        return inputError().isEmpty();
    }
    QString inputError() const override
    {
        if (_untouched || _combo || _check)
        {
            return {};
        }
        if (_field.type == "integer" && value().typeId() == QMetaType::QString)
        {
            return "Enter an integer within the supported exact range";
        }
        if (_field.type == "number" && value().typeId() == QMetaType::QString)
        {
            return "Enter a finite number";
        }
        if (_yaml())
        {
            try
            {
                SettingsSchema::parseValue(_text->toPlainText().toUtf8());
            }
            catch (const std::exception& error)
            {
                return QString::fromUtf8(error.what());
            }
        }
        return {};
    }

private:
    SettingsSchema::Field _field;
    QLineEdit*            _line  = nullptr;
    QPlainTextEdit*       _text  = nullptr;
    QComboBox*            _combo = nullptr;
    QCheckBox*            _check = nullptr;
    QVariant              _value;
    bool                  _untouched = true;
    bool                  _yaml() const
    {
        return _field.widget == "yaml" || _field.type == "object" || _field.type == "array"
            || (_field.widget.isEmpty() && (_field.type.isEmpty() || _field.type == "null"));
    }
    void _changed()
    {
        _untouched = false;
        emit valueChanged();
    }
};
const QStringList builtins {"boolCheckBox",
                            "stringLineEdit",
                            "enumCombo",
                            "integer",
                            "number",
                            "password",
                            "multiline",
                            "yaml",
                            "fontFamily",
                            "color",
                            "terminalPreview"};
} // namespace

SettingsWidgetRegistry::SettingsWidgetRegistry(QObject* parent) : QObject(parent)
{
}

void SettingsWidgetRegistry::registerWidget(const QString& name, Factory factory)
{
    if (name.isEmpty() || !factory || builtins.contains(name) || _factories.contains(name))
    {
        throw std::invalid_argument("Invalid or duplicate settings widget registration");
    }
    _factories.insert(name, std::move(factory));
}

bool SettingsWidgetRegistry::unregisterWidget(const QString& name)
{
    for (const auto& editor : _live.value(name))
    {
        if (editor)
        {
            throw std::logic_error("Close settings forms before unregistering their widget factory");
        }
    }
    _live.remove(name);
    return _factories.remove(name) != 0;
}

SettingsFieldEditor* SettingsWidgetRegistry::create(const SettingsSchema::Field& field, QWidget* parent)
{
    if (_factories.contains(field.widget))
    {
        auto* editor = _factories.value(field.widget)(field, parent);
        if (!editor)
        {
            throw std::runtime_error("Settings widget factory returned null");
        }
        editor->setParent(parent);
        _live[field.widget].removeIf([](const auto& item) { return item.isNull(); });
        _live[field.widget].append(editor);
        return editor;
    }
    if (!field.widget.isEmpty() && !builtins.contains(field.widget))
    {
        throw std::invalid_argument(("Unknown settings widget: " + field.widget).toStdString());
    }
    if (field.widget == "terminalPreview")
    {
        return new TerminalPreviewEditor(field, parent);
    }
    return new BuiltinEditor(field, parent);
}
