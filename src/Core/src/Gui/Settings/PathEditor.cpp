#include "PathEditor.h"

#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <algorithm>
#include <limits>

PathEditor::PathEditor(const SettingsSchema::Field& field, QWidget* parent) :
    SettingsFieldEditor(parent), _line(new QLineEdit(this)), _browse(new QToolButton(this)),
    _directory(field.widget == "directoryPath"), _save(field.presentation.value("mode").toString() == "save"),
    _title(field.presentation
               .value("title",
                      _directory ? tr("Select directory") :
                      _save      ? tr("Select output file") :
                                   tr("Select file"))
               .toString()),
    _filter(field.presentation.value("filter", tr("All files (*)")).toString())
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(_line, 1);
    layout->addWidget(_browse, 0, Qt::AlignVCenter);
    _line->setObjectName("pathText");
    _line->setAccessibleName(field.title);
    _line->setMaxLength(
        std::clamp(field.definition.value("maxLength", 32767).toInt(), 0, std::numeric_limits<int>::max()));
    _line->setReadOnly(field.readOnly);
    _line->installEventFilter(this);
    _browse->setObjectName("browsePath");
    _browse->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    _browse->setToolTip(_title);
    _browse->setAccessibleName(_title);
    _browse->setEnabled(!field.readOnly);
    setFocusProxy(_line);
    connect(_line, &QLineEdit::textChanged, this, [this] {
        _untouched = false;
        emit valueChanged();
    });
    connect(_browse, &QToolButton::clicked, this, [this] { _choose(); });
    _alignButton();
}

QVariant PathEditor::value() const
{
    return _untouched ? _value : QVariant(_line->text());
}

void PathEditor::setValue(const QVariant& value)
{
    const QSignalBlocker blocker(_line);
    _value     = value;
    _untouched = true;
    _line->setText(value.toString());
}

bool PathEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _line
        && (event->type() == QEvent::Resize || event->type() == QEvent::StyleChange
            || event->type() == QEvent::FontChange))
    {
        _alignButton();
    }
    return SettingsFieldEditor::eventFilter(watched, event);
}

void PathEditor::_choose()
{
    if (_dialog)
    {
        _dialog->raise();
        _dialog->activateWindow();
        return;
    }
    auto* dialog = new QFileDialog(this, _title);
    _dialog      = dialog;
    dialog->setObjectName("pathDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setAcceptMode(_save && !_directory ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog->setFileMode(_directory ? QFileDialog::Directory : _save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    if (_directory)
    {
        dialog->setOption(QFileDialog::ShowDirsOnly);
        if (!_line->text().isEmpty())
        {
            dialog->setDirectory(_line->text());
        }
    }
    else
    {
        dialog->setNameFilter(_filter);
        // Choosing a path changes a draft; it neither truncates nor overwrites the file.
        dialog->setOption(QFileDialog::DontConfirmOverwrite);
        if (!_line->text().isEmpty())
        {
            dialog->setDirectory(QFileInfo(_line->text()).absolutePath());
            dialog->selectFile(QFileInfo(_line->text()).fileName());
        }
    }
    connect(dialog, &QFileDialog::fileSelected, this, [this](const QString& path) { _line->setText(path); });
    dialog->open();
}

void PathEditor::_alignButton()
{
    const int size = controlVisualHeight(_line);
    _browse->setFixedSize(size, size);
}
