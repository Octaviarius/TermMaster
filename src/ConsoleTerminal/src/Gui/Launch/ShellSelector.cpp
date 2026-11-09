#include "ShellSelector.h"

#include <QComboBox>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

ShellSelector::ShellSelector(QWidget* parent) : QWidget(parent), _combo(new QComboBox(this))
{
    _combo->setObjectName("consoleShellCombo");
    _combo->setEditable(true);
    _combo->setInsertPolicy(QComboBox::NoInsert);
    _combo->setMaxVisibleItems(12);
#ifdef Q_OS_WIN
    _combo->addItems({"pwsh.exe", "powershell.exe", "cmd.exe", "wsl.exe", "bash.exe", "sh.exe", "ash.exe"});
#else
    _combo->addItems({"bash", "sh", "zsh", "fish", "dash", "ash", "pwsh"});
#endif
    _combo->setCurrentIndex(-1);
    _combo->lineEdit()->setMaxLength(32767);
    _combo->lineEdit()->setPlaceholderText(tr("Automatic (leave empty)"));
    _combo->setToolTip(tr("Choose a common shell or enter any executable/script path. "
                          "Suggestions require the corresponding shell to be installed. "
                          "Configure arguments in the separate Arguments field."));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_combo);
    connect(_combo, &QComboBox::editTextChanged, this, [this] {
        _untouched = false;
        if (_changed)
        {
            _changed();
        }
    });
}

QWidget* ShellSelector::widget()
{
    return this;
}
QVariant ShellSelector::value() const
{
    return _untouched ? _value : QVariant(_combo->currentText());
}
void ShellSelector::setValue(const QVariant& value)
{
    const QSignalBlocker blocker(_combo);
    _value     = value;
    _untouched = true;
    _combo->setCurrentIndex(-1);
    _combo->setEditText(value.toString());
}
void ShellSelector::setChangedCallback(std::function<void()> callback)
{
    _changed = std::move(callback);
}
QString ShellSelector::inputError() const
{
    const auto current = value();
    if (current.metaType().id() != QMetaType::QString)
    {
        return tr("Shell executable must be a string.");
    }
    if (current.toString().size() > 32767 || current.toString().contains(QChar::Null))
    {
        return tr("Shell executable must contain at most 32767 characters without NUL.");
    }
    return {};
}
