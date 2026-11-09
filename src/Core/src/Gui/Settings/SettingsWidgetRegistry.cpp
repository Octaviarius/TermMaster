#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
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
template <class Base>
class NumericSpinBox : public Base
{
public:
    explicit NumericSpinBox(QWidget* parent) : Base(parent)
    {
    }

    bool normalizing() const
    {
        return _normalizing;
    }

    void setSteppedCallback(std::function<void()> callback)
    {
        _stepped = std::move(callback);
    }
    void setActivatedCallback(std::function<void()> callback)
    {
        _activated = std::move(callback);
    }

protected:
    void stepBy(int steps) override
    {
        const QPointer<NumericSpinBox> guard(this);
        Base::stepBy(steps);
        if (!guard)
        {
            return;
        }
        const auto callback = _stepped;
        if (callback)
        {
            callback();
        }
    }
    bool event(QEvent* event) override
    {
        if (event->type() != QEvent::Show && event->type() != QEvent::FocusIn && event->type() != QEvent::FocusOut)
        {
            return Base::event(event);
        }
        const bool previous = _normalizing;
        _normalizing        = true;
        const QPointer<NumericSpinBox> guard(this);
        const bool                     result = Base::event(event);
        if (!guard)
        {
            return result;
        }
        _normalizing        = previous;
        const auto callback = _activated;
        if ((event->type() == QEvent::Show || event->type() == QEvent::FocusIn) && callback)
        {
            callback();
        }
        return result;
    }

private:
    bool                  _normalizing = false;
    std::function<void()> _stepped;
    std::function<void()> _activated;
};
class NumberSpinBox final : public NumericSpinBox<QDoubleSpinBox>
{
public:
    explicit NumberSpinBox(QWidget* parent) : NumericSpinBox<QDoubleSpinBox>(parent)
    {
        setLocale(QLocale::c());
        setDecimals(323);
    }
    QSize sizeHint() const override
    {
        return QSize(fontMetrics().horizontalAdvance(QStringLiteral("-9007199254740991.123")) + 32,
                     QDoubleSpinBox::sizeHint().height());
    }

protected:
    QString textFromValue(double value) const override
    {
        return QString::number(value, 'g', 17);
    }
    double valueFromText(const QString& text) const override
    {
        bool         ok     = false;
        const double parsed = text.toDouble(&ok);
        return ok && std::isfinite(parsed) ? parsed : value();
    }
    QValidator::State validate(QString& text, int& position) const override
    {
        Q_UNUSED(position)
        bool         ok     = false;
        const double parsed = text.toDouble(&ok);
        if (ok && std::isfinite(parsed) && parsed >= minimum() && parsed <= maximum()
            && (decimals() != 0 || std::trunc(parsed) == parsed))
        {
            return QValidator::Acceptable;
        }
        const QRegularExpression partial(QStringLiteral("^[+-]?(?:[0-9]*(?:\\.[0-9]*)?)(?:[eE][+-]?[0-9]*)?$"));
        return partial.match(text).hasMatch() ? QValidator::Intermediate : QValidator::Invalid;
    }
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
        else if (hint == "integer" || hint == "number"
                 || (hint.isEmpty() && (field.type == "integer" || field.type == "number")))
        {
            constexpr double exactLimit = 9007199254740991.0;
            double           minimum    = field.definition.value("minimum", -exactLimit).toDouble();
            double           maximum    = field.definition.value("maximum", exactLimit).toDouble();
            const bool       integer    = field.type == "integer" || hint == "integer";
            if (field.definition.contains("exclusiveMinimum"))
            {
                const double bound = field.definition.value("exclusiveMinimum").toDouble();
                minimum = std::max(minimum, integer ? std::floor(bound) + 1 : std::nextafter(bound, exactLimit));
            }
            if (field.definition.contains("exclusiveMaximum"))
            {
                const double bound = field.definition.value("exclusiveMaximum").toDouble();
                maximum = std::min(maximum, integer ? std::ceil(bound) - 1 : std::nextafter(bound, -exactLimit));
            }
            if (integer)
            {
                minimum = std::ceil(minimum);
                maximum = std::floor(maximum);
            }
            minimum = std::clamp(minimum, -exactLimit, exactLimit);
            maximum = std::clamp(maximum, -exactLimit, exactLimit);
            // An unsatisfiable schema remains invalid in the draft; Qt requires an ordered range.
            if (minimum > maximum)
            {
                maximum = minimum;
            }
            if (integer && minimum >= std::numeric_limits<int>::min() && maximum <= std::numeric_limits<int>::max())
            {
                _integer = new NumericSpinBox<QSpinBox>(this);
                _integer->setLocale(QLocale::c());
                _integer->setRange(static_cast<int>(minimum), static_cast<int>(maximum));
                layout->addWidget(_integer);
                _line = _integer->findChild<QLineEdit*>();
            }
            else
            {
                _number = new NumberSpinBox(this);
                if (integer)
                {
                    _number->setDecimals(0);
                }
                _number->setRange(minimum, maximum);
                layout->addWidget(_number);
                _line = _number->findChild<QLineEdit*>();
            }
            auto restoreOriginal = [this] {
                if (_untouched)
                {
                    const QSignalBlocker blocker(_line);
                    _line->setText(_value.typeId() == QMetaType::Double ? QString::number(_value.toDouble(), 'g', 17) :
                                                                          _value.toString());
                }
            };
            if (_integer)
            {
                _integer->setSteppedCallback([this] { _changed(); });
                _integer->setActivatedCallback(restoreOriginal);
                connect(_integer, &QSpinBox::valueChanged, this, [this] {
                    if (!_integer->normalizing())
                    {
                        _changed();
                    }
                });
            }
            if (_number)
            {
                _number->setSteppedCallback([this] { _changed(); });
                _number->setActivatedCallback(restoreOriginal);
                connect(_number, &QDoubleSpinBox::valueChanged, this, [this] {
                    if (!_number->normalizing())
                    {
                        _changed();
                    }
                });
            }
            _line->installEventFilter(this);
            (_integer ? static_cast<QAbstractSpinBox*>(_integer) : _number)->installEventFilter(this);
            connect(_line, &QLineEdit::textChanged, this, [this] { _changed(); });
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
            // Preserve the original text and QVariant even when Qt cannot represent a loaded value.
            if (_integer)
            {
                const QSignalBlocker spinBlocker(_integer);
                _integer->setValue(value.toInt());
            }
            if (_number)
            {
                const QSignalBlocker spinBlocker(_number);
                _number->setValue(value.toDouble());
            }
            _line->setText(value.typeId() == QMetaType::Double ? QString::number(value.toDouble(), 'g', 17) :
                                                                 value.toString());
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

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (_untouched && _line && (_integer || _number) && !_line->hasAcceptableInput())
        {
            const bool  focusOut = event->type() == QEvent::FocusOut;
            const auto* key      = event->type() == QEvent::KeyPress ? static_cast<QKeyEvent*>(event) : nullptr;
            const bool  confirm  = key && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter);
            // Focus changes and confirmation must not turn a loaded invalid value into a clamped edit.
            // A real text change or a step clears _untouched and restores normal spinbox behavior.
            if (focusOut || confirm)
            {
                return true;
            }
        }
        return SettingsFieldEditor::eventFilter(watched, event);
    }

private:
    SettingsSchema::Field     _field;
    NumericSpinBox<QSpinBox>* _integer = nullptr;
    NumberSpinBox*            _number  = nullptr;
    QLineEdit*                _line    = nullptr;
    QPlainTextEdit*           _text    = nullptr;
    QComboBox*                _combo   = nullptr;
    QCheckBox*                _check   = nullptr;
    QVariant                  _value;
    bool                      _untouched = true;
    bool                      _yaml() const
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
