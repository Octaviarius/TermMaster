#include "CommandEditor.h"

#include <QComboBox>
#include <QCompleter>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
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
#include <QScreen>
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
    setObjectName("commandEditor");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 3, 4, 3);
    layout->setSpacing(4);
    auto* options = new QHBoxLayout;
    options->setSpacing(6);
    _terminator = new QComboBox(this);
    _terminator->setObjectName("commandTerminator");
    for (const auto& value : {"LF", "CRLF", "CR", "NUL", "None", "Custom"})
    {
        _terminator->addItem(QString::fromLatin1(value), QString::fromLatin1(value).toLower());
    }
    _terminator->setToolTip(tr("Append these bytes once after each command"));
    _terminator->setMaximumWidth(95);
    options->addWidget(_terminator);
    _custom = new QLineEdit(this);
    _custom->setObjectName("commandCustomTerminator");
    _custom->setMaxLength(1024);
    _custom->setMaximumWidth(170);
    _custom->setPlaceholderText(tr("Escaped bytes"));
    _custom->setToolTip(tr("Escapes: \\r, \\n, \\0, \\\\, \\xHH"));
    options->addWidget(_custom);
    auto tool = [this, options](const QString& name, const QString& icon, const QString& text) {
        auto* button = new QToolButton(this);
        button->setObjectName(name);
        button->setIcon(Icons::icon(icon));
        button->setToolTip(text);
        button->setAccessibleName(text);
        button->setCheckable(true);
        options->addWidget(button);
        return button;
    };
    _historyToggle  = tool("commandHistoryToggle", "chevron-up", tr("Recent commands"));
    _advancedToggle = tool("commandAdvancedToggle",
                           "folder-open",
                           tr("Sending tools: saved commands, files and repeat"));
    _sendingStatus  = new QLabel(this);
    _sendingStatus->setObjectName("commandSendingStatus");
    _sendingStatus->setMaximumWidth(200);
    _sendingStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    options->addWidget(_sendingStatus);
    _stop = tool("commandStop", "square", tr("Stop queued, file and periodic sending"));
    _stop->setCheckable(false);
    options->addStretch();
    layout->addLayout(options);

    _body            = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    auto* editor      = new PasteEditor(_body);
    _editor           = editor;
    editor->pasteText = [this](const QString& text) { _paste(text); };
    _editor->setObjectName("commandDraft");
    _editor->setPlaceholderText(tr("Enter sends · Shift+Enter inserts a line break"));
    _editor->setFixedHeight(qMax(52, fontMetrics().lineSpacing() * 3 + 12));
    _editor->setTabChangesFocus(true);
    _editor->installEventFilter(this);
    setFocusProxy(_editor);
    bodyLayout->addWidget(_editor, 1);
    auto* send = new QPushButton(Icons::icon("arrow-right"), tr("Send"), _body);
    send->setObjectName("commandSend");
    send->setToolTip(tr("Send the current draft (Enter)"));
    bodyLayout->addWidget(send, 0, Qt::AlignBottom);
    layout->addWidget(_body);

    auto popup = [this](const QString& name) {
        auto* frame = new QFrame(this, Qt::Popup);
        frame->setObjectName(name);
        frame->setFrameShape(QFrame::StyledPanel);
        frame->installEventFilter(this);
        return frame;
    };
    _historyPopup       = popup("commandHistoryPopup");
    auto* historyLayout = new QVBoxLayout(_historyPopup);
    historyLayout->addWidget(new QLabel(tr("Recent commands"), _historyPopup));
    _history = new QComboBox(_historyPopup);
    _history->setObjectName("commandHistory");
    _history->setMinimumWidth(300);
    _history->setMaximumWidth(360);
    _history->setMinimumContentsLength(28);
    _history->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _history->setEditable(true);
    _history->setInsertPolicy(QComboBox::NoInsert);
    _history->lineEdit()->setPlaceholderText(tr("Search command history"));
    _history->completer()->setFilterMode(Qt::MatchContains);
    _history->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    historyLayout->addWidget(_history);

    _advanced            = popup("commandAdvancedPanel");
    auto* advancedLayout = new QVBoxLayout(_advanced);
    advancedLayout->setSpacing(8);
    advancedLayout->addWidget(new QLabel(tr("Saved commands"), _advanced));
    auto* favoriteRow = new QHBoxLayout;
    _favorites        = new QComboBox(_advanced);
    _favorites->setObjectName("commandFavorites");
    _favorites->setMinimumWidth(180);
    _favorites->setMinimumContentsLength(20);
    _favorites->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _favorites->setToolTip(tr("Saved command and its terminator"));
    favoriteRow->addWidget(_favorites, 1);
    auto button = [this](QHBoxLayout*   row,
                         const QString& name,
                         const QString& text,
                         const QString& icon,
                         const QString& tooltip) {
        auto* result = new QPushButton(Icons::icon(icon), text, _advanced);
        result->setObjectName(name);
        result->setToolTip(tooltip);
        result->setAccessibleName(tooltip);
        row->addWidget(result);
        return result;
    };
    auto* load = button(favoriteRow, "commandFavoriteLoad", {}, "folder-open", tr("Load saved command into the draft"));
    auto* save = button(favoriteRow, "commandFavoriteSave", {}, "save", tr("Save draft as a named command"));
    auto* remove = button(favoriteRow, "commandFavoriteDelete", {}, "trash", tr("Delete selected saved command"));
    for (auto* control : {load, save, remove})
    {
        control->setFixedWidth(30);
    }
    advancedLayout->addLayout(favoriteRow);
    auto* fileRow = new QHBoxLayout;
    auto* raw = button(fileRow, "commandRawFile", tr("Send file…"), "file-plus", tr("Choose a file to send unchanged"));
    auto* lines = button(fileRow,
                         "commandLineFile",
                         tr("Send lines…"),
                         "panel-bottom",
                         tr("Send file lines with the selected terminator"));
    fileRow->addStretch();
    advancedLayout->addLayout(fileRow);
    auto* repeat = new QHBoxLayout;
    repeat->addWidget(new QLabel(tr("Repeat every"), _advanced));
    _period = new QSpinBox(_advanced);
    _period->setObjectName("commandPeriod");
    _period->setRange(100, 86400000);
    _period->setSuffix(tr(" ms"));
    _period->setMaximumWidth(135);
    repeat->addWidget(_period);
    _periodicStart = button(repeat,
                            "commandPeriodicStart",
                            tr("Start"),
                            "rotate-cw",
                            tr("Repeat the current command until stopped"));
    repeat->addStretch();
    advancedLayout->addLayout(repeat);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    _pastePolicy = new QComboBox(_advanced);
    _pastePolicy->setObjectName("commandPastePolicy");
    _pastePolicy->addItem(tr("Edit before sending"), "edit");
    _pastePolicy->addItem(tr("Send immediately"), "immediate");
    form->addRow(tr("Paste:"), _pastePolicy);
    _disconnected = new QComboBox(_advanced);
    _disconnected->setObjectName("commandDisconnectedPolicy");
    _disconnected->addItem(tr("Reject sending"), "reject");
    _disconnected->addItem(tr("Retain unsent data"), "retain");
    _disconnected->setToolTip(
        tr("Connection policy. Processing and file/repeat producers require an open connection."));
    form->addRow(tr("When disconnected:"), _disconnected);
    advancedLayout->addLayout(form);
    _advanced->hide();
    _historyPopup->hide();

    connect(_advancedToggle, &QToolButton::toggled, this, [this](bool visible) {
        if (visible)
        {
            showSendingTools();
        }
        else
        {
            _advanced->hide();
        }
    });
    connect(_historyToggle, &QToolButton::toggled, this, [this](bool visible) {
        if (visible)
        {
            _showPopup(_historyPopup, _historyToggle);
            _history->setFocus();
        }
        else
        {
            _historyPopup->hide();
        }
    });
    connect(load, &QPushButton::clicked, this, [this] {
        _advanced->hide();
        _loadFavorite();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        _advanced->hide();
        _saveFavorite();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const auto index = _favorites->currentIndex();
        if (index < 0 || index >= _favoriteEntries.size())
        {
            return;
        }
        _favoriteEntries.removeAt(index);
        _refreshFavorites();
        emit settingChanged("favorites", _favoriteEntries);
    });
    connect(raw, &QPushButton::clicked, this, [this] { requestFile(); });
    connect(lines, &QPushButton::clicked, this, [this] { requestFile(true); });
    connect(_periodicStart, &QPushButton::clicked, this, [this] {
        _advanced->hide();
        _startPeriodic();
    });
    connect(_stop, &QToolButton::clicked, this, &CommandEditor::stopRequested);
    connect(_period, &QSpinBox::valueChanged, this, [this](int value) { emit settingChanged("periodMs", value); });
    connect(_disconnected, &QComboBox::activated, this, [this] {
        emit settingChanged("disconnectedPolicy", _disconnected->currentData());
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
        if (index < 0 || index >= _entries.size())
        {
            return;
        }
        const auto text = _entries.at(index);
        _historyPopup->hide();
        _editor->setPlainText(text);
        _editor->setFocus();
    });
    connect(_terminator, &QComboBox::activated, this, [this] {
        emit settingChanged("terminator", _terminator->currentData());
    });
    connect(_custom, &QLineEdit::editingFinished, this, [this] {
        emit settingChanged("customTerminator", _custom->text());
    });
    connect(_pastePolicy, &QComboBox::activated, this, [this] {
        emit settingChanged("pastePolicy", _pastePolicy->currentData());
    });
    setSendingState(false, {});
    setSettings({});
}

void CommandEditor::setSettings(const QVariantMap& input)
{
    _settings = input;
    const QSignalBlocker periodBlock(_period);
    _period->setValue(input.value("periodMs", 1000).toInt());
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
    const QSignalBlocker terminatorBlock(_terminator);
    const QSignalBlocker customBlock(_custom);
    const QSignalBlocker pasteBlock(_pastePolicy);
    auto                 select = [](QComboBox* combo, const QVariant& value) {
        const int index = combo->findData(value);
        combo->setCurrentIndex(index < 0 ? 0 : index);
    };
    select(_terminator, input.value(QStringLiteral("terminator"), QStringLiteral("cr")));
    _custom->setText(input.value(QStringLiteral("customTerminator")).toString());
    select(_pastePolicy, input.value(QStringLiteral("pastePolicy"), QStringLiteral("edit")));
    if (!buffered())
    {
        _advanced->hide();
        _historyPopup->hide();
    }
    _updateVisibility();
}

void CommandEditor::setSendCallback(SendCallback callback)
{
    _send = std::move(callback);
}

bool CommandEditor::buffered() const
{
    return _settings.value(QStringLiteral("mode")).toString() == QStringLiteral("buffered");
}

void CommandEditor::setBuffered(bool value)
{
    if (value != buffered())
    {
        emit settingChanged(QStringLiteral("mode"), value ? QStringLiteral("buffered") : QStringLiteral("interactive"));
    }
}

void CommandEditor::requestFile(bool lines)
{
    _advanced->hide();
    emit fileRequested(lines);
}

void CommandEditor::showSendingTools()
{
    _showPopup(_advanced, buffered() ? _advancedToggle : window());
}

void CommandEditor::retainPaste(const QString& text)
{
    setBuffered(true);
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

bool CommandEditor::eventFilter(QObject* object, QEvent* event)
{
    if ((object == _advanced || object == _historyPopup) && event->type() == QEvent::Hide)
    {
        auto*                toggle = object == _advanced ? _advancedToggle : _historyToggle;
        const QSignalBlocker blocker(toggle);
        toggle->setChecked(false);
    }
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
    const auto                    send = _send;
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
    _sending = active;
    _sendingStatus->setText(active ? tr("Sending…") : status);
    _sendingStatus->setToolTip(active && status.isEmpty() ? tr("Output is being processed or queued.") : status);
    _sendingStatus->setVisible(active || !status.isEmpty());
    _stop->setVisible(active);
    _stop->setEnabled(active);
    _updateVisibility();
}

void CommandEditor::_updateVisibility()
{
    const bool editing = buffered();
    _body->setVisible(editing);
    _terminator->setEnabled(editing);
    _terminator->setVisible(editing);
    _historyToggle->setVisible(editing);
    _advancedToggle->setVisible(editing);
    _custom->setVisible(editing && _terminator->currentData().toString() == QStringLiteral("custom"));
    _custom->setEnabled(editing);
    setVisible(editing || _sending);
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

void CommandEditor::_showPopup(QWidget* popup, QWidget* anchor)
{
    popup->adjustSize();
    const auto position = anchor->mapToGlobal(QPoint(0, anchor->height()));
    auto*      screen   = QGuiApplication::screenAt(position);
    if (!screen)
    {
        screen = anchor->screen();
    }
    const auto available = screen->availableGeometry().adjusted(4, 4, -4, -4);
    const int  width     = qMin(qMax(350, popup->sizeHint().width()), available.width());
    const int  height    = qMin(popup->sizeHint().height(), available.height());
    const int  top       = position.y() + height > available.bottom() ? anchor->mapToGlobal(QPoint()).y() - height :
                                                                        position.y();
    popup->setGeometry(qBound(available.left(), position.x(), available.right() - width + 1),
                       qBound(available.top(), top, available.bottom() - height + 1),
                       width,
                       height);
    popup->show();
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
    const QString text = _editor->toPlainText();

    emit periodicRequested(text, _terminator->currentData().toString(), _custom->text(), _period->value());
}
