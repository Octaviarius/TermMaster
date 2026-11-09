#include "CommandEditor.h"

#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Icons.h>
#include <Termaster/Core/Terminal/CommandInput.h>

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
    auto* advancedToggle = new QToolButton(this);
    advancedToggle->setObjectName(QStringLiteral("commandAdvancedToggle"));
    advancedToggle->setIcon(Icons::icon("sliders-horizontal"));
    advancedToggle->setToolTip(tr("Show advanced command sending controls"));
    advancedToggle->setCheckable(true);
    options->addWidget(advancedToggle);
    _sendingStatus = new QLabel(this);
    _sendingStatus->setObjectName(QStringLiteral("commandSendingStatus"));
    options->addWidget(_sendingStatus);
    _stop = new QToolButton(this);
    _stop->setObjectName(QStringLiteral("commandStop"));
    _stop->setIcon(Icons::icon("square"));
    _stop->setToolTip(tr("Stop pending and periodic sending"));
    options->addWidget(_stop);
    connect(_stop, &QToolButton::clicked, this, &CommandEditor::stopRequested);
    setSendingState(false, {});
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
    _advanced = new QWidget(this);
    _advanced->setObjectName(QStringLiteral("commandAdvancedPanel"));
    auto* advancedLayout = new QVBoxLayout(_advanced);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    auto* favoriteRow = new QHBoxLayout;
    _favorites        = new QComboBox(_advanced);
    _favorites->setObjectName(QStringLiteral("commandFavorites"));
    _favorites->setToolTip(tr("Named commands with their own terminator"));
    favoriteRow->addWidget(_favorites, 1);
    auto addButton = [this](QHBoxLayout*   row,
                            const QString& name,
                            const QString& text,
                            const QString& icon,
                            const QString& tooltip) {
        auto* button = new QPushButton(Icons::icon(icon), text, _advanced);
        button->setObjectName(name);
        button->setToolTip(tooltip);
        row->addWidget(button);
        return button;
    };
    auto* load   = addButton(favoriteRow,
                             "commandFavoriteLoad",
                             tr("Load"),
                             "folder-open",
                             tr("Load the selected favorite into the draft"));
    auto* save   = addButton(favoriteRow,
                             "commandFavoriteSave",
                             tr("Save"),
                             "save",
                             tr("Save the draft as a named favorite"));
    auto* remove = addButton(favoriteRow,
                             "commandFavoriteDelete",
                             tr("Delete"),
                             "x",
                             tr("Delete the selected favorite"));
    advancedLayout->addLayout(favoriteRow);
    auto* pacing  = new QHBoxLayout;
    auto  addSpin = [this, pacing](const QString& name, const QString& label, int minimum, int maximum) {
        pacing->addWidget(new QLabel(label, _advanced));
        auto* spin = new QSpinBox(_advanced);
        spin->setObjectName(name);
        spin->setRange(minimum, maximum);
        spin->setSuffix(tr(" ms"));
        pacing->addWidget(spin);
        return spin;
    };
    _byteDelay = addSpin("commandByteDelay", tr("Byte delay"), 0, 60000);
    _byteDelay->setToolTip(
        tr("Delay between bytes; UTF-8 multibyte characters may be split. Unavailable for datagrams."));
    _lineDelay = addSpin("commandLineDelay", tr("Line delay"), 0, 60000);
    _lineDelay->setToolTip(tr("Delay after line boundaries; also applies to line files with custom terminators"));
    _disconnected = new QComboBox(_advanced);
    _disconnected->setObjectName(QStringLiteral("commandDisconnectedPolicy"));
    _disconnected->addItem(tr("Reject while disconnected"), "reject");
    _disconnected->addItem(tr("Retain while disconnected"), "retain");
    pacing->addWidget(_disconnected);
    advancedLayout->addLayout(pacing);
    auto* sending = new QHBoxLayout;
    auto* raw     = addButton(sending,
                              "commandRawFile",
                              tr("Raw file"),
                              "file-plus",
                              tr("Choose a file to send as raw bytes"));
    auto* lines   = addButton(sending,
                              "commandLineFile",
                              tr("Line file"),
                              "panel-bottom",
                              tr("Choose a file to send as byte-preserving lines"));
    _period       = new QSpinBox(_advanced);
    _period->setObjectName(QStringLiteral("commandPeriod"));
    _period->setRange(100, 86400000);
    _period->setSuffix(tr(" ms"));
    _period->setToolTip(tr("Periodic command interval; sending starts only when Start is pressed"));
    sending->addWidget(_period);
    auto* start = addButton(sending,
                            "commandPeriodicStart",
                            tr("Start periodic"),
                            "play",
                            tr("Start repeating the current draft with its terminator"));
    advancedLayout->addLayout(sending);
    layout->addWidget(_advanced);
    _advanced->hide();
    connect(advancedToggle, &QToolButton::toggled, _advanced, &QWidget::setVisible);
    connect(load, &QPushButton::clicked, this, &CommandEditor::_loadFavorite);
    connect(save, &QPushButton::clicked, this, &CommandEditor::_saveFavorite);
    connect(remove, &QPushButton::clicked, this, [this] {
        const int index = _favorites->currentIndex();
        if (index < 0 || index >= _favoriteEntries.size())
        {
            return;
        }
        _favoriteEntries.removeAt(index);
        _refreshFavorites();
        emit settingChanged(QStringLiteral("favorites"), _favoriteEntries);
    });
    connect(raw, &QPushButton::clicked, this, [this] { emit fileRequested(false); });
    connect(lines, &QPushButton::clicked, this, [this] { emit fileRequested(true); });
    connect(start, &QPushButton::clicked, this, &CommandEditor::_startPeriodic);
    for (auto pair : {qMakePair(_byteDelay, QStringLiteral("byteDelayMs")),
                      qMakePair(_lineDelay, QStringLiteral("lineDelayMs")),
                      qMakePair(_period, QStringLiteral("periodMs"))})
    {
        connect(pair.first, &QSpinBox::valueChanged, this, [this, key = pair.second](int value) {
            emit settingChanged(key, value);
        });
    }
    connect(_disconnected, &QComboBox::activated, this, [this] {
        emit settingChanged(QStringLiteral("disconnectedPolicy"), _disconnected->currentData());
    });
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
    for (auto pair : {qMakePair(_byteDelay, QStringLiteral("byteDelayMs")),
                      qMakePair(_lineDelay, QStringLiteral("lineDelayMs")),
                      qMakePair(_period, QStringLiteral("periodMs"))})
    {
        const QSignalBlocker blocker(pair.first);
        pair.first->setValue(input.value(pair.second, pair.first == _period ? 1000 : 0).toInt());
    }
    const QSignalBlocker disconnectedBlock(_disconnected);
    _disconnected->setCurrentIndex(input.value("disconnectedPolicy").toString() == "retain" ? 1 : 0);
    _favoriteEntries.clear();
    int favoriteBytes = 2;
    for (const auto& value : input.value("favorites").toList())
    {
        const auto entry = value.toMap();
        const int  size  = QJsonDocument::fromVariant(entry).toJson(QJsonDocument::Compact).size() + 1;
        if (_favoriteEntries.size() >= 100 || size > _maximumHistoryBytes - favoriteBytes)
        {
            break;
        }
        if (entry.value("name").toString().isEmpty() || entry.value("text").toString().size() > _maximumCharacters)
        {
            continue;
        }
        favoriteBytes += size;
        _favoriteEntries.append(entry);
    }
    _refreshFavorites();
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

void CommandEditor::setSendingState(bool active, const QString& status)
{
    _sendingStatus->setText(status);
    _sendingStatus->setVisible(active || !status.isEmpty());
    _stop->setVisible(active);
    _stop->setEnabled(active);
}

void CommandEditor::_refreshFavorites()
{
    const QSignalBlocker blocker(_favorites);
    const QString        selected = _favorites->currentText();
    _favorites->clear();
    for (const auto& value : _favoriteEntries)
    {
        _favorites->addItem(value.toMap().value("name").toString());
    }
    const int index = _favorites->findText(selected);
    if (index >= 0)
    {
        _favorites->setCurrentIndex(index);
    }
}

void CommandEditor::_loadFavorite()
{
    const int index = _favorites->currentIndex();
    if (index < 0 || index >= _favoriteEntries.size())
    {
        return;
    }
    const auto entry = _favoriteEntries.at(index).toMap();
    _editor->setPlainText(entry.value("text").toString());
    const QString terminator = entry.value("terminator", "cr").toString();
    const QString custom     = entry.value("customTerminator").toString();
    _terminator->setCurrentIndex(qMax(0, _terminator->findData(terminator)));
    _custom->setText(custom);
    _custom->setVisible(_terminator->currentData().toString() == QStringLiteral("custom"));
    const QPointer<CommandEditor> guard(this);
    emit                          settingChanged(QStringLiteral("mode"), QStringLiteral("buffered"));
    if (!guard)
    {
        return;
    }
    emit settingChanged(QStringLiteral("terminator"), terminator);
    if (!guard)
    {
        return;
    }
    emit settingChanged(QStringLiteral("customTerminator"), custom);
}

void CommandEditor::_saveFavorite()
{
    const QPointer<CommandEditor> guard(this);
    bool                          ok   = false;
    const QString                 name = QInputDialog::getText(this,
                                                               tr("Save favorite"),
                                                               tr("Name"),
                                                               QLineEdit::Normal,
                                                               _favorites->currentText(),
                                                               &ok)
                                             .trimmed();
    if (!guard || !ok || name.isEmpty())
    {
        return;
    }
    QString error;
    if (name.size() > 128
        || !CommandInput::prepare(_editor->toPlainText(),
                                  _terminator->currentData().toString(),
                                  _custom->text(),
                                  &error))
    {
        QMessageBox::warning(this,
                             tr("Invalid favorite"),
                             name.size() > 128 ? tr("Favorite name exceeds 128 characters.") : error);
        return;
    }
    int index = -1;
    for (int i = 0; i < _favoriteEntries.size(); ++i)
    {
        if (_favoriteEntries.at(i).toMap().value("name").toString() == name)
        {
            index = i;
            break;
        }
    }
    QVariantMap entry = index >= 0 ? _favoriteEntries.at(index).toMap() : QVariantMap {};
    entry.insert("name", name);
    entry.insert("text", _editor->toPlainText());
    entry.insert("terminator", _terminator->currentData());
    entry.insert("customTerminator", _custom->text());
    auto entries = _favoriteEntries;
    if (index >= 0)
    {
        entries[index] = entry;
    }
    else
    {
        entries.append(entry);
    }
    if (entries.size() > 100
        || QJsonDocument::fromVariant(entries).toJson(QJsonDocument::Compact).size() > _maximumHistoryBytes)
    {
        QMessageBox::warning(this, tr("Favorites full"), tr("Favorites exceed the 100 command or 1 MiB limit."));
        return;
    }
    _favoriteEntries = entries;
    _refreshFavorites();
    _favorites->setCurrentText(name);
    emit settingChanged(QStringLiteral("favorites"), _favoriteEntries);
}

void CommandEditor::_startPeriodic()
{
    const QString                 text = _editor->toPlainText();
    const QPointer<CommandEditor> guard(this);
    if (!confirmPaste(text.toUtf8()) || !guard)
    {
        return;
    }
    emit periodicRequested(text, _terminator->currentData().toString(), _custom->text(), _period->value());
}
