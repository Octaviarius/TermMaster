#include "CommandEditor.h"

#include <QComboBox>
#include <QCompleter>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace
{
class PasteEditor final : public QPlainTextEdit
{
public:
    std::function<void(const QString&)> pasteText;
    explicit PasteEditor(QWidget* parent) : QPlainTextEdit(parent)
    {
    }

protected:
    void insertFromMimeData(const QMimeData* source) override
    {
        if (source && source->hasText() && pasteText)
        {
            pasteText(source->text());
        }
    }
};
} // namespace

CommandEditor::CommandEditor(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("commandEditor"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* options = new QHBoxLayout;
    _mode         = new QComboBox(this);
    _mode->setObjectName(QStringLiteral("commandInputMode"));
    _mode->addItem(tr("Interactive"), QStringLiteral("interactive"));
    _mode->addItem(tr("Buffered"), QStringLiteral("buffered"));
    _mode->setToolTip(tr("Send keys immediately or edit commands before sending"));
    options->addWidget(_mode);
    _terminator = new QComboBox(this);
    _terminator->setObjectName(QStringLiteral("commandTerminator"));
    for (const auto& value : {"LF", "CRLF", "CR", "NUL", "None", "Custom"})
    {
        _terminator->addItem(QString::fromLatin1(value), QString::fromLatin1(value).toLower());
    }
    _terminator->setToolTip(tr("Bytes appended once to each submitted command"));
    options->addWidget(_terminator);
    _custom = new QLineEdit(this);
    _custom->setObjectName(QStringLiteral("commandCustomTerminator"));
    _custom->setMaxLength(1024);
    _custom->setPlaceholderText(tr("Custom escaped bytes"));
    _custom->setToolTip(tr("Escapes: \\r, \\n, \\0, \\\\, \\xHH"));
    options->addWidget(_custom);
    _pastePolicy = new QComboBox(this);
    _pastePolicy->setObjectName(QStringLiteral("commandPastePolicy"));
    _pastePolicy->addItem(tr("Paste for editing"), QStringLiteral("edit"));
    _pastePolicy->addItem(tr("Paste immediately"), QStringLiteral("immediate"));
    options->addWidget(_pastePolicy);
    options->addStretch();
    layout->addLayout(options);
    _body            = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    auto* editor      = new PasteEditor(_body);
    _editor           = editor;
    editor->pasteText = [this](const QString& text) { _paste(text); };
    _editor->setObjectName(QStringLiteral("commandDraft"));
    _editor->setPlaceholderText(tr("Enter sends; Shift+Enter inserts a new line"));
    _editor->setMaximumHeight(120);
    _editor->setTabChangesFocus(true);
    _editor->installEventFilter(this);
    setFocusProxy(_editor);
    bodyLayout->addWidget(_editor);
    auto* sendRow = new QHBoxLayout;
    _history      = new QComboBox(_body);
    _history->setObjectName(QStringLiteral("commandHistory"));
    _history->setEditable(true);
    _history->setInsertPolicy(QComboBox::NoInsert);
    _history->lineEdit()->setPlaceholderText(tr("Search command history"));
    _history->completer()->setFilterMode(Qt::MatchContains);
    _history->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    sendRow->addWidget(_history, 1);
    auto* send = new QPushButton(tr("Send"), _body);
    send->setObjectName(QStringLiteral("commandSend"));
    send->setToolTip(tr("Submit the command draft"));
    sendRow->addWidget(send);
    bodyLayout->addLayout(sendRow);
    layout->addWidget(_body);
    connect(send, &QPushButton::clicked, this, [this] { _submit(_editor->toPlainText(), true); });
    connect(_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (_editor->toPlainText().size() > _maximumCharacters)
        {
            auto cursor = _editor->textCursor();
            cursor.setPosition(_maximumCharacters);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        }
    });
    connect(_history, &QComboBox::activated, this, [this](int index) {
        if (index >= 0 && index < _entries.size())
        {
            _editor->setPlainText(_entries.at(index));
            _editor->setFocus();
        }
    });
    connect(_mode, &QComboBox::activated, this, [this] {
        emit settingChanged(QStringLiteral("mode"), _mode->currentData());
    });
    connect(_terminator, &QComboBox::activated, this, [this] {
        emit settingChanged(QStringLiteral("terminator"), _terminator->currentData());
    });
    connect(_custom, &QLineEdit::editingFinished, this, [this] {
        emit settingChanged(QStringLiteral("customTerminator"), _custom->text());
    });
    connect(_pastePolicy, &QComboBox::activated, this, [this] {
        emit settingChanged(QStringLiteral("pastePolicy"), _pastePolicy->currentData());
    });
    setSettings({});
}

void CommandEditor::setSettings(const QVariantMap& input)
{
    _settings = input;
    const QSignalBlocker modeBlock(_mode);
    const QSignalBlocker terminatorBlock(_terminator);
    const QSignalBlocker customBlock(_custom);
    const QSignalBlocker pasteBlock(_pastePolicy);
    auto                 select = [](QComboBox* combo, const QVariant& value) {
        const int index = combo->findData(value);
        combo->setCurrentIndex(index < 0 ? 0 : index);
    };
    select(_mode, input.value(QStringLiteral("mode"), QStringLiteral("interactive")));
    select(_terminator, input.value(QStringLiteral("terminator"), QStringLiteral("cr")));
    _custom->setText(input.value(QStringLiteral("customTerminator")).toString());
    select(_pastePolicy, input.value(QStringLiteral("pastePolicy"), QStringLiteral("edit")));
    _body->setVisible(buffered());
    _terminator->setEnabled(buffered());
    _custom->setVisible(_terminator->currentData().toString() == QStringLiteral("custom"));
    _custom->setEnabled(buffered());
}

void CommandEditor::setSendCallback(SendCallback callback)
{
    _send = std::move(callback);
}

bool CommandEditor::buffered() const
{
    return _settings.value(QStringLiteral("mode")).toString() == QStringLiteral("buffered");
}

void CommandEditor::retainPaste(const QString& text)
{
    if (!buffered())
    {
        emit settingChanged(QStringLiteral("mode"), QStringLiteral("buffered"));
    }
    const int available = _maximumCharacters - _editor->toPlainText().size();
    if (text.size() > available)
    {
        QMessageBox::warning(this, tr("Paste too large"), tr("Paste exceeds the command editor limit."));
        return;
    }
    _editor->insertPlainText(text);
    _editor->setFocus();
}

void CommandEditor::pasteText(const QString& text)
{
    _paste(text);
}

bool CommandEditor::confirmPaste(const QByteArray& bytes)
{
    if (!_settings.value(QStringLiteral("confirmMultiline"), true).toBool()
        || (!bytes.contains('\n') && !bytes.contains('\r')))
    {
        return true;
    }
    return QMessageBox::question(this,
                                 tr("Send multiline input"),
                                 tr("Send input containing line breaks to the terminal?"),
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No)
        == QMessageBox::Yes;
}

bool CommandEditor::eventFilter(QObject* object, QEvent* event)
{
    if (object == _editor && event->type() == QEvent::KeyPress)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !(key->modifiers() & Qt::ShiftModifier))
        {
            _submit(_editor->toPlainText(), true);
            return true;
        }
        if (key->modifiers() == Qt::NoModifier && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down))
        {
            const auto cursor = _editor->textCursor();
            if ((key->key() == Qt::Key_Up && cursor.blockNumber() == 0)
                || (key->key() == Qt::Key_Down && cursor.atEnd()))
            {
                _navigateHistory(key->key() == Qt::Key_Up ? -1 : 1);
                return true;
            }
        }
    }
    return QWidget::eventFilter(object, event);
}

bool CommandEditor::_submit(const QString& text, bool clearDraft)
{
    if (text.size() > _maximumCharacters || !_send)
    {
        return false;
    }
    const QPointer<CommandEditor> guard(this);
    if (!confirmPaste(text.toUtf8()) || !guard)
    {
        return false;
    }
    const auto send = _send;
    if (!send(text, _terminator->currentData().toString(), _custom->text()) || !guard)
    {
        return false;
    }

    _remember(text);
    if (clearDraft)
    {
        _editor->clear();
    }
    return true;
}

void CommandEditor::_paste(const QString& text)
{
    if (_pastePolicy->currentData().toString() == QStringLiteral("immediate"))
    {
        if (text.size() > _maximumCharacters)
        {
            QMessageBox::warning(this, tr("Paste too large"), tr("Paste exceeds the command editor limit."));
            return;
        }
        _submit(text, false);
    }
    else
    {
        retainPaste(text);
    }
}

void CommandEditor::_navigateHistory(int direction)
{
    if (_entries.isEmpty())
    {
        return;
    }
    if (_historyIndex == _entries.size())
    {
        _savedDraft = _editor->toPlainText();
    }
    _historyIndex = qBound(0, _historyIndex + direction, static_cast<int>(_entries.size()));
    _editor->setPlainText(_historyIndex == _entries.size() ? _savedDraft : _entries.at(_historyIndex));
    auto cursor = _editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    _editor->setTextCursor(cursor);
}

void CommandEditor::_remember(const QString& text)
{
    if (!text.isEmpty() && (_entries.isEmpty() || _entries.last() != text))
    {
        _entries.append(text);
        _historyBytes += text.toUtf8().size();
        while (_entries.size() > 100 || _historyBytes > _maximumHistoryBytes)
        {
            _historyBytes -= _entries.takeFirst().toUtf8().size();
        }
        const QSignalBlocker blocker(_history);
        _history->clear();
        _history->addItems(_entries);
        _history->setCurrentIndex(-1);
        _history->clearEditText();
    }
    _historyIndex = _entries.size();
    _savedDraft.clear();
}
