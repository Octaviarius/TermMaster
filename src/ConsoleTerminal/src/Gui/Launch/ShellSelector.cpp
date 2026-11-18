#include "ShellSelector.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>

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
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(_combo, 1);
    auto* browse = new QToolButton(this);
    browse->setObjectName("browseShell");
    browse->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    browse->setToolTip(tr("Select shell executable"));
    browse->setAccessibleName(tr("Select shell executable"));
    const int buttonSize = _combo->sizeHint().height();
    browse->setFixedSize(buttonSize, buttonSize);
    layout->addWidget(browse, 0, Qt::AlignVCenter);
    connect(browse, &QToolButton::clicked, this, [this] {
        if (_dialog)
        {
            _dialog->raise();
            _dialog->activateWindow();
            return;
        }
        auto* dialog = new QFileDialog(this, tr("Select shell executable"));
        _dialog      = dialog;
        dialog->setObjectName("shellFileDialog");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setFileMode(QFileDialog::ExistingFile);
        if (!_combo->currentText().isEmpty())
        {
            dialog->selectFile(_combo->currentText());
        }
        connect(dialog, &QFileDialog::fileSelected, this, [this](const QString& path) { _combo->setEditText(path); });
        dialog->open();
    });
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
