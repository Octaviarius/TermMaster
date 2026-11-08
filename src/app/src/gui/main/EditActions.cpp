#include "EditActions.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QStyle>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <Termaster/Core/ActionContainer.h>
#include <Termaster/Core/CommandRegistry.h>

EditActions::EditActions(QMainWindow& window, CommandRegistry& commands, ActionContainer& menu, QObject* owner) :
    QObject(owner ? owner : &window), _window(&window)
{
    struct Definition
    {
        const char*               id;
        const char*               text;
        const char*               icon;
        const char*               group;
        QKeySequence::StandardKey shortcut;
    };
    const std::array definitions {
        Definition {     "Edit.Undo",QT_TR_NOOP("&Undo"),"edit-undo",   "history",QKeySequence::Undo                                                                                        },
        Definition {     "Edit.Redo",  QT_TR_NOOP("&Redo"),    "edit-redo",   "history",  QKeySequence::Redo},
        Definition {      "Edit.Cut",   QT_TR_NOOP("Cu&t"),     "edit-cut", "clipboard",   QKeySequence::Cut},
        Definition {     "Edit.Copy",  QT_TR_NOOP("&Copy"),    "edit-copy", "clipboard",  QKeySequence::Copy},
        Definition {    "Edit.Paste", QT_TR_NOOP("&Paste"),   "edit-paste", "clipboard", QKeySequence::Paste},
        Definition {"Edit.SelectAll",
                    QT_TR_NOOP("Select &All"),
                    "edit-select-all", "selection",
                    QKeySequence::SelectAll                                                                 }
    };
    for (std::size_t index = 0; index < definitions.size(); ++index)
    {
        const auto& definition = definitions[index];
        QIcon       icon       = QIcon::fromTheme(QString::fromLatin1(definition.icon));
        if (icon.isNull() && index < 2)
        {
            icon = window.style()->standardIcon(index == 0 ? QStyle::SP_ArrowBack : QStyle::SP_ArrowForward);
        }
        auto* command   = commands.create(QString::fromLatin1(definition.id),
                                          tr(definition.text),
                                          icon,
                                          QKeySequence(definition.shortcut));
        auto* action    = command->newAction(&window);
        _actions[index] = action;
        menu.addAction(action, QString::fromLatin1(definition.group));
        window.addAction(action);
        connect(action, &QAction::triggered, this, [this, operation = static_cast<Operation>(index)] {
            _execute(operation);
        });
    }
    window.installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this, [this] { _refresh(); });
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] { _refresh(); });
    _refresh();
}

bool EditActions::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == _window && (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate))
        || (watched == _target && event->type() == QEvent::ReadOnlyChange))
    {
        QTimer::singleShot(0, this, [this] { _refresh(); });
    }
    return QObject::eventFilter(watched, event);
}

QWidget* EditActions::_currentEditor() const
{
    if (!_window || QApplication::activeWindow() != _window)
    {
        return nullptr;
    }
    auto* widget = _window->focusWidget();
    if (!widget || widget->window() != _window)
    {
        return nullptr;
    }
    return qobject_cast<QLineEdit*>(widget) || qobject_cast<QTextEdit*>(widget)
                || qobject_cast<QPlainTextEdit*>(widget) ?
               widget :
               nullptr;
}

void EditActions::_observe(QWidget* target)
{
    if (_target == target)
    {
        return;
    }
    for (const auto& connection : _targetConnections)
    {
        disconnect(connection);
    }
    _targetConnections.clear();
    if (_target)
    {
        _target->removeEventFilter(this);
    }
    _target = target;
    if (!target)
    {
        return;
    }
    target->installEventFilter(this);
    const auto update = [this] { _refresh(); };
    _targetConnections.append(connect(target, &QObject::destroyed, this, update));
    if (auto* editor = qobject_cast<QLineEdit*>(target))
    {
        _targetConnections.append(connect(editor, &QLineEdit::selectionChanged, this, update));
        _targetConnections.append(connect(editor, &QLineEdit::textChanged, this, update));
    }
    else if (auto* editor = qobject_cast<QTextEdit*>(target))
    {
        _targetConnections.append(connect(editor, &QTextEdit::selectionChanged, this, update));
        _targetConnections.append(connect(editor, &QTextEdit::textChanged, this, update));
        _targetConnections.append(connect(editor, &QTextEdit::undoAvailable, this, update));
        _targetConnections.append(connect(editor, &QTextEdit::redoAvailable, this, update));
    }
    else if (auto* editor = qobject_cast<QPlainTextEdit*>(target))
    {
        _targetConnections.append(connect(editor, &QPlainTextEdit::selectionChanged, this, update));
        _targetConnections.append(connect(editor, &QPlainTextEdit::textChanged, this, update));
        _targetConnections.append(connect(editor, &QPlainTextEdit::undoAvailable, this, update));
        _targetConnections.append(connect(editor, &QPlainTextEdit::redoAvailable, this, update));
    }
}

void EditActions::_refresh()
{
    auto* target = _currentEditor();
    _observe(target);
    bool writable  = false;
    bool selection = false;
    bool content   = false;
    bool undo      = false;
    bool redo      = false;
    if (auto* editor = qobject_cast<QLineEdit*>(target))
    {
        writable  = !editor->isReadOnly();
        selection = editor->hasSelectedText();
        content   = !editor->text().isEmpty();
        undo      = editor->isUndoAvailable();
        redo      = editor->isRedoAvailable();
    }
    else if (auto* editor = qobject_cast<QTextEdit*>(target))
    {
        writable  = !editor->isReadOnly();
        selection = editor->textCursor().hasSelection();
        content   = !editor->document()->isEmpty();
        undo      = editor->document()->isUndoAvailable();
        redo      = editor->document()->isRedoAvailable();
    }
    else if (auto* editor = qobject_cast<QPlainTextEdit*>(target))
    {
        writable  = !editor->isReadOnly();
        selection = editor->textCursor().hasSelection();
        content   = !editor->document()->isEmpty();
        undo      = editor->document()->isUndoAvailable();
        redo      = editor->document()->isRedoAvailable();
    }
    const auto*      clipboard = QApplication::clipboard()->mimeData();
    const bool       paste     = clipboard && clipboard->hasText() && !clipboard->text().isEmpty();
    const bool       usable    = target && target->isEnabled();
    const std::array enabled {writable && undo,
                              writable && redo,
                              writable && selection,
                              selection,
                              writable && paste,
                              content};
    for (std::size_t index = 0; index < _actions.size(); ++index)
    {
        if (_actions[index])
        {
            _actions[index]->setEnabled(usable && enabled[index]);
        }
    }
}

void EditActions::_execute(Operation operation)
{
    _refresh();
    const auto              index = static_cast<std::size_t>(operation);
    const QPointer<QWidget> target(_currentEditor());
    if (!target || !_actions[index] || !_actions[index]->isEnabled())
    {
        return;
    }
    static constexpr std::array methods {"undo", "redo", "cut", "copy", "paste", "selectAll"};
    QMetaObject::invokeMethod(target.data(), methods[index], Qt::DirectConnection);
    _refresh();
}
