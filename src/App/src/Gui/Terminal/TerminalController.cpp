#include "TerminalController.h"

#include "../../Processing/TerminalProcessing.h"
#include "../../Terminal/AdvancedSender.h"
#include "../../Terminal/ProcessingRouter.h"
#include "../../Terminal/TerminalLineRecorder.h"
#include "../Main/MainWindow.h"
#include "../Processing/ProcessorFavorites.h"
#include "../Settings/SettingsDialog.h"
#include "CommandEditor.h"
#include "DetachedTerminalWindow.h"
#include "TerminalProcessingPanel.h"
#include "TerminalTabGroup.h"
#include "TerminalWorkspace.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHash>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Gui/Action/ToolbarActionContainer.h>
#include <Termaster/Core/Gui/Icons.h>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/NumberedName.h>
#include <Termaster/Core/Processing/LineflowEndpoint.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/Processing/ThreadedProcessor.h>
#include <Termaster/Core/Session.h>
#include <Termaster/Core/SettingsSchema.h>
#include <Termaster/Core/Terminal/CommandInput.h>
#include <Termaster/Core/Terminal/TerminalSendQueue.h>
#include <Termaster/Core/Terminal/ThreadedSerialDevice.h>
#include <Termaster/Core/Workspace.h>
#include <algorithm>
#include <stdexcept>

namespace
{
Q_LOGGING_CATEGORY(terminalTrace, "termaster.app.terminal.trace", QtInfoMsg)

QString owned(const QString& text)
{
    return QString(text.constData(), text.size());
}
bool fail(QString* error, const QString& text)
{
    if (error)
    {
        *error = owned(text);
    }
    return false;
}
std::optional<QVariant> lookup(const QVariantMap& values, const QString& path)
{
    QVariant current = values;
    for (const auto& part : path.split('/'))
    {
        if (current.typeId() != QMetaType::QVariantMap)
        {
            return std::nullopt;
        }
        const auto map = current.toMap();
        const auto it  = map.constFind(part);
        if (it == map.cend())
        {
            return std::nullopt;
        }
        current = it.value();
    }
    return current;
}
void put(QVariantMap& map, const QStringList& parts, qsizetype index, const QVariant& value)
{
    if (index == parts.size() - 1)
    {
        map.insert(parts.at(index), value);
        return;
    }
    auto child = map.value(parts.at(index)).toMap();
    put(child, parts, index + 1, value);
    map.insert(parts.at(index), child);
}
void removePath(QVariantMap& map, const QStringList& parts, qsizetype index = 0)
{
    const auto key = parts.at(index);
    if (index == parts.size() - 1)
    {
        map.remove(key);
        return;
    }
    if (map.value(key).typeId() != QMetaType::QVariantMap)
    {
        return;
    }
    auto child = map.value(key).toMap();
    removePath(child, parts, index + 1);
    if (child.isEmpty())
    {
        map.remove(key);
    }
    else
    {
        map.insert(key, child);
    }
}
class EditorAdapter final : public SettingsFieldEditor
{
public:
    EditorAdapter(std::unique_ptr<PluginHost::FieldEditor> editor, QWidget* parent) :
        SettingsFieldEditor(parent), _editor(std::move(editor))
    {
        if (!_editor || !_editor->widget())
        {
            throw std::invalid_argument("Plugin editor returned no widget");
        }
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(_editor->widget());
        const QPointer<EditorAdapter> self(this);
        _editor->setChangedCallback([self] {
            if (self)
            {
                emit self->valueChanged();
            }
        });
    }
    ~EditorAdapter() override
    {
        _editor->setChangedCallback({});
        _editor.reset();
    }
    QVariant value() const override
    {
        return _editor->value();
    }
    void setValue(const QVariant& value) override
    {
        _editor->setValue(value);
    }
    bool hasAcceptableInput() const override
    {
        return _editor->inputError().isEmpty();
    }
    QString inputError() const override
    {
        return owned(_editor->inputError());
    }

private:
    std::unique_ptr<PluginHost::FieldEditor> _editor;
};
} // namespace

struct TerminalController::Runtime
{
    QString                               id;
    QString                               type;
    QString                               name;
    SettingsOverlay                       settings;
    std::unique_ptr<SerialDevice>         device;
    std::unique_ptr<TerminalSendQueue>    sender;
    std::unique_ptr<AdvancedSender>       advanced;
    std::unique_ptr<ProcessingRouter>     processing;
    std::unique_ptr<TerminalLineRecorder> lineRecorder;
    QVariantMap                           processingConfiguration;
    QPointer<TerminalProcessingPanel>     processingPanel;
    bool                                  processingUpdate = false;
    bool                                  processingOpen   = false;
    QVariantMap                           targetConfiguration;
    QPointer<CommandEditor>               editor;
    QPointer<TerminalDisplay>             display;
    QPointer<QWidget>                     panel;
    QPointer<DetachedTerminalWindow>      window;
    QPointer<TerminalTabGroup>            group;
    bool                                  connectionRequested = false;
    QString                               lastError;
    QString                               title;
    QString                               completion;
    QTimer                                reconnect;
    explicit Runtime(SettingsOverlay configuration) : settings(std::move(configuration))
    {
    }
    ~Runtime()
    {
        connectionRequested = false;
        reconnect.stop();
        processingUpdate = true;
        if (device)
        {
            device->setCallbacks({});
        }

        lineRecorder.reset();
        advanced.reset();
        delete processingPanel.data();
        delete panel.data();
        processing.reset();
        sender.reset();
        if (device)
        {
            device->close();
            device.reset();
        }
    }
};

TerminalController::TerminalController(MainWindow&                 window,
                                       CommandRegistry&            commands,
                                       SettingsWidgetRegistry&     editors,
                                       std::function<Workspace*()> workspace,
                                       std::function<void()>       closeSettings,
                                       std::function<void()>       showSettings,
                                       QObject*                    parent) :
    QObject(parent), _window(window), _commands(commands), _editors(editors), _workspace(std::move(workspace)),
    _closeSettings(std::move(closeSettings)), _showSettings(std::move(showSettings))
{
    qApp->installEventFilter(this);
    _wireWorkspace(_window.terminalWorkspace());
    connect(qApp, &QApplication::focusChanged, this, [this] { _focusChanged(); });
    connect(qApp, &QGuiApplication::focusWindowChanged, this, [this] { _focusChanged(); });
    for (const bool requested : {true, false})
    {
        Action action;
        action.id       = requested ? "Terminal.StartAll" : "Terminal.StopAll";
        action.title    = requested ? tr("Start all") : tr("Stop all");
        action.iconName = requested ? "play" : "square";
        action.group    = "allConnections";
        action.shortcut = QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | (requested ? Qt::Key_S : Qt::Key_X));
        action.enabled  = [this, requested] { return _canSetAllConnections(requested); };
        action.trigger  = [this, requested] { _setAllConnections(requested); };
        registerAction(std::move(action));
    }

    struct ConnectionShortcut
    {
        const char*  id;
        const char*  title;
        const char*  icon;
        Action::Role role;
        Qt::Key      key;
    };
    for (const auto& definition : std::array {
             ConnectionShortcut {  "Terminal.Start",QT_TR_NOOP("Connect"),"play",    Action::Role::Connect,Qt::Key_S                                },
             ConnectionShortcut {   "Terminal.Stop",
                                 QT_TR_NOOP("Disconnect"),
                                 "square", Action::Role::Disconnect,
                                 Qt::Key_X},
             ConnectionShortcut {"Terminal.Restart",
                                 QT_TR_NOOP("Reconnect"),
                                 "rotate-cw",    Action::Role::Restart,
                                 Qt::Key_R}
    })
    {
        Action action;
        action.id       = definition.id;
        action.title    = tr(definition.title);
        action.iconName = definition.icon;
        action.group    = "connection";
        action.shortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | definition.key);
        action.visible  = [this] { return _current() != nullptr; };
        action.enabled  = [this, role = definition.role] { return !_connectionAction(role).isEmpty(); };
        action.trigger  = [this, role = definition.role] {
            const auto command = _connectionAction(role);
            if (!command.isEmpty())
            {
                _triggerAction(command, selectedTerminal(), selectedWindow());
            }
        };
        // The menu owns the shared binding; backend actions provide local controls.
        registerAction(std::move(action));
    }

    Action echo;
    echo.id       = "Terminal.LocalEcho";
    echo.title    = tr("Local echo");
    echo.iconName = "keyboard";
    echo.group    = "output";
    echo.role     = Action::Role::Clear;
    echo.shortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E);
    echo.enabled  = [this] { return _current() != nullptr; };
    echo.trigger  = [this] {
        auto* current = _current();
        if (!current)
        {
            return;
        }

        QString    error;
        const bool enabled = current->settings.value("input/localEcho", false).toBool();
        if (!current->settings.setValue("input/localEcho", !enabled, &error))
        {
            showMessage(error);
        }
    };
    registerAction(std::move(echo));
    _actions.at("Terminal.LocalEcho").action->setCheckable(true);

    Action wrap;
    wrap.id       = "View.WrapLines";
    wrap.title    = tr("Wrap lines");
    wrap.iconName = "text-wrap";
    wrap.group    = "terminal";
    wrap.role     = Action::Role::Clear;
    wrap.enabled  = [this] { return _current() != nullptr; };
    wrap.trigger  = [this] {
        auto* current = _current();
        if (!current || !_activeWindow())
        {
            return;
        }

        QString    error;
        const bool enabled = current->settings.value("appearance/wrapLines", true).toBool();
        if (!current->settings.setValue("appearance/wrapLines", !enabled, &error))
        {
            showMessage(error);
            return;
        }
        current->display->setAppearance(
            _effective(_types.at(current->type), current->settings).value("appearance").toMap());
    };
    _registerAction(std::move(wrap), _window.mainMenu().findMenu("/view"));
    _actions.at("View.WrapLines").action->setCheckable(true);

    Action status;
    status.id       = "View.StatusBar";
    status.title    = tr("Status bar");
    status.iconName = "panel-bottom";
    status.group    = "terminal";
    status.enabled  = [this] { return _current() && _current()->group; };
    status.trigger  = [this] {
        if (auto* current = _current(); current && current->group)
        {
            auto& bar = current->group->statusBar();
            bar.setVisible(bar.isHidden());
        }
    };
    _registerAction(std::move(status), _window.mainMenu().findMenu("/view"));
    _actions.at("View.StatusBar").action->setCheckable(true);

    const QStringList ids {"Terminal.BufferedInput", "Terminal.SendFile", "Terminal.Processing"};
    const QStringList titles {tr("Buffered input"), tr("Send file..."), tr("Processing")};
    const QStringList icons {"panel-bottom", "file-plus", "Processing"};
    for (int index = 0; index < ids.size(); ++index)
    {
        Action action;
        action.id       = ids[index];
        action.title    = titles[index];
        action.iconName = icons[index];
        action.group    = index == 2 ? "settings" : "output";
        action.role     = Action::Role::Additional;
        if (index == 0)
        {
            action.shortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B);
        }
        action.enabled = [this] { return _current() && _current()->editor; };
        action.trigger = [this, index] {
            auto* current = _current();
            if (!current || !current->editor)
            {
                return;
            }
            if (index == 0)
            {
                current->editor->setBuffered(!current->editor->buffered());
            }
            else if (index == 1)
            {
                current->editor->requestFile();
            }
            else if (current->processingPanel)
            {
                current->processingPanel->setVisible(current->processingPanel->isHidden());
                if (current->processingPanel->isHidden())
                {
                    current->processingPanel->findChild<QToolButton*>("processingPropertiesToggle")->setChecked(false);
                }
            }
        };
        registerAction(std::move(action));
        auto* presentation = _actions.at(ids[index]).action.data();
        presentation->setCheckable(index != 1);
    }
    Action detach;
    detach.id       = "Terminal.Detach";
    detach.title    = tr("Move to new window");
    detach.iconName = "app-window";
    detach.group    = "management";
    detach.role     = Action::Role::Additional;
    detach.visible  = [this] {
        const auto* current = _current();
        return current != nullptr;
    };
    detach.enabled = detach.visible;
    detach.trigger = [this] {
        const auto id = selectedTerminal();
        QString    error;
        if (!detachTerminal(id, &error))
        {
            showMessage(error);
        }
    };
    registerAction(std::move(detach));

    Action attach;
    attach.id       = "Terminal.Attach";
    attach.title    = tr("Return to terminal tabs");
    attach.iconName = "panels-top-left";
    attach.group    = "management";
    attach.role     = Action::Role::Additional;
    attach.visible  = [this] {
        const auto* current = _current();
        return current && current->window;
    };
    attach.enabled = attach.visible;
    attach.trigger = [this] {
        const auto id = selectedTerminal();
        QString    error;
        if (!attachTerminal(id, &error))
        {
            showMessage(error);
        }
    };
    registerAction(std::move(attach));

    Action close;
    close.id       = "Terminal.CloseView";
    close.title    = tr("Close terminal");
    close.iconName = "x";
    close.group    = "management";
    close.role     = Action::Role::Additional;
    close.visible  = [this] {
        const auto* current = _current();
        if (!current)
        {
            return false;
        }
        return std::none_of(_actions.cbegin(), _actions.cend(), [current](const auto& item) {
            return item.second.registration.role == Action::Role::Close
                && item.second.registration.terminalType == current->type;
        });
    };
    close.enabled = close.visible;
    close.trigger = [this] { closeSelectedTerminal(); };
    registerAction(std::move(close));
}
TerminalController::~TerminalController()
{
    closeAll();
}

void TerminalController::setSession(Session* session)
{
    if (_session == session)
    {
        return;
    }
    _beginBatch();
    const QPointer<TerminalController> self(this);
    const auto                         finish = qScopeGuard([self] {
        if (self)
        {
            self->_endBatch();
        }
    });
    closeAll();
    if (!self)
    {
        return;
    }
    _window.terminalWorkspace().clear();
    _focusedGroups.clear();
    _placements.clear();
    _session        = session;
    _layoutRestored = false;
    _restore();
}
void TerminalController::closeAll()
{
    _beginBatch();
    const QPointer<TerminalController> self(this);
    const auto                         finish = qScopeGuard([self] {
        if (self)
        {
            self->_endBatch();
        }
    });
    delete _creation.data();
    if (!self)
    {
        return;
    }
    while (!_terminals.empty())
    {
        _close(_terminals.begin()->first, true);
        if (!self)
        {
            return;
        }
    }
    for (const auto& window : _windows)
    {
        if (window)
        {
            window->allowClose();
            window->hide();
            window->deleteLater();
        }
    }
    _windows.clear();
}
QString TerminalController::selectedTerminal() const
{
    const auto* current = _current();
    return current ? current->id : QString {};
}
QStringList TerminalController::terminalIds() const
{
    QStringList result;
    for (const auto& [id, terminal] : _terminals)
    {
        result.append(id);
    }
    return result;
}
SettingsOverlay TerminalController::selectedSettings()
{
    if (auto* current = _current())
    {
        return current->settings;
    }
    if (_session)
    {
        return _session->settings();
    }
    throw std::logic_error("No terminal settings scope is selected");
}
QVariantMap TerminalController::workspaceSettings() const
{
    auto* workspace = _workspace ? _workspace() : nullptr;
    return workspace ? workspace->settings().values() : QVariantMap {};
}
void TerminalController::showMessage(const QString& message)
{
    if (auto* terminal = _current(); terminal && terminal->group)
    {
        terminal->group->statusBar().showMessage(owned(message));
    }
    else
    {
        _window.showMessage(owned(message));
    }
}
QObject* TerminalController::context()
{
    return this;
}
bool TerminalController::_activeWindow() const
{
    const auto* current = _current();
    const auto* host    = current && current->window ? static_cast<QWidget*>(current->window.data()) : &_window;
    return QApplication::activeWindow() == host;
}
bool TerminalController::canCreateTerminal() const
{
    return _session && _activeWindow();
}
bool TerminalController::connectionRequested(const QString& type) const
{
    const auto* terminal = _current();
    return terminal && terminal->type == type && terminal->connectionRequested;
}
bool TerminalController::hasSelectedTerminal(const QString& type) const
{
    const auto* current = _current();
    return current && current->type == type;
}
SerialDevice* TerminalController::selectedDevice(const QString& type)
{
    auto* current = _current();
    return _activeWindow() && hasSelectedTerminal(type) ? current->device.get() : nullptr;
}
QString TerminalController::_mainTerminalId() const
{
    auto* group = _focusedGroups.value(&_window).data();
    if (!group)
    {
        group = _window.terminalWorkspace().initialGroup();
    }
    auto* panel = group ? group->tabs().currentWidget() : nullptr;
    return panel ? panel->property("terminalId").toString() : QString();
}

TerminalController::Runtime* TerminalController::_current() const
{
    if (_commandTarget)
    {
        const auto it = _terminals.find(*_commandTarget);
        return it == _terminals.end() ? nullptr : it->second.get();
    }
    for (auto* window = QApplication::activeWindow(); window; window = window->parentWidget())
    {
        if (auto* group = _focusedGroups.value(window).data())
        {
            auto*      page = group->tabs().currentWidget();
            const auto it   = _terminals.find(page ? page->property("terminalId").toString() : QString());
            if (it != _terminals.end())
            {
                return it->second.get();
            }
        }
    }
    const auto it = _terminals.find(_mainTerminalId());
    return it == _terminals.end() ? nullptr : it->second.get();
}

QWidget* TerminalController::selectedWindow() const
{
    const auto* current = _current();
    return current && current->window ? static_cast<QWidget*>(current->window.data()) : &_window;
}

void TerminalController::_focusChanged()
{
    if (_movingHost)
    {
        return;
    }
    auto* focus = QApplication::focusWidget();
    for (auto* workspace : _workspaces())
    {
        for (auto* group : workspace->groups())
        {
            if (focus && (group == focus || group->isAncestorOf(focus)))
            {
                _focusedGroups[group->window()] = group;
            }
        }
    }
    const auto target  = selectedTerminal();
    const bool changed = _focusedTerminal != target;
    _focusedTerminal   = target;
    const QPointer<TerminalController> self(this);
    refreshTerminalActions();
    if (self && changed && _batchDepth == 0)
    {
        emit selectionChanged();
    }
}

bool TerminalController::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Shortcut && qobject_cast<QAction*>(watched) && _shortcutCandidate)
    {
        // Qt resolves keyboard layouts and multi-key sequences. Suppress a release
        // only after its shortcut was actually consumed, never for an unbound key.
        _shortcutReleases[_shortcutCandidate].insert(_shortcutCandidateKey);
        _shortcutCandidate.clear();
    }

    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress
        && event->type() != QEvent::KeyRelease)
    {
        return QObject::eventFilter(watched, event);
    }

    auto* display = qobject_cast<TerminalDisplay*>(watched);
    if (!display)
    {
        return QObject::eventFilter(watched, event);
    }
    if (event->type() == QEvent::KeyPress)
    {
        // A delivered press was not consumed by Qt's shortcut map.
        const auto pending = _shortcutReleases.find(watched);
        if (pending != _shortcutReleases.end())
        {
            pending->remove(static_cast<QKeyEvent*>(event)->key());
        }
        if (_shortcutCandidate == display)
        {
            _shortcutCandidate.clear();
        }
    }
    if (event->type() == QEvent::KeyRelease)
    {
        auto* key     = static_cast<QKeyEvent*>(event);
        auto  pending = _shortcutReleases.find(watched);
        if (pending != _shortcutReleases.end() && pending->remove(key->key()))
        {
            if (pending->isEmpty())
            {
                _shortcutReleases.erase(pending);
            }
            key->accept();
            return true;
        }
    }
    if (event->type() != QEvent::ShortcutOverride)
    {
        return QObject::eventFilter(watched, event);
    }

    const auto current = _terminals.find(display->property("terminalId").toString());
    if (current == _terminals.end() || current->second->display != display
        || display->window() != QApplication::activeWindow())
    {
        return QObject::eventFilter(watched, event);
    }
    auto*              key = static_cast<QKeyEvent*>(event);
    const QKeySequence pressed(key->keyCombination());
    if ((key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == (Qt::ControlModifier | Qt::ShiftModifier))
    {
        _shortcutCandidate    = display;
        _shortcutCandidateKey = key->key();
        key->ignore();
        return true;
    }
    const auto presentations = display->window()->findChildren<QAction*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* presentation : presentations)
    {
        if (!presentation || !presentation->isEnabled() || !presentation->isVisible())
        {
            continue;
        }
        for (const auto& shortcut : presentation->shortcuts())
        {
            if (!shortcut.isEmpty() && pressed.matches(shortcut) != QKeySequence::NoMatch)
            {
                _shortcutCandidate    = display;
                _shortcutCandidateKey = key->key();
                // Leave delivery to Qt's shortcut map instead of encoding terminal bytes.
                key->ignore();
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

QString TerminalController::_connectionAction(Action::Role role) const
{
    const auto* terminal = _current();
    if (!terminal)
    {
        return {};
    }
    const auto                               id = terminal->id;
    const QPointer<QWidget>                  panel(terminal->panel);
    const QPointer<const TerminalController> self(this);
    QStringList                              candidates;
    for (const auto& [command, item] : _actions)
    {
        if (item.registration.role == role
            && (item.registration.terminalType.isEmpty() || item.registration.terminalType == terminal->type))
        {
            candidates.append(command);
        }
    }
    for (const auto& command : candidates)
    {
        const auto found = _actions.find(command);
        if (found == _actions.end())
        {
            continue;
        }
        const auto item     = found->second;
        bool       eligible = false;
        try
        {
            eligible = (!item.registration.visible || item.registration.visible())
                    && (!item.registration.enabled || item.registration.enabled());
        }
        catch (const std::exception&)
        {
            eligible = false;
        }
        if (!self || !panel)
        {
            return {};
        }
        const auto live       = _terminals.find(id);
        const auto registered = _actions.find(command);
        if (live == _terminals.end() || live->second->panel != panel || _current() != live->second.get())
        {
            return {};
        }
        if (eligible && registered != _actions.end() && registered->second.action == item.action)
        {
            return command;
        }
    }
    return {};
}

void TerminalController::registerTerminalType(TerminalType type)
{
    if (type.id.isEmpty() || type.plugin.isEmpty() || !type.createDevice || _types.contains(type.id))
    {
        throw std::invalid_argument("Invalid or duplicate terminal type registration");
    }
    auto       schema            = std::make_shared<SettingsSchema>(type.settingsSchema);
    const auto id                = owned(type.id);
    type.id                      = id;
    type.plugin                  = owned(type.plugin);
    type.title                   = owned(type.title);
    type.settingsSchema          = QByteArray(type.settingsSchema.constData(), type.settingsSchema.size());
    auto workspaceSchema         = type.workspaceSettingsSchema.isEmpty() ?
                                       std::shared_ptr<SettingsSchema> {} :
                                       std::make_shared<SettingsSchema>(type.workspaceSettingsSchema);
    type.workspaceSettingsSchema = QByteArray(type.workspaceSettingsSchema.constData(),
                                              type.workspaceSettingsSchema.size());
    auto defaultSchema           = type.defaultSettingsSchema.isEmpty() ?
                                       schema :
                                       std::make_shared<SettingsSchema>(type.defaultSettingsSchema);
    type.defaultSettingsSchema = QByteArray(type.defaultSettingsSchema.constData(), type.defaultSettingsSchema.size());
    _types.emplace(id, Type {std::move(type), std::move(schema), std::move(workspaceSchema), std::move(defaultSchema)});
    _restore();
}
bool TerminalController::unregisterTerminalType(const QString& type, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!_types.contains(type))
    {
        return true;
    }
    if (_session && !(capturePresentation(error) && _session->sync(error)))
    {
        return false;
    }
    _closeSettings();
    delete _creation.data();
    QStringList remove;
    for (const auto& [id, terminal] : _terminals)
    {
        if (terminal->type == type)
        {
            remove.append(id);
        }
    }
    for (const auto& id : remove)
    {
        _close(id, true);
    }
    _types.erase(type);
    refreshTerminalActions();
    return true;
}
void TerminalController::registerAction(Action action)
{
    _registerAction(std::move(action), _window.mainMenu().findMenu("/terminal"));
}

void TerminalController::_registerAction(Action action, ActionContainer* menu)
{
    if (!action.iconName.isEmpty())
    {
        action.icon = Icons::icon(action.iconName);
        action.iconName.clear();
    }
    if (!action.trigger || action.icon.isNull() || _actions.contains(action.id))
    {
        throw std::invalid_argument("Invalid terminal action registration");
    }
    if (menu && !menu->groups().contains(action.group))
    {
        throw std::invalid_argument("Unknown action group");
    }
    if (action.role == Action::Role::Connect || action.role == Action::Role::Disconnect
        || action.role == Action::Role::Restart)
    {
        menu = nullptr;
    }

    action.terminalType     = owned(action.terminalType);
    action.id               = owned(action.id);
    action.title            = owned(action.title);
    action.group            = owned(action.group);
    const auto id           = action.id;
    auto*      command      = _commands.create(id, action.title, action.icon, action.shortcut);
    QAction*   presentation = nullptr;
    try
    {
        presentation = command->newAction(&_window);
        presentation->setToolTip(action.title);
        if (menu)
        {
            menu->addAction(presentation, action.group);
        }
        if (action.role == Action::Role::Create)
        {
            const auto group = "terminalCreation";
            qsizetype  index = 0;
            for (const auto* existing : _window.toolbar().actions(group))
            {
                const auto it = std::find_if(_actions.cbegin(), _actions.cend(), [existing](const auto& item) {
                    return item.second.action == existing;
                });
                if (it != _actions.cend()
                    && (it->second.registration.role < action.role
                        || (it->second.registration.role == action.role && it->first < id)))
                {
                    ++index;
                }
            }
            _window.toolbar().insertAction(presentation, group, index);
        }
        _actions.emplace(id, RegisteredAction {std::move(action), presentation, menu});
    }
    catch (...)
    {
        if (presentation)
        {
            if (menu)
            {
                menu->removeAction(presentation);
            }
            _window.toolbar().removeAction(presentation);
            delete presentation;
        }
        _commands.remove(id);
        throw;
    }
    presentation->setObjectName(id);
    presentation->setShortcutContext(Qt::WindowShortcut);
    _window.addAction(presentation);
    connect(presentation, &QAction::triggered, this, [this, id] { _triggerAction(id, _mainTerminalId(), &_window); });
    refreshTerminalActions();
}
void TerminalController::unregisterAction(const QString& id)
{
    auto it = _actions.find(id);
    if (it == _actions.end())
    {
        return;
    }
    for (auto* workspace : _workspaces())
    {
        for (auto* group : workspace->groups())
        {
            delete group->action(id);
            group->setAction(id, nullptr);
            group->setProperty("actionLayout", QVariant());
        }
    }
    for (const auto& window : _windows)
    {
        if (window)
        {
            delete window->findChild<QAction*>(id, Qt::FindDirectChildrenOnly);
        }
    }
    auto* action = it->second.action.data();
    if (action)
    {
        if (it->second.menu)
        {
            it->second.menu->removeAction(action);
        }
        _window.toolbar().removeAction(action);
        delete action;
    }
    _commands.remove(id);
    _actions.erase(it);
}
void TerminalController::registerEditor(const QString& id, EditorFactory factory)
{
    _editors.registerWidget(id, [factory = std::move(factory)](const SettingsSchema::Field& field, QWidget* parent) {
        return new EditorAdapter(factory(field.presentation, parent), parent);
    });
}
void TerminalController::unregisterEditor(const QString& id)
{
    _closeSettings();
    for (const auto& [key, terminal] : _terminals)
    {
        if (terminal->processingPanel)
        {
            terminal->processingPanel->releaseEditors();
        }
    }
    _editors.unregisterWidget(id);
}
void TerminalController::refreshTerminalActions()
{
    if (_batchDepth > 0 || _refreshingActions || _movingHost)
    {
        return;
    }
    const QPointer<TerminalController> self(this);
    _refreshingActions       = true;
    const auto finishRefresh = qScopeGuard([self] {
        if (self)
        {
            self->_refreshingActions = false;
        }
    });
    {
        const auto previousTarget = _commandTarget;
        _commandTarget            = _mainTerminalId();
        const auto  restoreTarget = qScopeGuard([self, previousTarget] {
            if (self)
            {
                self->_commandTarget = previousTarget;
            }
        });
        QStringList commands;
        for (const auto& [id, item] : _actions)
        {
            commands.append(id);
        }
        for (const auto& id : commands)
        {
            const auto it = _actions.find(id);
            if (it == _actions.end() || !it->second.action)
            {
                continue;
            }
            const auto              item   = it->second;
            const QPointer<QAction> action = item.action;
            try
            {
                const bool visible = !item.registration.visible || item.registration.visible();
                if (!self)
                {
                    return;
                }
                const bool enabled = _activeWindow() && (!item.registration.enabled || item.registration.enabled());
                if (!self)
                {
                    return;
                }
                if (!action)
                {
                    continue;
                }
                action->setChecked(_actionChecked(id, _current()));
                if (!self)
                {
                    return;
                }
                if (!action)
                {
                    continue;
                }
                action->setVisible(visible);
                if (!self)
                {
                    return;
                }
                if (action)
                {
                    action->setEnabled(enabled);
                }
            }
            catch (const std::exception&)
            {
                if (action)
                {
                    action->setEnabled(false);
                }
            }
            if (!self)
            {
                return;
            }
        }
    }

    QStringList detached;
    for (const auto& [id, terminal] : _terminals)
    {
        if (terminal->group && terminal->group->tabs().currentWidget() == terminal->panel)
        {
            detached.append(id);
        }
    }
    for (const auto& id : detached)
    {
        _refreshHostActions(id);
        if (!self)
        {
            return;
        }
    }
    for (auto* workspace : _workspaces())
    {
        for (auto* group : workspace->groups())
        {
            if (!group->tabs().currentWidget())
            {
                group->statusBar().clearMessage();
                _refreshGroupControls(group);
            }
        }
    }
}

bool TerminalController::_actionChecked(const QString& id, const Runtime* terminal) const
{
    if (!terminal)
    {
        return false;
    }
    if (id == "Terminal.LocalEcho")
    {
        return terminal->settings.value("input/localEcho", false).toBool();
    }
    if (id == "View.WrapLines")
    {
        return terminal->settings.value("appearance/wrapLines", true).toBool();
    }
    if (id == "View.StatusBar")
    {
        return terminal->group && !terminal->group->statusBar().isHidden();
    }
    if (id == "Terminal.BufferedInput")
    {
        return terminal->editor && terminal->editor->buffered();
    }
    if (id == "Terminal.Processing")
    {
        return terminal->processingPanel && !terminal->processingPanel->isHidden();
    }
    const auto it = _actions.find(id);
    return it != _actions.end() && it->second.action && it->second.action->isChecked();
}

void TerminalController::_triggerAction(const QString& id, const QString& terminalId, QWidget* host)
{
    if (!host || QApplication::activeWindow() != host)
    {
        return;
    }
    const auto it = _actions.find(id);
    if (it == _actions.end() || (!terminalId.isEmpty() && !_terminals.contains(terminalId)))
    {
        return;
    }
    const auto runtime = _terminals.find(terminalId);
    if (runtime != _terminals.end()
        && (runtime->second->window ? static_cast<QWidget*>(runtime->second->window.data()) : &_window) != host)
    {
        return;
    }
    const QPointer<TerminalController> self(this);
    {
        const auto previousTarget             = _commandTarget;
        _commandTarget                        = terminalId;
        const auto              restoreTarget = qScopeGuard([self, previousTarget] {
            if (self)
            {
                self->_commandTarget = previousTarget;
            }
        });
        const QPointer<QAction> source        = it->second.action;
        const QPointer<QWidget> targetHost    = host;
        const QPointer<QWidget> targetPanel   = runtime == _terminals.end() ? nullptr : runtime->second->panel.data();
        const auto              registration  = it->second.registration;
        const bool              visible       = !registration.visible || registration.visible();
        if (!self)
        {
            return;
        }
        const bool enabled = !registration.enabled || registration.enabled();
        if (!self)
        {
            return;
        }
        const auto current        = _actions.find(id);
        const auto currentRuntime = _terminals.find(terminalId);
        const bool sameTarget     = terminalId.isEmpty()
                                 || (currentRuntime != _terminals.end() && currentRuntime->second->panel == targetPanel
                                     && (currentRuntime->second->window ?
                                             static_cast<QWidget*>(currentRuntime->second->window.data()) :
                                             &_window)
                                            == targetHost);
        if (visible && enabled && source && targetHost && sameTarget && QApplication::activeWindow() == targetHost
            && current != _actions.end() && current->second.action == source)
        {
            registration.trigger();
        }
    }
    if (self)
    {
        refreshTerminalActions();
    }
}

QVariantMap TerminalController::_effective(const Type& type, SettingsOverlay settings) const
{
    auto        values = settings.values();
    QVariantMap document;
    for (const auto& field : type.schema->fields())
    {
        if (const auto value = lookup(values, field.settingsPath))
        {
            document.insert(field.key, *value);
        }
    }
    const auto explicitValues = document;
    document                  = type.schema->withDefaults(document);
    for (const auto& field : type.schema->fields())
    {
        if (!type.schema->isActive(field.key, document))
        {
            removePath(values, field.settingsPath.split('/'));
            continue;
        }
        if (!explicitValues.contains(field.key) && field.hasDefault)
        {
            put(values, field.settingsPath.split('/'), 0, field.defaultValue);
        }
    }
    return values;
}
bool TerminalController::_validate(const Type& type, SettingsOverlay settings, QString* error) const
{
    const auto  values = settings.values();
    QVariantMap document;
    for (const auto& field : type.schema->fields())
    {
        if (const auto value = lookup(values, field.settingsPath))
        {
            document.insert(field.key, *value);
        }
        else if (field.hasDefault)
        {
            document.insert(field.key, field.defaultValue);
        }
    }
    const auto errors = type.schema->validate(type.schema->activeValues(document));
    return errors.isEmpty() || fail(error, errors.first().path + ": " + errors.first().message);
}

bool TerminalController::createTerminal(const QString&     type,
                                        const QVariantMap& overrides,
                                        const QString&     name,
                                        QString*           error)
{
    if (error)
    {
        error->clear();
    }
    if (!_session || !_types.contains(type))
    {
        return fail(error, "No session or terminal type is available");
    }
    QSet<QString> occupied;
    const auto    objects = _session->objects().values();
    for (auto object = objects.cbegin(); object != objects.cend(); ++object)
    {
        occupied.insert(object.value().toMap().value("name").toString());
    }
    const auto  id    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto  label = name.isEmpty() ? numberedName("Terminal", occupied) : name;
    QVariantMap definition {
        {     "type",                                type},
        {   "module", _types.at(type).registration.plugin},
        {     "name",                               label},
        { "settings",                           overrides},
        {"connected",                               false}
    };
    if (!_session->objects().setValue(id, definition, error))
    {
        return false;
    }
    if (!_materialize(id, error))
    {
        _session->objects().remove(id);
        return false;
    }
    auto       presentation  = _session->presentation();
    const bool hadOrder      = presentation.contains("tabOrder");
    const auto previousOrder = presentation.value("tabOrder");
    if (!_rememberTabOrder(error) || !_session->sync(error))
    {
        _close(id);
        _session->objects().remove(id);
        if (hadOrder)
        {
            presentation.setValue("tabOrder", previousOrder);
        }
        else
        {
            presentation.remove("tabOrder");
        }
        return false;
    }
    refreshTerminalActions();
    return true;
}
bool TerminalController::_materialize(const QString&   id,
                                      QString*         error,
                                      TerminalDisplay* retainedDisplay,
                                      CommandEditor*   retainedEditor)
{
    if (!_session || _terminals.contains(id))
    {
        return true;
    }
    const auto definition = _session->objects().value(id).toMap();
    const auto typeId     = definition.value("type").toString();
    if (!_types.contains(typeId))
    {
        return fail(error, "Terminal module is unavailable");
    }
    auto& type = _types.at(typeId);
    if (definition.value("module").toString() != type.registration.plugin)
    {
        return fail(error, "Terminal module does not match its type");
    }
    auto terminal = std::make_unique<Runtime>(
        SettingsOverlay(_session->settings(), _session->objects().node(id + "/settings")));
    if (!_validate(type, terminal->settings, error))
    {
        return false;
    }
    terminal->id   = id;
    terminal->type = typeId;
    terminal->name = definition.value("name", id).toString();
    try
    {
        terminal->device = std::make_unique<ThreadedSerialDevice>(type.registration.createDevice);
        if (!terminal->device)
        {
            return fail(error, "Terminal device factory returned null");
        }
        terminal->display = retainedDisplay ? retainedDisplay : new TerminalDisplay;
        terminal->display->setObjectName("terminalDisplay");
        terminal->display->setProperty("terminalId", id);
        connect(terminal->display, &QObject::destroyed, this, [this, display = terminal->display.data()] {
            _shortcutReleases.remove(display);
        });
        const auto effective = _effective(type, terminal->settings);
        terminal->display->setAppearance(effective.value("appearance").toMap());
        terminal->panel = type.registration.createPanel ?
                              type.registration.createPanel(terminal->display, effective, nullptr) :
                              terminal->display.data();
        if (!terminal->panel)
        {
            if (!retainedDisplay)
            {
                delete terminal->display.data();
            }
            return fail(error, "Terminal panel factory returned null");
        }
        auto* content   = terminal->panel.data();
        auto* container = new QWidget;
        auto* layout    = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto* split        = new QSplitter(Qt::Horizontal, container);
        auto* terminalArea = new QWidget(split);
        terminalArea->setObjectName("terminalInteractionArea");
        auto* terminalLayout = new QVBoxLayout(terminalArea);
        terminalLayout->setContentsMargins(0, 0, 0, 0);
        terminalLayout->setSpacing(0);
        terminalLayout->addWidget(content, 1);
        split->addWidget(terminalArea);
        layout->addWidget(split, 1);
        terminal->editor = retainedEditor ? retainedEditor : new CommandEditor(terminalArea);
        terminal->editor->setParent(terminalArea);
        terminal->editor->setSettings(terminal->settings.value("input").toMap());
        if (terminal->advanced)
        {
            terminal->advanced->configure(terminal->settings.value("input").toMap());
        }
        terminal->display->setFocusProxy(terminal->editor->buffered() ? terminal->editor.data() : nullptr);
        terminalLayout->addWidget(terminal->editor);
        terminal->panel      = container;
        terminal->sender     = std::make_unique<TerminalSendQueue>(*terminal->device);
        terminal->processing = std::make_unique<ProcessingRouter>();
        terminal->advanced   = std::make_unique<AdvancedSender>(*terminal->sender, *terminal->device);
        terminal->advanced->configure(terminal->settings.value("input").toMap());
        terminal->targetConfiguration = effective.value("transport").toMap();
        if (_processors)
        {
            terminal->processingPanel = new TerminalProcessingPanel(
                *_processors,
                _editors,
                *terminal->processing,
                terminal->settings.value("processing").toMap(),
                [this, id](const QVariantMap& baseline, const QVariantMap& candidate, QString* error) {
                    return _applyProcessing(id, baseline, candidate, error);
                },
                terminalArea,
                _processorFavorites);
            connect(terminal->processingPanel,
                    &TerminalProcessingPanel::compositionEditorRequested,
                    this,
                    &TerminalController::compositionEditorRequested);
            split->addWidget(terminal->processingPanel->propertiesWidget());
            split->setStretchFactor(0, 1);
            split->setStretchFactor(1, 0);
            terminalLayout->addWidget(terminal->processingPanel);
            terminal->processingPanel->hide();
        }
    }
    catch (const std::exception& exception)
    {
        if (retainedDisplay)
        {
            retainedDisplay->setParent(nullptr);
        }
        else
        {
            delete terminal->display.data();
        }
        if (retainedEditor)
        {
            retainedEditor->setParent(nullptr);
        }
        return fail(error, QString::fromUtf8(exception.what()));
    }
    auto* device  = terminal->device.get();
    auto* panel   = terminal->panel.data();
    auto* display = terminal->display.data();
    terminal->reconnect.setSingleShot(true);
    terminal->reconnect.setInterval(1000);
    connect(&terminal->reconnect, &QTimer::timeout, this, [this, id] { _attemptConnection(id); });
    _terminals.emplace(id, std::move(terminal));
    const QPointer<TerminalController> self(this);
    SerialDevice::Callbacks            callbacks;
    callbacks.received = [self, id](const QByteArray& bytes) {
        if (!self)
        {
            return;
        }
        auto it = self->_terminals.find(id);
        if (it != self->_terminals.end() && it->second->display)
        {
            auto& runtime = *it->second;
            if (TerminalProcessing::mode(runtime.processingConfiguration) == "bypass")
            {
                runtime.display->receive(bytes);
                return;
            }
            QString error;
            if (runtime.processing->submit(ProcessingRouter::Direction::Rx, {bytes}, {}, &error)
                != Processor::AcceptResult::Accepted)
            {
                if (runtime.processing->diagnostics(ProcessingRouter::Direction::Rx).state
                    != ProcessingRouter::State::Failed)
                {
                    runtime.processing->fail(ProcessingRouter::Direction::Rx,
                                             tr("Receive processing rejected input: %1").arg(error));
                }
            }
        }
    };
    callbacks.stateChanged = [self, id](SerialDevice::State) {
        if (self)
        {
            self->_changed(id);
        }
    };
    callbacks.written = [self, id](qint64) {
        if (self)
        {
            const auto it = self->_terminals.find(id);
            if (it != self->_terminals.end())
            {
                it->second->sender->flush();
                it->second->processing->wake(ProcessingRouter::Direction::Tx);
            }
        }
    };
    callbacks.error = [self, id](const QString& error) {
        if (self)
        {
            auto it = self->_terminals.find(id);
            if (it != self->_terminals.end())
            {
                it->second->advanced->connectionChanged();
                if (it->second->lastError != error)
                {
                    self->_showTerminalMessage(id, error);
                }
                it->second->lastError = owned(error);
                self->_changed(id);
            }
        }
    };
    callbacks.exited = [self, id](int code) {
        if (!self)
        {
            return;
        }
        auto it = self->_terminals.find(id);
        if (it == self->_terminals.end())
        {
            return;
        }
        auto& runtime               = *it->second;
        runtime.connectionRequested = false;
        runtime.reconnect.stop();
        runtime.completion = tr("Process exited with code %1").arg(code);
        if (code == 0)
        {
            runtime.lastError.clear();
        }
        self->_changed(id);
    };
    device->setCallbacks(std::move(callbacks));
    connect(display, &TerminalDisplay::terminalSizeChanged, this, [this, id](int columns, int rows) {
        const auto it = _terminals.find(id);
        if (it != _terminals.end())
        {
            if (!_movingHost)
            {
                it->second->device->resize(columns, rows);
            }
        }
    });
    device->resize(display->terminalSize().width(), display->terminalSize().height());
    auto* editor = _terminals.at(id)->editor.data();
    connect(editor, &CommandEditor::settingChanged, this, [this, id](const QString& key, const QVariant& value) {
        const auto it = _terminals.find(id);
        if (it == _terminals.end())
        {
            return;
        }
        QString error;
        if (!it->second->settings.setValue("input/" + key, value, &error))
        {
            _showTerminalMessage(id, error);
        }
        it->second->editor->setSettings(it->second->settings.value("input").toMap());
        it->second->advanced->configure(it->second->settings.value("input").toMap());
        it->second->display->setFocusProxy(it->second->editor->buffered() ? it->second->editor.data() : nullptr);
        if (key == "mode")
        {
            if (it->second->editor->buffered())
            {
                it->second->editor->setFocus();
            }
            else
            {
                it->second->display->setFocus();
            }
        }
        refreshTerminalActions();
    });
    auto* router = _terminals.at(id)->processing.get();
    router->setOutput(ProcessingRouter::Direction::Rx,
                      [self, id](const Processor::Chunk& chunk, const TerminalSendQueue::Options&, QString*) {
                          if (!self || !self->_terminals.contains(id))
                          {
                              return Processor::AcceptResult::Rejected;
                          }
                          self->_terminals.at(id)->display->receive(chunk.bytes);
                          return Processor::AcceptResult::Accepted;
                      });
    router->setOutput(
        ProcessingRouter::Direction::Tx,
        [self, id](const Processor::Chunk& chunk, const TerminalSendQueue::Options& options, QString* error) {
            if (!self || !self->_terminals.contains(id))
            {
                return Processor::AcceptResult::Rejected;
            }
            auto& runtime = *self->_terminals.at(id);
            if (runtime.device->state() != SerialDevice::State::Open
                || chunk.bytes.size() > TerminalSendQueue::MaximumPendingBytes
                || (runtime.device->atomicWrites() && chunk.bytes.size() > 65507))
            {
                if (error)
                {
                    *error = tr("Processed output cannot be sent with the current transport state or packet limits.");
                }
                return Processor::AcceptResult::Rejected;
            }
            if (runtime.sender->pendingBytes() > TerminalSendQueue::MaximumPendingBytes - chunk.bytes.size())
            {
                return Processor::AcceptResult::WouldBlock;
            }
            return runtime.sender->enqueue(chunk.bytes, options, error) ? Processor::AcceptResult::Accepted :
                                                                          Processor::AcceptResult::Rejected;
        });
    connect(
        router,
        &ProcessingRouter::error,
        this,
        [this, id, expected = QPointer<ProcessingRouter>(router)](ProcessingRouter::Direction direction,
                                                                  const QString&              error) {
            const auto it = _terminals.find(id);
            if (it == _terminals.end() || !expected || it->second->processing.get() != expected)
            {
                return;
            }
            const QPointer<TerminalController> self(this);
            it->second->lastError = error;
            if (it->second->processing->diagnostics(ProcessingRouter::Direction::Tx).state
                == ProcessingRouter::State::Failed)
            {
                it->second->advanced->stopProducers();
            }
            if (!self)
            {
                return;
            }
            _showTerminalMessage(id,
                                 (direction == ProcessingRouter::Direction::Rx ? tr("Rx processing: ") :
                                                                                 tr("Tx processing: "))
                                     + error);
        },
        Qt::QueuedConnection);
    connect(_terminals.at(id)->sender.get(), &TerminalSendQueue::changed, router, [router] {
        router->wake(ProcessingRouter::Direction::Tx);
    });
    auto* advanced = _terminals.at(id)->advanced.get();
    connect(advanced, &AdvancedSender::stopped, this, [this, id] {
        const QPointer<TerminalController> self(this);
        const auto                         it = _terminals.find(id);
        if (it != _terminals.end() && !it->second->processingUpdate)
        {
            QString    error;
            const bool configured = _configureProcessing(*it->second, &error);
            if (self && !configured)
            {
                _showTerminalMessage(id, error);
            }
        }
    });
    connect(advanced, &AdvancedSender::stateChanged, editor, &CommandEditor::setSendingState);
    connect(advanced, &AdvancedSender::failed, this, [this, id](const QString& error) {
        _showTerminalMessage(id, error);
    });
    connect(editor, &CommandEditor::stopRequested, advanced, &AdvancedSender::stop);
    connect(editor,
            &CommandEditor::periodicRequested,
            this,
            [this, id](const QString& text, const QString& terminator, const QString& custom, int interval) {
                const auto it = _terminals.find(id);
                if (it == _terminals.end())
                {
                    return;
                }
                QString    error;
                const auto bytes = CommandInput::prepare(text, terminator, custom, &error);
                if (!bytes || !it->second->advanced->startPeriodic(*bytes, interval, &error))
                {
                    _showTerminalMessage(id, error);
                }
            });
    connect(editor, &CommandEditor::fileRequested, this, [this, id](bool lines) {
        const auto initial = _terminals.find(id);
        if (initial == _terminals.end())
        {
            return;
        }
        const QPointer<QWidget>            panel = initial->second->panel;
        const QPointer<TerminalController> self(this);
        const QPointer<QWidget>            parent = initial->second->window ?
                                                        static_cast<QWidget*>(initial->second->window.data()) :
                                                        &_window;
        const QString                      path   = QFileDialog::getOpenFileName(parent, tr("Select file to send"));
        if (!self || !parent || !panel || path.isEmpty())
        {
            return;
        }
        if (QMessageBox::question(parent,
                                  tr("Send file"),
                                  lines ?
                                      tr("Send this file as byte-preserving lines with the selected terminator?\n%1")
                                          .arg(path) :
                                      tr("Send this file as raw bytes without a terminator?\n%1").arg(path),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No)
                != QMessageBox::Yes
            || !self)
        {
            return;
        }
        const auto it = _terminals.find(id);
        if (!panel || it == _terminals.end() || it->second->panel != panel)
        {
            return;
        }
        QString    error;
        const auto input      = it->second->settings.value("input").toMap();
        const auto terminator = CommandInput::prepare({},
                                                      input.value("terminator", "cr").toString(),
                                                      input.value("customTerminator").toString(),
                                                      &error);
        if ((lines && !terminator)
            || !it->second->advanced->startFile(path, lines, lines ? *terminator : QByteArray {}, &error))
        {
            _showTerminalMessage(id, error);
        }
    });
    connect(_terminals.at(id)->sender.get(), &TerminalSendQueue::failed, this, [this, id](const QString& error) {
        _showTerminalMessage(id, error);
    });
    editor->setSendCallback([this, id](const QString& text, const QString& terminator, const QString& custom) {
        const auto it = _terminals.find(id);
        if (it == _terminals.end())
        {
            return false;
        }
        QString    error;
        const auto bytes = CommandInput::prepare(text, terminator, custom, &error);
        if (!bytes || !it->second->advanced->submit(*bytes, &error))
        {
            _showTerminalMessage(id, error);
            return false;
        }
        if (it->second->settings.value("input/localEcho", false).toBool())
        {
            it->second->display->localEcho(*bytes);
        }
        return true;
    });
    connect(display,
            &TerminalDisplay::outputBytes,
            this,
            [this, id](const QByteArray& data, TerminalDisplay::OutputKind kind, const QByteArray& echo) {
                auto it = _terminals.find(id);
                if (it == _terminals.end())
                {
                    return;
                }
                const QPointer<CommandEditor> editor = it->second->editor;
                if (kind == TerminalDisplay::OutputKind::Keyboard && editor->buffered())
                {
                    editor->setFocus();
                    return;
                }
                if (kind == TerminalDisplay::OutputKind::Paste)
                {
                    if (editor->buffered())
                    {
                        editor->pasteText(QString::fromUtf8(echo));
                        return;
                    }
                    if (it->second->settings.value("input/pastePolicy", "edit").toString() == "edit")
                    {
                        editor->retainPaste(QString::fromUtf8(echo));
                        return;
                    }
                }
                QString    error;
                const bool accepted = TerminalProcessing::mode(it->second->processingConfiguration) == "bypass" ?
                                          it->second->sender->enqueue(data, &error) :
                                          it->second->processing->submit(ProcessingRouter::Direction::Tx,
                                                                         {data},
                                                                         {},
                                                                         &error)
                                              == Processor::AcceptResult::Accepted;
                if (!accepted)
                {
                    _showTerminalMessage(id, error);
                }
                else if ((kind == TerminalDisplay::OutputKind::Keyboard || kind == TerminalDisplay::OutputKind::Paste)
                         && it->second->settings.value("input/localEcho", false).toBool())
                {
                    it->second->display->localEcho(echo);
                }
            });
    connect(display, &TerminalDisplay::linkActivated, this, [](const QUrl& url) { QDesktopServices::openUrl(url); });
    connect(display, &TerminalDisplay::titleChanged, this, [this, id](const QString& title) {
        auto it = _terminals.find(id);
        if (it != _terminals.end())
        {
            it->second->title = owned(title);
            _changed(id);
        }
    });
    panel->setProperty("terminalId", id);
    auto* group = _findGroup(_placements.value(id));
    if (!group)
    {
        group = _window.terminalWorkspace().initialGroup();
    }
    _terminals.at(id)->group  = group;
    _terminals.at(id)->window = qobject_cast<DetachedTerminalWindow*>(group->window());
    _placements.insert(id, group->id());
    auto&     tabs  = group->tabs();
    const int index = tabs.addTab(panel, _terminals.at(id)->name);
    if (_batchDepth > 0)
    {
        _batchSelected         = panel;
        _batchSelectionChanged = true;
    }
    else
    {
        tabs.setCurrentIndex(index);
        _focusedGroups[group->window()] = group;
        _window.showTerminals(true);
        display->setFocus();
    }
    _changed(id);
    return true;
}
void TerminalController::_beginBatch()
{
    if (_batchDepth++ > 0)
    {
        return;
    }
    _batchTabs.clear();
    for (auto* workspace : _workspaces())
    {
        workspace->setUpdatesEnabled(false);
        for (auto* group : workspace->groups())
        {
            _batchTabs.append({&group->tabs(), group->tabs().blockSignals(true)});
        }
    }
    _batchSelected.clear();
    _batchSelectionChanged = false;
}

void TerminalController::_endBatch()
{
    if (_batchDepth > 1)
    {
        --_batchDepth;
        return;
    }
    const QPointer<TerminalController> self(this);
    if (_batchSelected)
    {
        const auto id = _batchSelected->property("terminalId").toString();
        const auto it = _terminals.find(id);
        if (it != _terminals.end() && it->second->group)
        {
            auto* group = it->second->group.data();
            group->tabs().setCurrentWidget(_batchSelected);
            _focusedGroups[group->window()] = group;
        }
    }
    _batchSelected.clear();
    const auto tabs = _batchTabs;
    _batchTabs.clear();
    for (const auto& item : tabs)
    {
        if (item.first)
        {
            item.first->blockSignals(item.second);
        }
    }
    for (auto* workspace : _workspaces())
    {
        workspace->setUpdatesEnabled(true);
    }
    if (!self)
    {
        return;
    }
    _window.showTerminals(!_terminals.empty());
    const bool changed = _batchSelectionChanged;
    _batchDepth        = 0;
    refreshTerminalActions();
    if (self && changed)
    {
        emit selectionChanged();
    }
}

void TerminalController::_restore()
{
    _beginBatch();
    const QPointer<TerminalController> self(this);
    const auto                         finish = qScopeGuard([self] {
        if (self)
        {
            self->_endBatch();
        }
    });
    if (!_session)
    {
        return;
    }
    _restoreLayout();
    const auto    objects = _session->objects().values();
    QStringList   order;
    QSet<QString> listed;
    for (const auto& value : _session->presentation().value("tabOrder").toList())
    {
        const auto id = value.toString();
        if (objects.contains(id) && !listed.contains(id))
        {
            order.append(id);
            listed.insert(id);
        }
    }
    for (auto it = objects.cbegin(); it != objects.cend(); ++it)
    {
        if (!listed.contains(it.key()))
        {
            order.append(it.key());
        }
    }

    QStringList connections;
    int         target = 0;
    for (const auto& id : order)
    {
        if (!_types.contains(objects.value(id).toMap().value("type").toString()))
        {
            continue;
        }
        const bool existed = _terminals.contains(id);
        QString    error;
        const bool restored = _materialize(id, &error);
        if (!self)
        {
            return;
        }
        if (!restored)
        {
            _window.showMessage(owned(error));
            continue;
        }
        const auto connected = objects.value(id).toMap().value("connected");
        if (!existed && connected.typeId() == QMetaType::Bool && connected.toBool())
        {
            connections.append(id);
        }

        auto&     tabs  = _window.terminalTabs();
        const int index = tabs.indexOf(_terminals.at(id)->panel);
        if (index < 0)
        {
            continue;
        }
        if (index != target)
        {
            tabs.tabBar()->moveTab(index, target);
        }
        ++target;
    }
    for (auto* workspace : _workspaces())
    {
        for (auto* group : workspace->groups())
        {
            int restoredIndex = 0;
            for (const auto& savedId : group->property("restoredTabs").toStringList())
            {
                for (int index = restoredIndex; index < group->tabs().count(); ++index)
                {
                    if (group->tabs().widget(index)->property("terminalId").toString() == savedId)
                    {
                        group->tabs().tabBar()->moveTab(index, restoredIndex++);
                        break;
                    }
                }
            }
            const auto active = group->property("restoredActive").toString();
            for (int index = 0; index < group->tabs().count(); ++index)
            {
                if (group->tabs().widget(index)->property("terminalId").toString() == active)
                {
                    group->tabs().setCurrentIndex(index);
                }
            }
        }
    }
    if (!_session->presentation().value("terminalLayout").toMap().isEmpty())
    {
        _batchSelected.clear();
    }

    // Open only after all panels, processing and layout have been reconstructed.
    // Repeated provider registration must not restart existing terminals.
    const auto session = _session;
    for (const auto& id : connections)
    {
        _setConnectionRequested(id, true);
        if (!self || _session != session)
        {
            return;
        }
    }
}

bool TerminalController::_rememberTabOrder(QString* error)
{
    if (!_session)
    {
        return true;
    }
    QHash<QWidget*, QString> ids;
    for (const auto& [id, terminal] : _terminals)
    {
        ids.insert(terminal->panel, id);
    }
    QStringList   visible;
    QSet<QString> visibleIds;
    for (auto* workspace : _workspaces())
    {
        for (auto* group : workspace->groups())
        {
            const auto& tabs = group->tabs();
            for (int index = 0; index < tabs.count(); ++index)
            {
                const auto id = ids.value(tabs.widget(index));
                if (!id.isEmpty())
                {
                    visible.append(id);
                    visibleIds.insert(id);
                }
            }
        }
    }

    QVariantList  order;
    QSet<QString> listed;
    int           next = 0;
    for (const auto& value : _session->presentation().value("tabOrder").toList())
    {
        const auto id      = value.toString();
        const auto current = visibleIds.contains(id) ? visible.value(next++) : id;
        if (!current.isEmpty() && !listed.contains(current))
        {
            order.append(current);
            listed.insert(current);
        }
    }
    for (const auto& id : visible)
    {
        if (!listed.contains(id))
        {
            order.append(id);
            listed.insert(id);
        }
    }
    // Preserve definitions whose providers are unavailable or views are closed.
    for (const auto& id : _session->objects().values().keys())
    {
        if (!listed.contains(id))
        {
            order.append(id);
        }
    }
    if (_session->presentation().value("tabOrder").toList() == order)
    {
        return true;
    }
    return _session->presentation().setValue("tabOrder", order, error);
}

DetachedTerminalWindow* TerminalController::detachedWindow(const QString& id) const
{
    const auto it = _terminals.find(id);
    return it == _terminals.end() ? nullptr : it->second->window.data();
}

TerminalTabGroup* TerminalController::terminalGroup(const QString& id) const
{
    const auto it = _terminals.find(id);
    return it == _terminals.end() ? nullptr : it->second->group.data();
}

QList<TerminalWorkspace*> TerminalController::_workspaces() const
{
    QList<TerminalWorkspace*> result {&_window.terminalWorkspace()};
    for (const auto& window : _windows)
    {
        if (window)
        {
            result.append(&window->terminalWorkspace());
        }
    }
    return result;
}

TerminalTabGroup* TerminalController::_findGroup(const QString& id) const
{
    if (id.isEmpty())
    {
        return nullptr;
    }
    for (auto* workspace : _workspaces())
    {
        if (auto* group = workspace->findGroup(id))
        {
            return group;
        }
    }
    return nullptr;
}

void TerminalController::_wireGroup(TerminalTabGroup* group)
{
    if (!group || group->property("controllerWired").toBool())
    {
        return;
    }
    group->setProperty("controllerWired", true);
    if (_batchDepth > 0)
    {
        _batchTabs.append({&group->tabs(), group->tabs().blockSignals(true)});
    }
    const QPointer<TerminalTabGroup> guarded(group);
    connect(&group->tabs(), &QTabWidget::currentChanged, this, [this, guarded] {
        if (_movingHost || _batchDepth || !guarded)
        {
            return;
        }
        _focusedGroups[guarded->window()] = guarded;
        auto*      page                   = guarded->tabs().currentWidget();
        const auto id                     = page ? page->property("terminalId").toString() : QString();
        if (!id.isEmpty())
        {
            _changed(id);
        }
        else
        {
            _refreshGroupControls(guarded);
        }
        emit selectionChanged();
    });
    connect(group->tabs().tabBar(), &QTabBar::tabMoved, this, [this] {
        if (!_movingHost && !_batchDepth && _session)
        {
            QString error;
            if (!_rememberTabOrder(&error))
            {
                showMessage(error);
            }
        }
    });
}

void TerminalController::_wireWorkspace(TerminalWorkspace& workspace)
{
    connect(&workspace, &TerminalWorkspace::groupAdded, this, &TerminalController::_wireGroup);
    for (auto* group : workspace.groups())
    {
        _wireGroup(group);
    }
    connect(&workspace, &TerminalWorkspace::activated, this, [this](TerminalTabGroup* group) {
        if (!group || _movingHost || _batchDepth)
        {
            return;
        }
        _focusedGroups[group->window()] = group;
        _focusChanged();
    });
    connect(&workspace, &TerminalWorkspace::closeRequested, this, [this](const QString& id) { _close(id); });
    connect(&workspace, &TerminalWorkspace::contextMenuRequested, this, &TerminalController::_showGroupMenu);
    connect(&workspace,
            &TerminalWorkspace::transferRequested,
            this,
            [this](const QString& id, TerminalTabGroup* target, Qt::Edge edge, int index) {
                QString error;
                if (!moveTerminal(id, target, edge, index, &error))
                {
                    showMessage(error);
                }
            });
    connect(&workspace,
            &TerminalWorkspace::groupTransferRequested,
            this,
            [this](TerminalTabGroup* source, TerminalTabGroup* target, Qt::Edge edge) {
                const QPointer<TerminalController> self(this);
                QString                            error;
                if (!moveGroup(source, target, edge, &error) && self)
                {
                    showMessage(error);
                }
            });
    connect(&workspace,
            &TerminalWorkspace::groupDetachRequested,
            this,
            [this](TerminalTabGroup* group, const QPoint& position) {
                const QPointer<TerminalController> self(this);
                const QPointer<TerminalTabGroup>   guarded(group);
                QString                            error;
                if (!detachGroup(group, &error))
                {
                    if (self)
                    {
                        showMessage(error);
                    }
                    return;
                }
                if (self && guarded)
                {
                    guarded->window()->move(position - QPoint(40, 12));
                }
            });
    connect(&workspace, &TerminalWorkspace::detachRequested, this, [this](const QString& id, const QPoint& position) {
        QString error;
        if (!detachTerminal(id, &error))
        {
            showMessage(error);
            return;
        }
        if (auto* window = detachedWindow(id))
        {
            window->move(position - QPoint(40, 12));
        }
    });
}

DetachedTerminalWindow* TerminalController::_createWindow(const QString& requestedId, int requestedNumber)
{
    QSet<int> usedNumbers;
    for (const auto& existing : _windows)
    {
        if (existing)
        {
            usedNumbers.insert(existing->number());
        }
    }
    int number = requestedNumber;
    if (number < 2 || usedNumbers.contains(number))
    {
        number = 2;
        while (usedNumbers.contains(number))
        {
            ++number;
        }
    }
    auto* window = new DetachedTerminalWindow(_commands, &_window);
    window->setNumber(number);
    window->setContextNames(_workspace() ? _workspace()->name() : QString(), _session ? _session->name() : QString());
    window->setProperty("windowId",
                        requestedId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : requestedId);
    _windows.append(window);
    _wireWorkspace(window->terminalWorkspace());
    connect(window,
            &DetachedTerminalWindow::returnRequested,
            this,
            [this, guarded = QPointer<DetachedTerminalWindow>(window)] {
                if (guarded)
                {
                    QString error;
                    if (!returnWindow(guarded, &error))
                    {
                        showMessage(error);
                    }
                }
            });
    connect(window, &DetachedTerminalWindow::mainWindowRequested, this, [this] {
        _window.showNormal();
        _window.raise();
        _window.activateWindow();
    });
    connect(window, &QObject::destroyed, this, [this, window] {
        _focusedGroups.remove(window);
        _windows.removeAll(nullptr);
    });
    return window;
}

bool TerminalController::moveTerminal(const QString&    id,
                                      TerminalTabGroup* destination,
                                      Qt::Edge          edge,
                                      int               position,
                                      QString*          error)
{
    if (error)
    {
        error->clear();
    }
    auto it = _terminals.find(id);
    if (!_session || _movingHost || it == _terminals.end() || !destination)
    {
        return fail(error, tr("Terminal or destination group is no longer available."));
    }
    TerminalWorkspace* owner = nullptr;
    for (auto* workspace : _workspaces())
    {
        if (workspace->groups().contains(destination))
        {
            owner = workspace;
        }
    }
    if (!owner)
    {
        return fail(error, tr("Destination group belongs to another session."));
    }
    if (edge != Qt::Edge(0))
    {
        int groups = 0;
        for (auto* workspace : _workspaces())
        {
            groups += workspace->groups().size();
        }
        if (groups >= 256)
        {
            return fail(error, tr("At most 256 terminal groups are supported."));
        }
    }
    if (edge != Qt::Edge(0) && edge != Qt::LeftEdge && edge != Qt::RightEdge && edge != Qt::TopEdge
        && edge != Qt::BottomEdge)
    {
        return fail(error, tr("Invalid split direction."));
    }
    const QPointer<TerminalController> self(this);
    const QPointer<QWidget>            panel    = it->second->panel;
    const QPointer<TerminalDisplay>    display  = it->second->display;
    const QPointer<TerminalTabGroup>   previous = it->second->group;
    if (!panel || !display || !previous)
    {
        return fail(error, tr("Terminal page is unavailable."));
    }
    const auto alive = [self, panel, id] {
        if (!self || !panel)
        {
            return false;
        }
        const auto current = self->_terminals.find(id);
        return current != self->_terminals.end() && current->second->panel == panel;
    };
    _movingHost       = true;
    const auto moving = qScopeGuard([self] {
        if (self)
        {
            self->_movingHost = false;
        }
    });
    if (edge == Qt::Edge(0) && previous == destination)
    {
        auto&     tabs     = previous->tabs();
        const int oldIndex = tabs.indexOf(panel);
        if (oldIndex < 0)
        {
            return fail(error, tr("Terminal page is not in its source group."));
        }
        int index = position < 0 ? tabs.count() : std::clamp(position, 0, tabs.count());
        if (index > oldIndex)
        {
            --index;
        }
        tabs.tabBar()->moveTab(oldIndex, index);
        if (!alive() || !previous)
        {
            return false;
        }
        tabs.setCurrentWidget(panel);
        if (!alive() || !previous)
        {
            return false;
        }
        _focusedGroups[previous->window()] = previous;
        _movingHost                        = false;
        _changed(id);
        if (!alive())
        {
            return false;
        }
        emit selectionChanged();
        if (!alive())
        {
            return false;
        }
        return _rememberTabOrder(error);
    }
    QPointer<TerminalTabGroup> target(destination);
    if (edge != Qt::Edge(0))
    {
        target = owner->splitGroup(destination, edge);
    }
    if (!target)
    {
        return fail(error, tr("Could not create destination group."));
    }
    const bool signalsBlocked = display->blockSignals(true);
    const bool updates        = panel->updatesEnabled();
    const auto restore        = qScopeGuard([display, panel, signalsBlocked, updates] {
        if (display)
        {
            display->blockSignals(signalsBlocked);
        }
        if (panel)
        {
            panel->setUpdatesEnabled(updates);
        }
    });
    panel->setUpdatesEnabled(false);
    const int oldIndex = previous->tabs().indexOf(panel);
    previous->tabs().removeTab(oldIndex);
    if (!alive() || !target)
    {
        return false;
    }
    if (target == previous && position > oldIndex)
    {
        --position;
    }
    const int index = target->tabs().insertTab(position < 0 ? target->tabs().count() :
                                                              std::clamp(position, 0, target->tabs().count()),
                                               panel,
                                               QString());
    if (!alive() || !target)
    {
        return false;
    }
    it                 = _terminals.find(id);
    it->second->group  = target;
    it->second->window = qobject_cast<DetachedTerminalWindow*>(target->window());
    _placements[id]    = target->id();
    target->tabs().setCurrentIndex(index);
    _focusedGroups[target->window()] = target;
    target->setProperty("retainedPlacement", false);
    _window.showTerminals(true);
    owner->layout()->activate();
    if (panel->layout())
    {
        panel->layout()->activate();
    }
    if (!alive())
    {
        return false;
    }
    const auto grid = display->terminalSize();
    it->second->device->resize(grid.width(), grid.height());
    if (!alive() || !target)
    {
        return false;
    }
    _changed(id);
    const auto sourcePage = previous ? previous->tabs().currentWidget() : nullptr;
    const auto sourceId   = sourcePage ? sourcePage->property("terminalId").toString() : QString();
    if (!sourceId.isEmpty())
    {
        _changed(sourceId);
    }
    if (!alive() || !target || !display)
    {
        return false;
    }
    _refreshGroupControls(previous);
    if (!alive() || !target || !display)
    {
        return false;
    }
    target->window()->show();
    target->window()->raise();
    target->window()->activateWindow();
    display->setFocus();
    if (!alive())
    {
        return false;
    }
    _cleanupGroups();
    _movingHost = false;
    refreshTerminalActions();
    if (!alive())
    {
        return false;
    }
    emit selectionChanged();
    if (!self || !_session)
    {
        return false;
    }
    return _rememberTabOrder(error);
}

bool TerminalController::moveGroup(TerminalTabGroup* source,
                                   TerminalTabGroup* destination,
                                   Qt::Edge          edge,
                                   QString*          error)
{
    if (error)
    {
        error->clear();
    }
    if (!_session || _movingHost || !source || !destination)
    {
        return fail(error, tr("Terminal group is no longer available for moving."));
    }
    if (edge != Qt::Edge(0) && edge != Qt::LeftEdge && edge != Qt::RightEdge && edge != Qt::TopEdge
        && edge != Qt::BottomEdge)
    {
        return fail(error, tr("Invalid split direction."));
    }
    QPointer<TerminalWorkspace> sourceOwner;
    QPointer<TerminalWorkspace> targetOwner;
    for (auto* workspace : _workspaces())
    {
        const auto groups = workspace->groups();
        if (groups.contains(source))
        {
            sourceOwner = workspace;
        }
        if (groups.contains(destination))
        {
            targetOwner = workspace;
        }
    }
    if (!sourceOwner || !targetOwner)
    {
        return fail(error, tr("Terminal groups must belong to the current session."));
    }
    if (source == destination)
    {
        return true;
    }

    const QPointer<TerminalController> self(this);
    const QPointer<TerminalTabGroup>   group(source);
    const QPointer<TerminalTabGroup>   target(destination);
    const auto                         session = _session;
    const auto                         alive   = [self, group, sourceOwner, targetOwner, session] {
        return self && group && sourceOwner && targetOwner && self->_session == session;
    };
    QList<QPair<QPointer<TerminalDisplay>, bool>> blocked;
    QHash<QString, QPointer<QWidget>>             pages;
    QHash<QString, QPointer<QWidget>>             affectedPages;
    QHash<QString, QPointer<TerminalDisplay>>     affectedDisplays;
    const auto                                    sourceGroups = sourceOwner->groups();
    const auto                                    targetGroups = targetOwner->groups();
    for (const auto& [id, terminal] : _terminals)
    {
        if ((sourceGroups.contains(terminal->group) || targetGroups.contains(terminal->group)) && terminal->panel
            && terminal->display)
        {
            affectedPages.insert(id, terminal->panel);
            affectedDisplays.insert(id, terminal->display);
        }
        if (terminal->group != source)
        {
            continue;
        }
        if (!terminal->panel || !terminal->display || source->tabs().indexOf(terminal->panel) < 0)
        {
            return fail(error, tr("Terminal page is unavailable in its source group."));
        }
        pages.insert(id, terminal->panel);
    }
    _movingHost = true;
    for (auto it = affectedDisplays.cbegin(); it != affectedDisplays.cend(); ++it)
    {
        blocked.append({it.value(), it.value()->blockSignals(true)});
    }
    const auto restore = qScopeGuard([self, blocked] {
        for (const auto& entry : blocked)
        {
            if (entry.first)
            {
                entry.first->blockSignals(entry.second);
            }
        }
        if (self)
        {
            self->_movingHost = false;
        }
    });
    if (!targetOwner->transferGroup(group, *sourceOwner, target, edge, error))
    {
        return false;
    }
    if (!alive())
    {
        return false;
    }

    // Publish every runtime's new host before invoking any backend or action callback.
    const QPointer<QWidget>                host(group->window());
    const QPointer<DetachedTerminalWindow> secondary(qobject_cast<DetachedTerminalWindow*>(host.data()));
    for (auto it = pages.cbegin(); it != pages.cend(); ++it)
    {
        const auto current = _terminals.find(it.key());
        if (current != _terminals.end() && current->second->panel == it.value())
        {
            current->second->window = secondary;
        }
    }
    _focusedGroups[host] = group;
    _cleanupGroups();
    if (!alive() || !host)
    {
        return false;
    }
    _refreshGroupControls(group);
    if (!alive() || !host)
    {
        return false;
    }
    host->show();
    if (!alive() || !host)
    {
        return false;
    }
    // Removing a group and inserting a split can resize siblings in both windows.
    QList<QPointer<TerminalWorkspace>> affectedOwners {sourceOwner};
    if (targetOwner != sourceOwner)
    {
        affectedOwners.append(targetOwner);
    }
    for (const auto& owner : affectedOwners)
    {
        if (!alive() || !host || !owner)
        {
            return false;
        }
        owner->layout()->activate();
        if (!alive() || !host || !owner)
        {
            return false;
        }
        const auto groups = owner->groups();
        for (auto* item : groups)
        {
            const QPointer<TerminalTabGroup> guarded(item);
            if (guarded->layout())
            {
                guarded->layout()->activate();
            }
            if (!alive() || !host || !guarded)
            {
                return false;
            }
        }
    }
    for (auto it = affectedPages.cbegin(); it != affectedPages.cend(); ++it)
    {
        const auto current = _terminals.find(it.key());
        const auto display = affectedDisplays.value(it.key());
        if (current == _terminals.end() || !it.value() || current->second->panel != it.value() || !display
            || current->second->display != display || !current->second->group)
        {
            continue;
        }
        const auto terminalGroup = current->second->group;
        if ((!sourceOwner->groups().contains(terminalGroup) && !targetOwner->groups().contains(terminalGroup))
            || (!pages.contains(it.key()) && !display->isVisible()))
        {
            continue;
        }
        if (it.value()->layout())
        {
            it.value()->layout()->activate();
        }
        if (!alive() || !host || !display || !it.value())
        {
            return false;
        }
        const auto latest = _terminals.find(it.key());
        if (latest == _terminals.end() || latest->second->panel != it.value() || latest->second->display != display
            || latest->second->group != terminalGroup)
        {
            continue;
        }
        const auto grid = display->terminalSize();
        latest->second->device->resize(grid.width(), grid.height());
        if (!alive() || !host)
        {
            return false;
        }
        if (pages.contains(it.key()))
        {
            _changed(it.key());
            if (!alive() || !host)
            {
                return false;
            }
        }
    }
    host->raise();
    host->activateWindow();
    if (!alive() || !host)
    {
        return false;
    }
    if (auto* page = group->tabs().currentWidget())
    {
        const auto current = _terminals.find(page->property("terminalId").toString());
        if (current != _terminals.end() && current->second->group == group && current->second->display)
        {
            current->second->display->setFocus();
        }
    }
    if (!alive())
    {
        return false;
    }
    _movingHost = false;
    refreshTerminalActions();
    if (!alive())
    {
        return false;
    }
    emit selectionChanged();
    if (!alive())
    {
        return false;
    }
    return _rememberTabOrder(error);
}

bool TerminalController::detachGroup(TerminalTabGroup* group, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!_session || _movingHost || !group)
    {
        return fail(error, tr("Terminal group is no longer available for detaching."));
    }
    bool owned = false;
    for (auto* workspace : _workspaces())
    {
        owned = owned || workspace->groups().contains(group);
    }
    if (!owned)
    {
        return fail(error, tr("Terminal group belongs to another session."));
    }
    if (_windows.size() >= 64)
    {
        return fail(error, tr("At most 64 secondary windows are supported."));
    }

    const QPointer<TerminalController>     self(this);
    const QPointer<TerminalTabGroup>       guarded(group);
    const auto                             session = _session;
    const auto                             size    = group->size();
    const QPointer<DetachedTerminalWindow> window  = _createWindow();
    if (self && window && guarded && _session == session)
    {
        window->resize(size);
    }
    if (!self || !guarded || !window || _session != session
        || !moveGroup(guarded, window->terminalWorkspace().initialGroup(), Qt::Edge(0), error))
    {
        // A callback can close or replace the session after transfer; never delete a live host.
        if (self && window && (!guarded || guarded->window() != window))
        {
            _windows.removeAll(window);
            _focusedGroups.remove(window);
            window->allowClose();
            window->deleteLater();
        }
        return false;
    }
    return self && window && guarded && _session == session;
}

bool TerminalController::detachTerminal(const QString& id, QString* error)
{
    if (_windows.size() >= 64)
    {
        return fail(error, tr("At most 64 secondary windows are supported."));
    }
    if (!_session || !_terminals.contains(id) || _movingHost)
    {
        return fail(error, tr("Terminal is no longer available for detaching."));
    }
    const QPointer<TerminalController>     self(this);
    const QPointer<DetachedTerminalWindow> window = _createWindow();
    window->show();
    if (!moveTerminal(id, window->terminalWorkspace().initialGroup(), Qt::Edge(0), -1, error))
    {
        if (window)
        {
            window->allowClose();
            window->deleteLater();
        }
        return false;
    }
    return self && window;
}

bool TerminalController::attachTerminal(const QString& id, QString* error)
{
    return moveTerminal(id, _window.terminalWorkspace().initialGroup(), Qt::Edge(0), -1, error);
}

bool TerminalController::returnWindow(DetachedTerminalWindow* window, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!_session || _movingHost || !window || !_windows.contains(window))
    {
        return fail(error, tr("Secondary window is no longer available."));
    }
    const QPointer<TerminalController>     self(this);
    const QPointer<DetachedTerminalWindow> guarded(window);
    const auto                             session = _session;
    QPointer<TerminalTabGroup>             focused = _focusedGroups.value(window);
    if (!focused)
    {
        focused = window->terminalWorkspace().initialGroup();
    }
    QList<QPair<QPointer<TerminalDisplay>, bool>> blocked;
    QStringList                                   movedIds;
    for (const auto& [id, terminal] : _terminals)
    {
        if (terminal->window == window)
        {
            movedIds.append(id);
            blocked.append({terminal->display, terminal->display->blockSignals(true)});
        }
    }
    _movingHost        = true;
    const auto restore = qScopeGuard([self, blocked] {
        for (const auto& entry : blocked)
        {
            if (entry.first)
            {
                entry.first->blockSignals(entry.second);
            }
        }
        if (self)
        {
            self->_movingHost = false;
        }
    });
    if (!_window.terminalWorkspace().appendWorkspace(window->terminalWorkspace(), error))
    {
        return false;
    }
    if (!self || !guarded || _session != session)
    {
        return false;
    }
    for (const auto& id : movedIds)
    {
        const auto it = _terminals.find(id);
        if (it == _terminals.end())
        {
            continue;
        }
        it->second->window = nullptr;
    }
    if (focused)
    {
        _focusedGroups[&_window] = focused;
    }
    _windows.removeAll(window);
    _focusedGroups.remove(window);
    window->allowClose();
    window->hide();
    window->deleteLater();
    _window.showTerminals(true);
    _window.layout()->activate();
    _window.raise();
    _window.activateWindow();
    for (const auto& id : movedIds)
    {
        const auto it = _terminals.find(id);
        if (it == _terminals.end())
        {
            continue;
        }
        const auto grid = it->second->display->terminalSize();
        it->second->device->resize(grid.width(), grid.height());
        if (!self || _session != session)
        {
            return false;
        }
        _changed(id);
        if (!self || _session != session)
        {
            return false;
        }
    }
    if (focused && focused->tabs().currentWidget())
    {
        const auto id      = focused->tabs().currentWidget()->property("terminalId").toString();
        const auto current = _terminals.find(id);
        if (current != _terminals.end())
        {
            current->second->display->setFocus();
        }
        if (!self || _session != session)
        {
            return false;
        }
    }
    _movingHost = false;
    refreshTerminalActions();
    if (!self || _session != session)
    {
        return false;
    }
    emit selectionChanged();
    if (!self || !_session)
    {
        return false;
    }
    return _rememberTabOrder(error);
}

void TerminalController::_cleanupGroups()
{
    for (auto* workspace : _workspaces())
    {
        for (auto* group : workspace->groups())
        {
            bool retained = false;
            for (auto it = _placements.cbegin(); it != _placements.cend(); ++it)
            {
                if (it.value() == group->id() && !_terminals.contains(it.key()))
                {
                    retained = true;
                }
            }
            group->setProperty("retainedPlacement", retained);
        }
        workspace->removeEmptyGroups();
        const auto groups   = workspace->groups();
        auto*      selected = _focusedGroups.value(workspace->window()).data();
        if (!groups.contains(selected) || !selected->tabs().currentWidget())
        {
            selected = workspace->initialGroup();
            for (auto* group : groups)
            {
                if (group->tabs().count())
                {
                    selected = group;
                    break;
                }
            }
            _focusedGroups[workspace->window()] = selected;
        }
    }
    const auto windows = _windows;
    for (const auto& window : windows)
    {
        if (!window)
        {
            continue;
        }
        bool occupied = false;
        for (auto* group : window->terminalWorkspace().groups())
        {
            occupied = occupied || group->tabs().count() || group->property("retainedPlacement").toBool();
        }
        if (!occupied)
        {
            _windows.removeAll(window);
            _focusedGroups.remove(window);
            window->allowClose();
            window->hide();
            window->deleteLater();
        }
    }
    _window.showTerminals(!_terminals.empty());
}

void TerminalController::_restoreLayout()
{
    if (!_session || _layoutRestored)
    {
        return;
    }
    _layoutRestored  = true;
    const auto saved = _session->presentation().value("terminalLayout").toMap();
    if (saved.isEmpty())
    {
        return;
    }
    QString error;
    if (!_window.terminalWorkspace().restoreLayout(saved.value("main").toMap(), &error))
    {
        showMessage(error);
        return;
    }
    QSet<int> reservedNumbers;
    for (const auto& value : saved.value("windows").toList())
    {
        const auto entry = value.toMap();
        if (entry.contains("number"))
        {
            reservedNumbers.insert(entry.value("number").toInt());
        }
    }
    for (const auto& value : saved.value("windows").toList())
    {
        const auto entry  = value.toMap();
        int        number = entry.value("number").toInt();
        if (!entry.contains("number"))
        {
            number = 2;
            while (reservedNumbers.contains(number))
            {
                ++number;
            }
            reservedNumbers.insert(number);
        }
        auto* window = _createWindow(entry.value("id").toString(), number);
        if (!window->terminalWorkspace().restoreLayout(entry.value("tree").toMap(), &error))
        {
            window->allowClose();
            window->deleteLater();
            showMessage(error);
            continue;
        }
        window->restoreGeometry(QByteArray::fromBase64(entry.value("geometry").toByteArray()));
        window->show();
    }
    for (auto* workspace : _workspaces())
    {
        for (auto* group : workspace->groups())
        {
            _wireGroup(group);
            for (const auto& id : group->property("restoredTabs").toStringList())
            {
                _placements[id] = group->id();
            }
            if (group->id() == saved.value("activeGroup").toString())
            {
                _focusedGroups[group->window()] = group;
            }
        }
    }
}

bool TerminalController::capturePresentation(QString* error)
{
    if (!_session)
    {
        return true;
    }
    // Preserve connection intent through opening, failures and retries, not just an open device.
    // Keep unavailable provider objects untouched.
    QVariantMap connections;
    for (const auto& [id, terminal] : _terminals)
    {
        const auto key       = id + "/connected";
        const bool connected = terminal->connectionRequested;
        const auto saved     = _session->objects().value(key);
        if (saved.typeId() != QMetaType::Bool || saved.toBool() != connected)
        {
            connections.insert(key, connected);
        }
    }
    if (!_session->objects().applyValues(std::move(connections), {}, error))
    {
        return false;
    }

    const auto                  saved = _session->presentation().value("terminalLayout").toMap();
    QHash<QString, QVariantMap> previousGroups;
    const auto                  collect = [&previousGroups](auto&& recurse, const QVariantMap& node) -> void {
        if (node.value("type") == "group")
        {
            previousGroups.insert(node.value("id").toString(), node);
        }
        else
        {
            for (const auto& child : node.value("children").toList())
            {
                recurse(recurse, child.toMap());
            }
        }
    };
    collect(collect, saved.value("main").toMap());
    for (const auto& window : saved.value("windows").toList())
    {
        collect(collect, window.toMap().value("tree").toMap());
    }
    const auto mergeTree = [this, &previousGroups](auto&& recurse, QVariantMap tree) -> QVariantMap {
        if (tree.value("type") == "group")
        {
            const auto   groupId = tree.value("id").toString();
            const auto   live    = tree.value("tabs").toList();
            const auto   old     = previousGroups.value(groupId);
            QVariantList tabs;
            int          next = 0;
            for (const auto& value : old.value("tabs").toList())
            {
                const auto id = value.toString();
                if (_placements.value(id) != groupId)
                {
                    continue;
                }
                if (!_terminals.contains(id))
                {
                    tabs.append(id);
                }
                else if (live.contains(id) && next < live.size())
                {
                    tabs.append(live.at(next++));
                }
            }
            while (next < live.size())
            {
                tabs.append(live.at(next++));
            }
            for (auto it = _placements.cbegin(); it != _placements.cend(); ++it)
            {
                if (it.value() == groupId && !_terminals.contains(it.key()) && !tabs.contains(it.key()))
                {
                    tabs.append(it.key());
                }
            }
            tree.insert("tabs", tabs);
            const auto oldActive = old.value("active").toString();
            if (tree.value("active").toString().isEmpty() && tabs.contains(oldActive))
            {
                tree.insert("active", oldActive);
            }
            if (auto* group = _findGroup(groupId))
            {
                group->setProperty("restoredTabs", tabs);
                group->setProperty("restoredActive", tree.value("active"));
            }
        }
        else
        {
            QVariantList children;
            for (const auto& child : tree.value("children").toList())
            {
                children.append(recurse(recurse, child.toMap()));
            }
            tree.insert("children", children);
        }
        return tree;
    };
    QVariantMap value = saved;
    value.insert("main", mergeTree(mergeTree, _window.terminalWorkspace().saveLayout()));
    QVariantList windows;
    for (const auto& window : _windows)
    {
        if (!window)
        {
            continue;
        }
        QVariantMap entry;
        const auto  id = window->property("windowId").toString();
        for (const auto& old : saved.value("windows").toList())
        {
            if (old.toMap().value("id").toString() == id)
            {
                entry = old.toMap();
            }
        }
        entry.insert("id", id);
        entry.insert("number", window->number());
        entry.insert("geometry", QString::fromLatin1(window->saveGeometry().toBase64()));
        entry.insert("tree", mergeTree(mergeTree, window->terminalWorkspace().saveLayout()));
        windows.append(entry);
    }
    value.insert("windows", windows);
    if (auto* terminal = _current(); terminal && terminal->group)
    {
        value.insert("activeGroup", terminal->group->id());
    }
    else if (!_findGroup(saved.value("activeGroup").toString()))
    {
        value.remove("activeGroup");
    }
    return _session->presentation().setValue("terminalLayout", value, error);
}

QStringList TerminalController::_surfaceCommands(const QString& surface, bool secondary) const
{
    const QString  path    = "ui/terminalGroups/" + QString(secondary ? "secondary/" : "embedded/") + surface;
    const QVariant setting = _workspace() ? _workspace()->settings().value(path) : QVariant();
    QStringList    rules;
    if (setting.isValid())
    {
        for (const auto& value : setting.toList())
        {
            if (value.typeId() == QMetaType::QString && value.toString().size() < 128 && rules.size() < 64)
            {
                rules.append(value.toString());
            }
        }
    }
    else if (surface == "toolbar")
    {
        rules = {"group:connection", "command:Terminal.LocalEcho", "command:View.WrapLines", "group:settings"};
    }
    else
    {
        rules = {"command:Terminal.BufferedInput", "command:Terminal.SendFile", "command:Terminal.Processing"};
    }
    QStringList commandIds;
    for (const auto& [id, action] : _actions)
    {
        commandIds.append(id);
    }
    std::stable_sort(commandIds.begin(), commandIds.end(), [this](const auto& left, const auto& right) {
        const auto leftRole  = _actions.at(left).registration.role;
        const auto rightRole = _actions.at(right).registration.role;
        return leftRole != rightRole ? leftRole < rightRole : left < right;
    });

    QStringList result;
    for (const auto& rule : rules)
    {
        for (const auto& id : commandIds)
        {
            const auto& action = _actions.at(id);
            if (action.registration.role == Action::Role::Create || action.registration.group == "allConnections")
            {
                continue;
            }
            const bool explicitCommand = rule == "command:" + id;
            if (!explicitCommand && rule != "group:" + action.registration.group)
            {
                continue;
            }
            if (!explicitCommand && action.registration.role == Action::Role::Additional)
            {
                continue;
            }
            if (!result.contains(id))
            {
                result.append(id);
            }
        }
    }
    return result;
}

void TerminalController::_refreshHostActions(const QString& id)
{
    const auto it = _terminals.find(id);
    if (it == _terminals.end() || !it->second->group)
    {
        return;
    }
    const QPointer<TerminalTabGroup>       group  = it->second->group;
    const QPointer<DetachedTerminalWindow> window = it->second->window;
    const QPointer<TerminalController>     self(this);
    if (group->tabs().currentWidget() != it->second->panel)
    {
        return;
    }
    _refreshGroupControls(group);
    if (self && window)
    {
        _refreshWindowActions(window);
    }
}

void TerminalController::_refreshGroupControls(TerminalTabGroup* group)
{
    if (!group)
    {
        return;
    }
    auto*      panel    = group->tabs().currentWidget();
    const auto id       = panel ? panel->property("terminalId").toString() : QString();
    const auto previous = _commandTarget;
    _commandTarget      = id;
    const QPointer<TerminalController> self(this);
    const QPointer<TerminalTabGroup>   guarded(group);
    const auto                         restore = qScopeGuard([self, previous] {
        if (self)
        {
            self->_commandTarget = previous;
        }
    });
    const QPointer<QWidget>            expectedPanel(panel);
    QStringList                        commandIds;
    for (const auto& [commandId, item] : _actions)
    {
        commandIds.append(commandId);
    }
    for (const auto& commandId : commandIds)
    {
        const auto registration = _actions.find(commandId);
        if (registration == _actions.end())
        {
            continue;
        }
        const RegisteredAction item = registration->second;
        if (item.registration.role == Action::Role::Create || item.registration.group == "allConnections")
        {
            continue;
        }
        QPointer<QAction> action = guarded->action(commandId);
        if (!action)
        {
            auto* command = _commands.find(commandId);
            if (!command)
            {
                continue;
            }
            action = command->newAction(guarded);
            action->setObjectName(commandId);
            action->setShortcutContext(Qt::WidgetShortcut);
            guarded->setAction(commandId, action);
            connect(action, &QAction::triggered, this, [this, guarded, commandId] {
                if (!guarded)
                {
                    return;
                }
                auto* page = guarded->tabs().currentWidget();
                if (page)
                {
                    _triggerAction(commandId, page->property("terminalId").toString(), guarded->window());
                }
            });
        }
        const QPointer<QAction> source    = item.action;
        const auto              canUpdate = [self, guarded, expectedPanel, id, commandId, action, source] {
            if (!self || !guarded || !action || !source || guarded->tabs().currentWidget() != expectedPanel
                || guarded->action(commandId) != action)
            {
                return false;
            }
            const auto currentAction = self->_actions.find(commandId);
            if (currentAction == self->_actions.end() || currentAction->second.action != source)
            {
                return false;
            }
            const auto current = self->_terminals.find(id);
            return id.isEmpty() ? !expectedPanel :
                                  current != self->_terminals.end() && current->second->panel == expectedPanel
                                      && current->second->group == guarded;
        };
        const auto current = _terminals.find(id);
        const bool matches = current != _terminals.end()
                          && (item.registration.terminalType.isEmpty()
                              || item.registration.terminalType == current->second->type);
        bool       visible = false;
        bool       enabled = false;
        try
        {
            visible = matches && (!item.registration.visible || item.registration.visible());
            if (!canUpdate())
            {
                return;
            }
            enabled = visible && (!item.registration.enabled || item.registration.enabled());
        }
        catch (const std::exception&)
        {
            enabled = false;
        }
        if (!canUpdate())
        {
            return;
        }
        const auto latest    = _terminals.find(id);
        const bool checked   = _actionChecked(commandId, latest == _terminals.end() ? nullptr : latest->second.get());
        const bool checkable = source->isCheckable();
        action->setVisible(visible);
        if (!canUpdate())
        {
            return;
        }
        action->setEnabled(enabled);
        if (!canUpdate())
        {
            return;
        }
        action->setCheckable(checkable);
        if (!canUpdate())
        {
            return;
        }
        action->setChecked(checked);
        if (!canUpdate())
        {
            return;
        }
    }
    const bool secondary = group->window() != &_window;
    const auto toolbar   = _surfaceCommands("toolbar", secondary);
    const auto status    = _surfaceCommands("status", secondary);
    const auto signature = QString(secondary ? "secondary" : "embedded") + toolbar.join('|') + "/" + status.join('|');
    if (group->property("actionLayout").toString() == signature)
    {
        return;
    }
    group->setProperty("actionLayout", signature);
    for (const auto& surface : {QString("toolbar"), QString("status")})
    {
        QToolBar* bar = &group->toolbar();
        if (surface == "status")
        {
            bar = group->findChild<QToolBar*>("terminalStatusControls");
            if (!bar)
            {
                bar = new QToolBar(group);
                bar->setObjectName("terminalStatusControls");
                bar->setIconSize(QSize(16, 16));
                bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
                group->statusBar().addPermanentWidget(bar);
            }
        }
        bar->clear();
        delete bar->findChild<QObject*>("groupActionContainer", Qt::FindDirectChildrenOnly);
        auto* container = new ToolbarActionContainer(bar, bar);
        container->setObjectName("groupActionContainer");
        const auto commands = surface == "toolbar" ? toolbar : status;
        container->addGroup("actions");
        for (const auto& command : commands)
        {
            if (auto* action = group->action(command))
            {
                container->addAction(action, "actions");
                if (surface == "status")
                {
                    const QString name = command == "Terminal.BufferedInput" ? "statusBufferedInput" :
                                         command == "Terminal.SendFile"      ? "statusSendFile" :
                                         command == "Terminal.Processing"    ? "statusProcessing" :
                                                                               QString();
                    if (!name.isEmpty() && bar->widgetForAction(action))
                    {
                        bar->widgetForAction(action)->setObjectName(name);
                    }
                }
            }
        }
    }
}

void TerminalController::_refreshWindowActions(DetachedTerminalWindow* window)
{
    if (!window)
    {
        return;
    }
    const QPointer<TerminalController>     self(this);
    const QPointer<DetachedTerminalWindow> host(window);
    QPointer<TerminalTabGroup>             group = _focusedGroups.value(window);
    if (!group)
    {
        group = window->terminalWorkspace().initialGroup();
    }
    if (!group)
    {
        return;
    }
    _refreshGroupControls(group);
    const auto alive = [self, host, group] { return self && host && group && group->window() == host; };
    if (!alive())
    {
        return;
    }
    QStringList commands;
    for (const auto& [id, item] : _actions)
    {
        commands.append(id);
    }
    for (const auto& id : commands)
    {
        const auto current = _actions.find(id);
        if (current == _actions.end())
        {
            continue;
        }
        const auto item = current->second;
        // Secondary hosts already own their active-group status visibility action.
        if (item.registration.role == Action::Role::Create || id == "View.StatusBar")
        {
            continue;
        }
        QPointer<QAction> action = host->findChild<QAction*>(id, Qt::FindDirectChildrenOnly);
        if (!action)
        {
            auto* command = _commands.find(id);
            if (!command)
            {
                continue;
            }
            action = command->newAction(host);
            action->setObjectName(id);
            action->setShortcutContext(Qt::WindowShortcut);
            host->addAction(action);
            if (item.menu)
            {
                auto& menu = id == "View.WrapLines" ? host->viewMenu() : host->terminalMenu();
                menu.addAction(action,
                               menu.groups().contains(item.registration.group) ? item.registration.group :
                                                                                 QString("management"));
            }
            connect(action, &QAction::triggered, this, [this, host, id] {
                if (!host)
                {
                    return;
                }
                auto* target = _focusedGroups.value(host).data();
                if (!target)
                {
                    target = host->terminalWorkspace().initialGroup();
                }
                auto* page = target ? target->tabs().currentWidget() : nullptr;
                if (page)
                {
                    _triggerAction(id, page->property("terminalId").toString(), host);
                }
            });
        }
        const QPointer<QAction> source    = group->action(id);
        const bool              bulk      = item.registration.group == "allConnections";
        const bool              visible   = bulk || (source && source->isVisible());
        const bool              enabled   = bulk ? (!item.registration.enabled || item.registration.enabled()) :
                                                   source && source->isEnabled();
        const bool              checkable = source && source->isCheckable();
        const bool              checked   = source && source->isChecked();
        action->setVisible(visible);
        if (!alive() || !action)
        {
            return;
        }
        action->setEnabled(enabled);
        if (!alive() || !action)
        {
            return;
        }
        action->setCheckable(checkable);
        if (!alive() || !action)
        {
            return;
        }
        action->setChecked(checked);
        if (!alive() || !action)
        {
            return;
        }
    }
}

void TerminalController::_showGroupMenu(const QString& id, const QPoint& position)
{
    const auto it = _terminals.find(id);
    if (it == _terminals.end() || !it->second->group)
    {
        return;
    }
    const QPointer<TerminalController> self(this);
    const QPointer<TerminalTabGroup>   group = it->second->group;
    const QPointer<QWidget>            panel = it->second->panel;
    group->tabs().setCurrentWidget(panel);
    _focusedGroups[group->window()] = group;
    _refreshGroupControls(group);
    if (!self || !group || !panel)
    {
        return;
    }
    auto* menu = new QMenu(group);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName("terminalTabMenu");
    const auto validTarget = [self, group, panel, id] {
        if (!self || !group || !panel || group->tabs().currentWidget() != panel)
        {
            return false;
        }
        const auto current = self->_terminals.find(id);
        return current != self->_terminals.end() && current->second->panel == panel && current->second->group == group;
    };
    const auto invalidate = [menu, validTarget] {
        if (!validTarget())
        {
            for (auto* action : menu->actions())
            {
                action->setEnabled(false);
            }
            menu->close();
        }
    };
    connect(this, &TerminalController::selectionChanged, menu, invalidate);
    connect(&group->tabs(), &QTabWidget::currentChanged, menu, invalidate);
    for (const auto& [command, item] : _actions)
    {
        if (!item.menu)
        {
            continue;
        }
        const QPointer<QAction> source = group->action(command);
        if (!source || !source->isVisible())
        {
            continue;
        }
        // A context proxy displays the window binding without registering a second shortcut.
        const auto label = source->shortcut().isEmpty() ?
                               source->text() :
                               source->text() + '\t' + source->shortcut().toString(QKeySequence::NativeText);
        auto*      proxy = menu->addAction(source->icon(), label);
        proxy->setObjectName(command);
        proxy->setToolTip(source->toolTip());
        proxy->setEnabled(source->isEnabled());
        proxy->setCheckable(source->isCheckable());
        proxy->setChecked(source->isChecked());
        connect(source, &QAction::changed, proxy, [source, proxy, validTarget] {
            proxy->setEnabled(validTarget() && source && source->isEnabled());
            if (source)
            {
                proxy->setVisible(source->isVisible());
                proxy->setChecked(source->isChecked());
            }
        });
        connect(source, &QObject::destroyed, proxy, [proxy] { proxy->setEnabled(false); });
        connect(proxy, &QAction::triggered, this, [this, command, id, group, source, validTarget] {
            if (validTarget() && source && source->isVisible() && source->isEnabled())
            {
                _triggerAction(command, id, group->window());
            }
        });
    }
    menu->popup(position);
}

void TerminalController::_close(const QString& id, bool preservePlacement)
{
    auto it = _terminals.find(id);
    if (it == _terminals.end())
    {
        return;
    }
    const QPointer<TerminalController> self(this);
    const QPointer<TerminalTabGroup>   group          = it->second->group;
    const bool                         previousMoving = _movingHost;
    _movingHost                                       = true;
    const auto moving                                 = qScopeGuard([self, previousMoving] {
        if (self)
        {
            self->_movingHost = previousMoving;
        }
    });
    if (group)
    {
        group->tabs().removeTab(group->tabs().indexOf(it->second->panel));
    }
    if (!preservePlacement)
    {
        _placements.remove(id);
    }
    auto terminal = std::move(it->second);
    _terminals.erase(it);
    terminal.reset();
    if (!self)
    {
        return;
    }
    if (group)
    {
        group->setProperty("retainedPlacement", preservePlacement);
    }
    if (_batchDepth > 0)
    {
        _batchSelectionChanged = true;
    }
    else
    {
        _cleanupGroups();
        _movingHost  = previousMoving;
        auto* active = group ? group->tabs().currentWidget() : nullptr;
        if (active)
        {
            _changed(active->property("terminalId").toString());
        }
        if (!self)
        {
            return;
        }
        refreshTerminalActions();
        emit selectionChanged();
    }
}
QString TerminalController::_mainParameter(const Runtime& terminal, const QVariantMap& transport) const
{
    if (terminal.type == "socket")
    {
        auto host = transport.value("host").toString();
        if (host.contains(':') && !host.startsWith('['))
        {
            host = '[' + host + ']';
        }
        return host + ':' + transport.value("port").toString();
    }
    if (terminal.type == "serial")
    {
        return transport.value("portName").toString();
    }
    if (terminal.type == "console")
    {
        const auto program = transport.value("program").toString();
        return program.isEmpty() ? tr("Automatic shell") : program;
    }
    return {};
}

void TerminalController::_showTerminalMessage(const QString& id, const QString& message)
{
    const auto it = _terminals.find(id);
    if (it != _terminals.end() && it->second->group)
    {
        it->second->group->statusBar().showMessage(owned(message));
    }
}

void TerminalController::_changed(const QString& id)
{
    auto it = _terminals.find(id);
    if (it == _terminals.end())
    {
        return;
    }
    const auto state    = it->second->device->state();
    auto&      terminal = *it->second;
    const bool open     = state == SerialDevice::State::Open;
    if (open != terminal.processingOpen)
    {
        terminal.processingOpen = open;
        if (open)
        {
            const QPointer<TerminalController> self(this);
            const QPointer<QWidget>            panel = terminal.panel;
            QString                            error;
            const bool                         configured = _configureProcessing(terminal, &error);
            if (!self || !panel)
            {
                return;
            }
            if (!configured)
            {
                terminal.processingConfiguration = terminal.settings.value("processing").toMap();
                terminal.advanced->setProcessingRouter(terminal.processing.get());
                terminal.processing->fail(ProcessingRouter::Direction::Rx, error);
                if (!self || !panel)
                {
                    return;
                }
                terminal.processing->fail(ProcessingRouter::Direction::Tx, error);
                if (!self || !panel)
                {
                    return;
                }
            }
        }
        else
        {
            _stopLineRecorder(terminal);
            // Drain already received output after peer closure; recreate Rx on reconnect.
            terminal.processing->cancel(ProcessingRouter::Direction::Tx);
        }
    }
    terminal.advanced->connectionChanged();
    if (state == SerialDevice::State::Open)
    {
        terminal.lastError.clear();
    }
    if (terminal.connectionRequested && terminal.device->autoReconnect()
        && (state == SerialDevice::State::Closed || state == SerialDevice::State::Failed))
    {
        if (!terminal.reconnect.isActive())
        {
            terminal.reconnect.start();
        }
    }
    else
    {
        terminal.reconnect.stop();
    }

    QString icon;
    QString status;
    switch (state)
    {
        case SerialDevice::State::Closed:
            icon   = "Closed";
            status = tr("Disconnected");
            break;
        case SerialDevice::State::Opening:
            icon   = "Busy";
            status = tr("Connecting");
            break;
        case SerialDevice::State::Open:
            icon   = "Open";
            status = tr("Connected");
            break;
        case SerialDevice::State::Closing:
            icon   = "Busy";
            status = tr("Disconnecting");
            break;
        case SerialDevice::State::Failed:
            icon   = "Failed";
            status = tr("Connection failed");
            break;
    }
    if (terminal.connectionRequested && terminal.device->autoReconnect()
        && (state == SerialDevice::State::Closed || state == SerialDevice::State::Failed))
    {
        icon = "Retry";
    }
    if (!terminal.group)
    {
        return;
    }
    auto&              tabs      = terminal.group->tabs();
    const int          index     = tabs.indexOf(terminal.panel);
    const auto         type      = _types.at(terminal.type).registration.title;
    const auto         effective = _effective(_types.at(terminal.type), terminal.settings);
    const auto         parameter = _mainParameter(terminal, effective.value("transport").toMap());
    const QFontMetrics metrics(tabs.tabBar()->font());
    const auto  detail   = metrics.elidedText(parameter.simplified(), Qt::ElideMiddle, 32 * metrics.averageCharWidth());
    const auto  typeIcon = terminal.type == "serial" ? "cable" :
                           terminal.type == "socket" ? "network" :
                                                       "square-terminal";
    QString     tabTitle = type;
    const auto& registration = _types.at(terminal.type).registration;
    if (registration.tabTitle)
    {
        try
        {
            const auto label = registration.tabTitle(effective).simplified();
            if (!label.isEmpty() && !label.contains(QChar::Null) && label.size() <= 256)
            {
                tabTitle = owned(label);
            }
        }
        catch (const std::exception&)
        {
            // Presentation failures retain the registered terminal type label.
        }
    }
    terminal.group->setTabPresentation(index,
                                       tabTitle,
                                       detail,
                                       Icons::icon(typeIcon),
                                       QIcon(":/Termaster/App/Status/" + icon + ".svg"));
    if (tabs.currentWidget() == terminal.panel)
    {
        terminal.group->statusBar().showMessage(
            tr("%1: %2").arg(terminal.name, status)
            + (terminal.lastError.isEmpty() ? QString() : " — " + terminal.lastError));
        if (terminal.window && _focusedGroups.value(terminal.window) == terminal.group)
        {
            terminal.window->setContextNames(_workspace() ? _workspace()->name() : QString(),
                                             _session ? _session->name() : QString());
        }
    }
    const auto desired = terminal.connectionRequested ? tr("Connected") : tr("Disconnected");
    QString    tooltip = type;
    if (!parameter.isEmpty())
    {
        tooltip += "\n" + parameter;
    }
    tooltip += tr("\nName: %1\nDesired: %2\nActual: %3").arg(terminal.name, desired, status);
    if (!terminal.lastError.isEmpty())
    {
        tooltip += tr("\nLast error: %1").arg(terminal.lastError);
    }
    if (terminal.reconnect.isActive())
    {
        tooltip += tr("\nRetrying automatically.");
    }
    if (!terminal.completion.isEmpty())
    {
        tooltip += "\n" + terminal.completion;
    }
    if (!terminal.title.isEmpty())
    {
        tooltip += "\n" + terminal.title;
    }
    tabs.setTabToolTip(index, Qt::convertFromPlainText(tooltip));
    tabs.tabBar()->setAccessibleTabName(index, tooltip);
    refreshTerminalActions();
}
void TerminalController::connectSelectedTerminal()
{
    if (const auto* terminal = _current(); terminal && _activeWindow())
    {
        _setConnectionRequested(terminal->id, true);
    }
}
void TerminalController::disconnectSelectedTerminal()
{
    if (const auto* terminal = _current(); terminal && _activeWindow())
    {
        _setConnectionRequested(terminal->id, false);
    }
}
void TerminalController::closeSelectedTerminal()
{
    if (auto* terminal = _current(); terminal && _activeWindow())
    {
        const auto id = terminal->id;
        QString    error;
        if (_session && !_session->objects().setValue(id + "/connected", false, &error))
        {
            showMessage(error);
            return;
        }

        _close(id);
    }
}
void TerminalController::clearSelectedTerminal()
{
    if (auto* terminal = _current(); terminal && _activeWindow())
    {
        terminal->display->clear();
    }
}
void TerminalController::showSelectedTerminalSettings()
{
    if (_current())
    {
        _showSettings();
    }
}

std::shared_ptr<SettingsSchema> TerminalController::_appearanceSchema()
{
    if (_appearance)
    {
        return _appearance;
    }
    QFile file(":/Termaster/App/TerminalAppearance.yaml");
    if (!file.open(QIODevice::ReadOnly))
    {
        throw std::runtime_error("Terminal appearance schema is unavailable");
    }
    _appearance = std::make_shared<SettingsSchema>(file.readAll());
    return _appearance;
}
void TerminalController::setProcessorRegistry(ProcessorRegistry& registry)
{
    _processors = &registry;
}

std::optional<TerminalController::PreparedProcessing> TerminalController::_prepareProcessing(const QVariantMap& stored,
                                                                                             QString*           error)
{
    const QPointer<TerminalController> self(this);
    const QPointer<ProcessorRegistry>  processors(_processors);
    if (!TerminalProcessing::validateMode(stored, error))
    {
        return std::nullopt;
    }
    if (TerminalProcessing::mode(stored) == "bypass")
    {
        return PreparedProcessing {};
    }

    const auto value = stored.value("lineflow");
    if (value.metaType().id() != QMetaType::QVariantMap)
    {
        fail(error, "Lineflow processing selection must be a map");
        return std::nullopt;
    }
    const auto selection = value.toMap();
    for (const auto& key : {QStringLiteral("configuration"), QStringLiteral("overrides")})
    {
        if (selection.contains(key) && selection.value(key).metaType().id() != QMetaType::QVariantMap)
        {
            fail(error, "Lineflow processing configuration must be a map: " + key);
            return std::nullopt;
        }
    }
    if (selection.value("type").metaType().id() != QMetaType::QString || selection.value("type").toString().isEmpty())
    {
        fail(error, "Lineflow processing selection requires a string type");
        return std::nullopt;
    }
    if (!self || !processors)
    {
        fail(error, "Processing registry is unavailable");
        return std::nullopt;
    }

    std::unique_ptr<ProcessorRegistry> scope;
    auto*                              registry = processors.data();
    auto                               target   = selection.value("type").toString();
    if (target == "@inplace")
    {
        scope = processors->scoped(TerminalProcessing::diagram(stored), error);
        if (!self || !processors || !scope)
        {
            return std::nullopt;
        }
        registry = scope.get();
        target   = TerminalProcessing::rootId();
    }
    PreparedProcessing result;
    auto               runtimeConfiguration = selection.value("configuration").toMap();
    auto               runtimeOverrides     = selection.value("overrides").toMap();
    const auto         properties = registry->properties(target, runtimeConfiguration, runtimeOverrides, error);
    if (!self || !processors || !properties)
    {
        return std::nullopt;
    }
    auto*         workspace = _workspace ? _workspace() : nullptr;
    const QString directory = workspace ? workspace->filePath().parent().toString() : QString {};
    for (const auto& property : *properties)
    {
        auto configuration = property.configuration;
        if ((property.type == "@initial" || property.type == "@final") && configuration.value("enabled").toBool())
        {
            auto path = configuration.value("file").toString();
            if (!QDir::isAbsolutePath(path))
            {
                if (directory.isEmpty())
                {
                    fail(error, "Relative recording paths require an open workspace");
                    return std::nullopt;
                }
                path = QDir::cleanPath(QDir(directory).absoluteFilePath(path));
                configuration.insert("file", path);
                auto endpoint = runtimeOverrides.value(property.path).toMap();
                endpoint.insert("file", path);
                runtimeOverrides.insert(property.path, endpoint);
            }
        }
        if (property.path != "@final" || !configuration.value("enabled").toBool()
            || configuration.value("recordingMode").toString() != "lines")
        {
            continue;
        }
        const auto resolved = LineflowEndpoint::resolveConfiguration(configuration, {}, 2, error);
        if (!resolved)
        {
            return std::nullopt;
        }
        result.lineRecording = *resolved;
        // Only the selected root's rendered endpoint is replaced by the display collector.
        // Nested endpoints remain untouched and are rejected by generic preparation when enabled in lines mode.
        auto defaults        = runtimeConfiguration.value("overrides").toMap();
        auto endpoint        = defaults.value("@final").toMap();
        endpoint.insert("enabled", false);
        defaults.insert("@final", endpoint);
        runtimeConfiguration.insert("overrides", defaults);
        endpoint = runtimeOverrides.value("@final").toMap();
        endpoint.insert("enabled", false);
        runtimeOverrides.insert("@final", endpoint);
    }
    const auto prepared = registry->prepare(target, runtimeConfiguration, {}, error);
    if (!self || !processors || !prepared)
    {
        return std::nullopt;
    }
    if (prepared->inputCount() != 2 || prepared->outputCount() != 2)
    {
        fail(error, "Terminal Lineflow requires a 2x2 processor");
        return std::nullopt;
    }
    auto instance = ThreadedProcessor::create(prepared, runtimeOverrides, error);
    if (!self || !processors || !instance)
    {
        return std::nullopt;
    }
    result.processor = std::move(instance);
    return result;
}

void TerminalController::setProcessorFavorites(ProcessorFavorites& favorites)
{
    _processorFavorites = &favorites;
}

bool TerminalController::_configureProcessing(Runtime& terminal, QString* error)
{
    const QPointer<TerminalController> self(this);
    const QString                      id              = terminal.id;
    const QPointer<QWidget>            panel           = terminal.panel;
    Session* const                     originalSession = _session;
    const auto                         configuration   = terminal.settings.value("processing").toMap();
    const auto                         liveTerminal    = [self, id, panel, originalSession]() -> Runtime* {
        if (!self || !panel || self->_session != originalSession)
        {
            return nullptr;
        }
        const auto current = self->_terminals.find(id);
        return current != self->_terminals.end() && current->second->panel == panel ? current->second.get() : nullptr;
    };
    auto prepared = _prepareProcessing(configuration, error);
    if (!prepared)
    {
        return false;
    }
    auto* live = liveTerminal();
    if (!live)
    {
        return fail(error, "Terminal changed while preparing processing instances");
    }
    if (live->settings.value("processing").toMap() != configuration)
    {
        return fail(error, "Terminal processing settings changed while preparing processing instances");
    }

    _setLineRecorder(*live, prepared->lineRecording);
    live->processing->replaceDuplex(std::move(prepared->processor));
    live = liveTerminal();
    if (!live || live->settings.value("processing").toMap() != configuration)
    {
        return fail(error, "Terminal changed while replacing processing instances");
    }
    live->processingConfiguration = configuration;
    live->advanced->setProcessingRouter(TerminalProcessing::mode(configuration) == "bypass" ? nullptr :
                                                                                              live->processing.get());
    return true;
}

void TerminalController::_setLineRecorder(Runtime& terminal, const QVariantMap& configuration, bool replace)
{
    if (terminal.lineRecorder && !replace && terminal.lineRecorder->configuration() == configuration)
    {
        return;
    }
    _stopLineRecorder(terminal);
    if (configuration.isEmpty() || !terminal.display)
    {
        return;
    }
    terminal.lineRecorder = std::make_unique<TerminalLineRecorder>(*terminal.display, configuration);
    connect(terminal.lineRecorder.get(),
            &TerminalLineRecorder::error,
            this,
            [this, id = terminal.id](const QString& error) {
                _showTerminalMessage(id, tr("Rendered line recording: %1").arg(error));
            });
}

void TerminalController::_stopLineRecorder(Runtime& terminal)
{
    if (terminal.lineRecorder)
    {
        QString error;
        if (!terminal.lineRecorder->stop(&error))
        {
            _showTerminalMessage(terminal.id, tr("Rendered line recording: %1").arg(error));
        }
        terminal.lineRecorder.reset();
    }
}

bool TerminalController::_applyProcessing(const QString&     id,
                                          const QVariantMap& baseline,
                                          const QVariantMap& configuration,
                                          QString*           error)
{
    const QPointer<TerminalController> self(this);
    const auto                         it = _terminals.find(id);
    if (!_session || it == _terminals.end())
    {
        return fail(error, "Terminal is no longer available");
    }
    auto&                   terminal        = *it->second;
    const QPointer<QWidget> originalPanel   = terminal.panel;
    Session* const          originalSession = _session;
    if (terminal.settings.value("processing").toMap() != baseline)
    {
        return fail(error, "Terminal processing settings changed concurrently; revert the draft");
    }
    if (configuration == baseline)
    {
        return true;
    }
    if (!TerminalProcessing::validateMode(configuration, error))
    {
        return false;
    }
    const bool                        processingChanged = !TerminalProcessing::validateMode(baseline, nullptr)
                                                       || !TerminalProcessing::sameActive(configuration, baseline);
    std::optional<PreparedProcessing> prepared;
    if (processingChanged)
    {
        prepared = _prepareProcessing(configuration, error);
        if (!prepared)
        {
            return false;
        }
    }
    if (!self)
    {
        return false;
    }
    const auto current = _terminals.find(id);
    if (!originalPanel || _session != originalSession || current == _terminals.end()
        || current->second->panel != originalPanel)
    {
        return fail(error, "Terminal changed while preparing processing instances");
    }
    if (current->second->settings.value("processing").toMap() != baseline)
    {
        return fail(error, "Terminal processing settings changed concurrently; revert the draft");
    }
    if (!_session->diskUnchanged(error))
    {
        return false;
    }

    auto objects  = _session->objects().values();
    auto object   = objects.value(id).toMap();
    auto settings = object.value("settings").toMap();
    settings.insert("processing", configuration);
    object.insert("settings", settings);
    objects.insert(id, object);
    const auto bytes = _session->prepareObjects(objects, error);
    if (!bytes)
    {
        return false;
    }
    QSaveFile file(_session->filePath().toString());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(*bytes) != bytes->size() || !file.commit())
    {
        return fail(error, "Cannot save terminal processing settings: " + file.errorString());
    }
    if (!_session->acceptPreparedDocument(*bytes, error))
    {
        return false;
    }
    if (!processingChanged)
    {
        terminal.processingConfiguration = configuration;
        return true;
    }

    const auto liveTerminal = [self, originalPanel, originalSession, id, configuration]() -> Runtime* {
        if (!self || !originalPanel || self->_session != originalSession)
        {
            return nullptr;
        }
        const auto current = self->_terminals.find(id);
        return current != self->_terminals.end() && current->second->panel == originalPanel
                    && current->second->settings.value("processing").toMap() == configuration ?
                   current->second.get() :
                   nullptr;
    };
    terminal.processingUpdate = true;
    terminal.advanced->stop();
    auto* live = liveTerminal();
    if (!live)
    {
        return true;
    }
    live->processingUpdate = false;
    _setLineRecorder(*live, prepared->lineRecording, true);
    live->processing->replaceDuplex(std::move(prepared->processor));
    live = liveTerminal();
    if (!live)
    {
        return true;
    }
    live->processingConfiguration = configuration;
    live->advanced->setProcessingRouter(TerminalProcessing::mode(configuration) == "bypass" ? nullptr :
                                                                                              live->processing.get());
    return true;
}

void TerminalController::addDefaultSettingsPages(SettingsDialog& dialog, const QString& scope, SettingsOverlay settings)
{
    for (const auto& [id, type] : _types)
    {
        dialog.addPage(scope + "." + id,
                       "Terminals",
                       type.registration.title,
                       "Default connection parameters for terminals in this scope.",
                       type.defaultSchema,
                       settings);
        if (scope == "workspace" && type.workspaceSchema)
        {
            dialog.addPage(scope + "." + id + ".suggestions",
                           "Terminals",
                           type.registration.title + " suggestions",
                           "Additional baud rates offered in this workspace.",
                           type.workspaceSchema,
                           settings);
        }
    }
}
void TerminalController::addSettingsPages(SettingsDialog& dialog)
{
    if (auto* terminal = _current())
    {
        dialog.addPage("terminal.display",
                       "Terminal",
                       "Text and screen",
                       "Overrides for the selected terminal.",
                       _appearanceSchema(),
                       terminal->settings);
        ConnectionDraft draft;
        draft.values     = std::make_shared<Settings>(QString {});
        draft.baseline   = _session->objects().value(terminal->id).toMap();
        const auto local = draft.baseline.value("settings").toMap();
        draft.values->setValue(terminal->type, local);
        auto target = SettingsOverlay(_session->settings(), draft.values->node(terminal->type));
        dialog.addPage("terminal.connection",
                       "Terminal",
                       "Connection",
                       "Changing type stops the connection. Connect explicitly after applying the new settings.",
                       _types.at(terminal->type).schema,
                       target);
        dialog.addPageVariant("terminal.connection",
                              terminal->type,
                              _types.at(terminal->type).registration.title,
                              _types.at(terminal->type).schema,
                              target);
        for (const auto& [id, type] : _types)
        {
            if (id != terminal->type)
            {
                dialog.addPageVariant("terminal.connection",
                                      id,
                                      type.registration.title,
                                      type.schema,
                                      SettingsOverlay(_session->settings(), draft.values->node(id)));
            }
        }
        _connectionDrafts.insert(&dialog, std::move(draft));
        connect(&dialog, &QObject::destroyed, this, [this, key = &dialog] { _connectionDrafts.remove(key); });
    }
}

bool TerminalController::saveSettings(SettingsDialog& dialog, QString* error)
{
    const auto draft = _connectionDrafts.find(&dialog);
    const auto id    = dialog.property("terminalId").toString();
    const auto type  = dialog.pageChoice("terminal.connection");
    if (!_session || draft == _connectionDrafts.end() || !_terminals.contains(id) || !_types.contains(type))
    {
        return fail(error, tr("The terminal or selected type is no longer available."));
    }
    auto       definition = _session->objects().value(id).toMap();
    const auto original   = definition;
    const auto oldType    = _terminals.at(id)->type;
    const bool changed    = type != oldType;
    const auto baseline   = draft->baseline;
    if (definition.value("type") != baseline.value("type") || definition.value("module") != baseline.value("module")
        || definition.value("settings").toMap().value("transport")
               != baseline.value("settings").toMap().value("transport"))
    {
        return fail(error, tr("Terminal connection settings changed externally; reopen settings before applying."));
    }
    for (const auto& field : _types.at(oldType).schema->fields())
    {
        if (lookup(definition.value("settings").toMap(), field.settingsPath)
            != lookup(baseline.value("settings").toMap(), field.settingsPath))
        {
            return fail(error,
                        tr("Terminal settings changed externally at %1; reopen settings before applying.")
                            .arg(field.settingsPath));
        }
    }
    const auto local     = draft->values->node(type);
    const auto candidate = SettingsOverlay(_session->settings(), local);
    if (!_validate(_types.at(type), candidate, error))
    {
        return false;
    }
    auto settings = definition.value("settings").toMap();
    if (changed)
    {
        settings.remove("transport");
    }
    for (const auto& field : _types.at(type).schema->fields())
    {
        if (local.contains(field.settingsPath))
        {
            put(settings, field.settingsPath.split('/'), 0, local.value(field.settingsPath));
        }
        else
        {
            removePath(settings, field.settingsPath.split('/'));
        }
    }
    definition.insert("settings", settings);
    definition.insert("type", type);
    definition.insert("module", _types.at(type).registration.plugin);
    if (!capturePresentation(error))
    {
        return false;
    }
    definition.insert("connected", changed ? false : _terminals.at(id)->connectionRequested);
    if (!_session->objects().setValue(id, definition, error))
    {
        return false;
    }
    if (!_session->sync(error))
    {
        _session->objects().setValue(id, original);
        return false;
    }
    if (changed)
    {
        _beginBatch();
        const auto                       finish  = qScopeGuard([this] { _endBatch(); });
        auto&                            old     = *_terminals.at(id);
        const QPointer<TerminalTabGroup> group   = old.group;
        const int                        index   = group ? group->tabs().indexOf(old.panel) : -1;
        const QPointer<TerminalDisplay>  display = old.display;
        const QPointer<CommandEditor>    editor  = old.editor;
        old.processingUpdate                     = true;
        old.connectionRequested                  = false;
        old.reconnect.stop();
        old.device->setCallbacks({});
        old.advanced->stop();
        display->setFocusProxy(nullptr);
        display->disconnect(this);
        editor->disconnect(this);
        display->setParent(nullptr);
        editor->setParent(nullptr);
        _close(id, true);
        if (!_materialize(id, error, display, editor))
        {
            // Restore the prior definition and reattach the retained common widgets.
            auto rollback = original;
            rollback.insert("connected", false);
            _session->objects().setValue(id, rollback);
            QString    rollbackError;
            const bool restored = _materialize(id, &rollbackError, display, editor);
            if (restored && group && index >= 0)
            {
                group->tabs().tabBar()->moveTab(group->tabs().indexOf(_terminals.at(id)->panel), index);
            }
            QString persistenceError;
            if (!_session->sync(&persistenceError))
            {
                rollbackError += "\n" + persistenceError;
            }
            if (!restored)
            {
                delete display.data();
                delete editor.data();
            }
            if (!rollbackError.isEmpty() && error)
            {
                *error += "\n" + rollbackError;
            }
            return false;
        }
        auto& replacement = *_terminals.at(id);
        if (group && index >= 0)
        {
            const int placed = group->tabs().indexOf(replacement.panel);
            group->tabs().tabBar()->moveTab(placed, index);
        }
        _changed(id);
    }
    draft->baseline = definition;
    return true;
}

void TerminalController::applySettings()
{
    for (const auto& [id, terminal] : _terminals)
    {
        const auto effective = _effective(_types.at(terminal->type), terminal->settings);
        terminal->display->setAppearance(effective.value("appearance").toMap());
        terminal->editor->setSettings(effective.value("input").toMap());
        if (terminal->advanced)
        {
            terminal->advanced->configure(effective.value("input").toMap());
        }
        terminal->display->setFocusProxy(terminal->editor->buffered() ? terminal->editor.data() : nullptr);
        const auto target = effective.value("transport").toMap();
        if (target != terminal->targetConfiguration)
        {
            const bool pending = terminal->advanced->active();
            terminal->advanced->stop();
            terminal->targetConfiguration = target;
            if (pending)
            {
                showMessage(tr("Pending output cancelled because the terminal connection settings changed."));
            }
        }
        const auto processing = effective.value("processing").toMap();
        if (terminal->processingConfiguration != processing)
        {
            if (TerminalProcessing::validateMode(processing, nullptr)
                && TerminalProcessing::validateMode(terminal->processingConfiguration, nullptr)
                && TerminalProcessing::sameActive(terminal->processingConfiguration, processing))
            {
                terminal->processingConfiguration = processing;
            }
            else
            {
                terminal->advanced->stop();
            }
        }
        if (terminal->processingPanel)
        {
            terminal->processingPanel->setConfiguration(processing);
        }
        terminal->name = _session->objects().value(id).toMap().value("name", id).toString();
        _changed(id);
    }
}
void TerminalController::showCreateTerminal(const QString& type)
{
    if (!_session || !_types.contains(type))
    {
        return;
    }
    delete _creation.data();
    auto draft = std::make_shared<Settings>(QString {});
    draft->setValue("configuration", QVariantMap {});
    auto  target = SettingsOverlay(_session->settings(), draft->node("configuration"));
    auto* dialog = new SettingsDialog(
        _editors,
        [this, type, draft](QString* error) {
            return createTerminal(type, draft->value("configuration").toMap(), {}, error);
        },
        &_window);
    _creation = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Create %1 terminal").arg(_types.at(type).registration.title));
    dialog->addPage("new.connection",
                    "New terminal",
                    "Connection",
                    "Create a disconnected terminal. Connect explicitly when ready.",
                    _types.at(type).schema,
                    target);
    dialog->addPage("new.display",
                    "New terminal",
                    "Text and screen",
                    "Inherited appearance can be overridden for this terminal.",
                    _appearanceSchema(),
                    target);
    dialog->selectPage("new.connection");
    connect(dialog, &SettingsDialog::applied, dialog, &QDialog::accept);
    dialog->show();
}

bool TerminalController::_canSetAllConnections(bool requested) const
{
    for (const auto& [id, terminal] : _terminals)
    {
        const auto state = terminal->device->state();
        if (requested ? (!terminal->connectionRequested
                         && (state == SerialDevice::State::Closed || state == SerialDevice::State::Failed)) :
                        (terminal->connectionRequested || state == SerialDevice::State::Open
                         || state == SerialDevice::State::Opening))
        {
            return true;
        }
    }
    return false;
}

void TerminalController::_setAllConnections(bool requested)
{
    // Snapshot runtime identity as opening a backend may process nested GUI events.
    QList<std::pair<QString, QPointer<QWidget>>> targets;
    for (const auto& [id, terminal] : _terminals)
    {
        targets.append({id, terminal->panel});
    }
    const QPointer<TerminalController> self(this);
    for (const auto& [id, panel] : targets)
    {
        if (!self)
        {
            return;
        }
        const auto it = _terminals.find(id);
        if (!panel || it == _terminals.end() || it->second->panel != panel)
        {
            continue;
        }
        const auto state = it->second->device->state();
        if (requested
            && (state == SerialDevice::State::Opening || state == SerialDevice::State::Open
                || state == SerialDevice::State::Closing))
        {
            continue;
        }
        _setConnectionRequested(id, requested);
    }
}

void TerminalController::_setConnectionRequested(QString id, bool requested)
{
    qCDebug(terminalTrace) << "Connection request changed" << id;
    const auto it = _terminals.find(id);
    if (it == _terminals.end())
    {
        return;
    }
    auto& terminal = *it->second;
    if (requested)
    {
        if (terminal.connectionRequested)
        {
            return;
        }
        terminal.completion.clear();
        terminal.connectionRequested = true;
        terminal.lastError.clear();
        _attemptConnection(id);
        return;
    }
    terminal.connectionRequested = false;
    terminal.reconnect.stop();
    terminal.lastError.clear();
    terminal.advanced->stop();
    _stopLineRecorder(terminal);
    terminal.device->close();
    _changed(id);
}

void TerminalController::_attemptConnection(const QString& id)
{
    qCDebug(terminalTrace) << "Connection attempt" << id;
    auto it = _terminals.find(id);
    if (it == _terminals.end() || !it->second->connectionRequested)
    {
        return;
    }
    auto& terminal = *it->second;
    terminal.reconnect.stop();
    const auto state = terminal.device->state();
    if (state == SerialDevice::State::Open || state == SerialDevice::State::Opening
        || state == SerialDevice::State::Closing)
    {
        _changed(id);
        return;
    }
    const auto type = _types.find(terminal.type);
    if (type == _types.end())
    {
        return;
    }
    const auto target = _effective(type->second, terminal.settings).value("transport").toMap();
    if (target != terminal.targetConfiguration)
    {
        const bool pending = terminal.advanced->active();
        terminal.advanced->stop();
        terminal.targetConfiguration = target;
        if (pending)
        {
            showMessage(tr("Pending output cancelled because the terminal connection settings changed."));
        }
    }
    terminal.device->resize(terminal.display->terminalSize().width(), terminal.display->terminalSize().height());
    QString error;
    if (!_validate(type->second, terminal.settings, &error) || !terminal.device->open(target, &error))
    {
        if (error.isEmpty())
        {
            error = tr("Connection attempt failed.");
        }
        if (terminal.lastError != error)
        {
            _showTerminalMessage(id, error);
        }
        terminal.lastError = owned(error);
    }
    _changed(id);
}
