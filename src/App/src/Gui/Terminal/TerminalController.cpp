#include "TerminalController.h"

#include "../../Processing/TerminalProcessing.h"
#include "../../Terminal/AdvancedSender.h"
#include "../../Terminal/ProcessingRouter.h"
#include "../Main/MainWindow.h"
#include "../Processing/ProcessorFavorites.h"
#include "../Settings/SettingsDialog.h"
#include "CommandEditor.h"
#include "DetachedTerminalWindow.h"
#include "TerminalProcessingPanel.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHash>
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
#include <Termaster/Core/Gui/Icons.h>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/RandomNames.h>
#include <Termaster/Core/Session.h>
#include <Termaster/Core/SettingsSchema.h>
#include <Termaster/Core/Terminal/CommandInput.h>
#include <Termaster/Core/Terminal/TerminalSendQueue.h>
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
    QString                            id;
    QString                            type;
    QString                            name;
    SettingsOverlay                    settings;
    std::unique_ptr<SerialDevice>      device;
    std::unique_ptr<TerminalSendQueue> sender;
    std::unique_ptr<AdvancedSender>    advanced;
    std::unique_ptr<ProcessingRouter>  processing;
    QVariantMap                        processingConfiguration;
    QPointer<TerminalProcessingPanel>  processingPanel;
    bool                               processingUpdate = false;
    bool                               processingOpen   = false;
    QVariantMap                        targetConfiguration;
    QPointer<CommandEditor>            editor;
    QPointer<TerminalDisplay>          display;
    QPointer<QWidget>                  panel;
    QPointer<DetachedTerminalWindow>   window;
    QMap<QString, QPointer<QAction>>   hostActions;
    bool                               connectionRequested = false;
    QString                            lastError;
    QString                            title;
    QString                            completion;
    QTimer                             reconnect;
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
    connect(&_window.terminalTabs(), &QTabWidget::currentChanged, this, [this] {
        if (_movingHost)
        {
            return;
        }
        refreshTerminalActions();
        emit selectionChanged();
    });
    connect(&_window.terminalTabs(), &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto* widget = _window.terminalTabs().widget(index);
        for (const auto& [id, terminal] : _terminals)
        {
            if (terminal->panel == widget)
            {
                const auto key = id;
                _close(key);
                break;
            }
        }
    });
    auto* bar = _window.terminalTabs().tabBar();
    bar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bar, &QWidget::customContextMenuRequested, this, &TerminalController::_showTabMenu);
    connect(bar, &QTabBar::tabMoved, this, [this] {
        if (_batchDepth == 0 && _session)
        {
            QString error;
            if (!_rememberTabOrder(&error))
            {
                showMessage(error);
            }
        }
    });
    connect(qApp, &QApplication::focusChanged, this, [this] { _focusChanged(); });
    connect(qApp, &QGuiApplication::focusWindowChanged, this, [this] { _focusChanged(); });
    connect(&_window, &MainWindow::terminalDetachRequested, this, [this](const QString& id) {
        QString error;
        if (!detachTerminal(id, &error))
        {
            showMessage(error);
        }
    });
    for (const bool requested : {true, false})
    {
        Action action;
        action.id       = requested ? "Terminal.StartAll" : "Terminal.StopAll";
        action.title    = requested ? tr("Start all") : tr("Stop all");
        action.iconName = requested ? "play" : "square";
        action.group    = "allConnections";
        action.enabled  = [this, requested] { return _canSetAllConnections(requested); };
        action.trigger  = [this, requested] { _setAllConnections(requested); };
        registerAction(std::move(action));
    }

    Action echo;
    echo.id       = "Terminal.LocalEcho";
    echo.title    = tr("Local echo");
    echo.iconName = "keyboard";
    echo.group    = "output";
    echo.role     = Action::Role::Clear;
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

    const QStringList ids {"Terminal.BufferedInput", "Terminal.SendFile", "Terminal.Processing"};
    const QStringList titles {tr("Buffered input"), tr("Send fileвЂ¦"), tr("Processing")};
    const QStringList icons {"panel-bottom", "file-plus", "Processing"};
    for (int index = 0; index < ids.size(); ++index)
    {
        Action action;
        action.id       = ids[index];
        action.title    = titles[index];
        action.iconName = icons[index];
        action.group    = index == 2 ? "settings" : "output";
        action.role     = Action::Role::Additional;
        action.enabled  = [this] { return _current() && _current()->editor; };
        action.trigger  = [this, index] {
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
        auto* button = new QToolButton(&_window);
        button->setObjectName(index == 0 ? "statusBufferedInput" : index == 1 ? "statusSendFile" : "statusProcessing");
        button->setAutoRaise(true);
        button->setIconSize(QSize(16, 16));
        button->setDefaultAction(presentation);
        _window.statusBar()->addPermanentWidget(button);
        _statusButtons[index] = button;
    }
    Action detach;
    detach.id       = "Terminal.Detach";
    detach.title    = tr("Move to new window");
    detach.iconName = "app-window";
    detach.group    = "management";
    detach.role     = Action::Role::Additional;
    detach.visible  = [this] {
        const auto* current = _current();
        return current && !current->window;
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
    attach.title    = tr("Return to main window");
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
        if (!current || !current->window)
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
    _session = session;
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
    if (auto* host = qobject_cast<QMainWindow*>(selectedWindow()))
    {
        host->statusBar()->showMessage(owned(message));
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
    const auto* selected = _window.terminalTabs().currentWidget();
    for (const auto& [id, terminal] : _terminals)
    {
        if (selected && terminal->panel == selected)
        {
            return id;
        }
    }
    return {};
}

TerminalController::Runtime* TerminalController::_current() const
{
    if (_commandTarget)
    {
        const auto it = _terminals.find(*_commandTarget);
        return it == _terminals.end() ? nullptr : it->second.get();
    }
    for (auto* active = QApplication::activeWindow(); active; active = active->parentWidget())
    {
        for (const auto& [id, terminal] : _terminals)
        {
            if (terminal->window && terminal->window == active)
            {
                return terminal.get();
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
    if (_session && !_session->sync(error))
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
    if (!menu || !menu->groups().contains(action.group))
    {
        throw std::invalid_argument("Unknown action group");
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
        menu->addAction(presentation, action.group);
        if (action.role != Action::Role::Close && action.role != Action::Role::Additional)
        {
            const auto group = action.role == Action::Role::Create ? "terminalCreation" : "terminal";
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
            menu->removeAction(presentation);
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
    for (auto& [terminalId, terminal] : _terminals)
    {
        auto* presentation = terminal->hostActions.take(id).data();
        if (presentation && terminal->window)
        {
            terminal->window->terminalMenu().removeAction(presentation);
            terminal->window->viewMenu().removeAction(presentation);
            terminal->window->terminalToolbar().removeAction(presentation);
            delete presentation;
        }
    }
    auto* action = it->second.action.data();
    if (action)
    {
        it->second.menu->removeAction(action);
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
        if (terminal->window)
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
    const auto  id    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto  label = name.isEmpty() ? RandomNames::generate().toString() : name;
    QVariantMap definition {
        {    "type",                                type},
        {  "module", _types.at(type).registration.plugin},
        {    "name",                               label},
        {"settings",                           overrides}
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
bool TerminalController::_materialize(const QString& id, QString* error)
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
        terminal->device = type.registration.createDevice();
        if (!terminal->device)
        {
            return fail(error, "Terminal device factory returned null");
        }
        terminal->display = new TerminalDisplay;
        terminal->display->setObjectName("terminalDisplay");
        const auto effective = _effective(type, terminal->settings);
        terminal->display->setAppearance(effective.value("appearance").toMap());
        terminal->panel = type.registration.createPanel ?
                              type.registration.createPanel(terminal->display, effective, nullptr) :
                              terminal->display.data();
        if (!terminal->panel)
        {
            delete terminal->display.data();
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
        terminal->editor = new CommandEditor(terminalArea);
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
            split->addWidget(terminal->processingPanel->propertiesWidget());
            split->setStretchFactor(0, 1);
            split->setStretchFactor(1, 0);
            terminalLayout->addWidget(terminal->processingPanel);
            terminal->processingPanel->hide();
        }
    }
    catch (const std::exception& exception)
    {
        delete terminal->display.data();
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
            if (TerminalProcessing::active(runtime.processingConfiguration).value("rx").toMap().isEmpty()
                && !TerminalProcessing::active(runtime.processingConfiguration).contains("duplex"))
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
        [this, id](ProcessingRouter::Direction direction, const QString& error) {
            const auto it = _terminals.find(id);
            if (it == _terminals.end())
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
            const bool configured = _configureProcessing(*it->second,
                                                         &error,
                                                         it->second->processingConfiguration
                                                             == it->second->settings.value("processing").toMap());
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
                const bool accepted = TerminalProcessing::active(it->second->processingConfiguration)
                                                  .value("tx")
                                                  .toMap()
                                                  .isEmpty()
                                           && !TerminalProcessing::active(it->second->processingConfiguration)
                                                   .contains("duplex") ?
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
    const int index = _window.terminalTabs().addTab(panel, _terminals.at(id)->name);
    if (_batchDepth > 0)
    {
        _batchSelected         = panel;
        _batchSelectionChanged = true;
    }
    else
    {
        _window.terminalTabs().setCurrentIndex(index);
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
    auto& tabs           = _window.terminalTabs();
    _batchSignalsBlocked = tabs.blockSignals(true);
    _batchUpdatesEnabled = tabs.updatesEnabled();
    tabs.setUpdatesEnabled(false);
    _batchSelectionChanged = false;
    _batchSelected.clear();
}

void TerminalController::_endBatch()
{
    if (_batchDepth > 1)
    {
        --_batchDepth;
        return;
    }
    const QPointer<TerminalController> self(this);
    const QPointer<QTabWidget>         tabs(&_window.terminalTabs());
    const bool                         signalsBlocked  = _batchSignalsBlocked;
    const bool                         updatesEnabled  = _batchUpdatesEnabled;
    const auto                         restoreTabState = [tabs, signalsBlocked, updatesEnabled] {
        if (tabs)
        {
            tabs->blockSignals(signalsBlocked);
            tabs->setUpdatesEnabled(updatesEnabled);
        }
    };
    auto restoreTabs = qScopeGuard(restoreTabState);
    if (_batchSelected && tabs->indexOf(_batchSelected) >= 0)
    {
        tabs->setCurrentWidget(_batchSelected);
        if (!self)
        {
            return;
        }
    }
    if (_batchSelectionChanged)
    {
        _window.showTerminals(_window.terminalTabs().count() > 0);
        if (!self)
        {
            return;
        }
    }
    if (_batchSelected)
    {
        if (auto* terminal = _current())
        {
            terminal->display->setFocus();
            if (!self)
            {
                return;
            }
        }
    }
    restoreTabState();
    restoreTabs.dismiss();
    if (!self)
    {
        return;
    }
    const bool changed = _batchSelectionChanged;
    _batchSelected.clear();
    _batchDepth = 0;
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

    int target = 0;
    for (const auto& id : order)
    {
        if (!_types.contains(objects.value(id).toMap().value("type").toString()))
        {
            continue;
        }
        QString    error;
        const bool restored = _materialize(id, &error);
        if (!self)
        {
            return;
        }
        if (!restored)
        {
            _window.statusBar()->showMessage(owned(error));
            continue;
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
    _restoreDetached();
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
    const auto&   tabs = _window.terminalTabs();
    for (int index = 0; index < tabs.count(); ++index)
    {
        const auto id = ids.value(tabs.widget(index));
        if (!id.isEmpty())
        {
            visible.append(id);
            visibleIds.insert(id);
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
    return _session->presentation().setValue("tabOrder", order, error);
}

QMainWindow* TerminalController::detachedWindow(const QString& id) const
{
    const auto it = _terminals.find(id);
    return it == _terminals.end() ? nullptr : it->second->window.data();
}

bool TerminalController::detachTerminal(const QString& id, QString* error)
{
    if (error)
    {
        error->clear();
    }
    auto it = _terminals.find(id);
    if (!_session || it == _terminals.end() || !it->second->panel || _movingHost)
    {
        return fail(error, tr("Terminal is no longer available for detaching."));
    }
    if (it->second->window)
    {
        return true;
    }
    if (!_rememberTabOrder(error))
    {
        return false;
    }

    const QPointer<TerminalController> owner(this);
    const QPointer<QWidget>            originalPanel = it->second->panel;
    const auto                         alive         = [owner, originalPanel, id] {
        if (!owner || !originalPanel)
        {
            return false;
        }
        const auto current = owner->_terminals.find(id);
        return current != owner->_terminals.end() && current->second->panel == originalPanel;
    };
    auto&      terminal = *it->second;
    const auto saved    = _session->presentation().node("detachedWindows").value(id).toMap();
    {
        QPointer<TerminalController> self(this);
        _movingHost                                   = true;
        const auto                             moving = qScopeGuard([self] {
            if (self)
            {
                self->_movingHost = false;
            }
        });
        const QPointer<DetachedTerminalWindow> host   = new DetachedTerminalWindow(_commands, &_window);
        host->setProperty("terminalId", id);
        terminal.window = host;
        host->installEventFilter(this);
        host->terminalToolbar().installEventFilter(this);
        host->statusBar()->installEventFilter(this);
        connect(host, &DetachedTerminalWindow::returnRequested, this, [this, id] {
            QString error;
            if (!attachTerminal(id, &error))
            {
                showMessage(error);
            }
        });
        if (!saved.isEmpty())
        {
            host->restoreGeometry(QByteArray::fromBase64(saved.value("geometry").toString().toLatin1()));
        }
        host->terminalToolbar().setVisible(saved.value("toolbarVisible", true).toBool());
        host->statusBar()->setVisible(saved.value("statusBarVisible", true).toBool());
        const QPointer<QWidget> panel   = terminal.panel;
        const bool              updates = panel->updatesEnabled();
        panel->setUpdatesEnabled(false);
        QPointer<TerminalDisplay> display  = terminal.display;
        const bool                blocked  = display->blockSignals(true);
        const auto                geometry = qScopeGuard([display, blocked] {
            if (display)
            {
                display->blockSignals(blocked);
            }
        });
        auto&                     tabs     = _window.terminalTabs();
        const int                 index    = tabs.indexOf(panel);
        if (index >= 0)
        {
            tabs.removeTab(index);
        }
        if (!alive() || !host)
        {
            return false;
        }
        host->setCentralWidget(panel);
        // QTabWidget hides a removed page; the new host must show it explicitly.
        panel->show();
        host->show();
        if (!alive() || !host)
        {
            return false;
        }
        host->layout()->activate();
        if (!alive())
        {
            return false;
        }
        panel->layout()->activate();
        if (!alive())
        {
            return false;
        }
        panel->setUpdatesEnabled(updates);
        _window.showTerminals(tabs.count() > 0);
        _changed(id);
        if (!_captureDetached(id, error))
        {
            // The cached settings owner normally cannot fail here. Keep a visible
            // live host if it does, rather than discarding the terminal panel.
            return false;
        }
    }
    QPointer<TerminalController> self(this);
    const auto                   runtime = _terminals.find(id);
    if (!alive() || runtime == _terminals.end())
    {
        return false;
    }
    const auto size = runtime->second->display->terminalSize();
    runtime->second->device->resize(size.width(), size.height());
    if (!self || !originalPanel)
    {
        return false;
    }
    _refreshHostActions(id);
    if (!self)
    {
        return false;
    }
    const auto current = _terminals.find(id);
    if (current != _terminals.end() && current->second->window)
    {
        const QPointer<DetachedTerminalWindow> host    = current->second->window;
        const QPointer<TerminalDisplay>        display = current->second->display;
        host->raise();
        if (!self || !host || !display)
        {
            return false;
        }
        host->activateWindow();
        if (!self || !display)
        {
            return false;
        }
        display->setFocus();
    }
    if (!self)
    {
        return false;
    }
    if (_batchDepth == 0)
    {
        refreshTerminalActions();
        if (self)
        {
            emit selectionChanged();
        }
    }
    return true;
}

bool TerminalController::attachTerminal(const QString& id, QString* error)
{
    if (error)
    {
        error->clear();
    }
    auto it = _terminals.find(id);
    if (!_session || it == _terminals.end() || !it->second->panel || _movingHost)
    {
        return fail(error, tr("Terminal is no longer available for returning."));
    }
    const QPointer<TerminalController> owner(this);
    const QPointer<QWidget>            originalPanel   = it->second->panel;
    const QPointer<TerminalDisplay>    originalDisplay = it->second->display;
    const auto                         alive           = [owner, originalPanel, id] {
        if (!owner || !originalPanel)
        {
            return false;
        }
        const auto current = owner->_terminals.find(id);
        return current != owner->_terminals.end() && current->second->panel == originalPanel;
    };
    auto&                            terminal = *it->second;
    QPointer<DetachedTerminalWindow> host     = terminal.window;
    if (!host)
    {
        return true;
    }
    auto state = _session->presentation().node("detachedWindows");
    if (state.contains(id) && !state.remove(id, error))
    {
        return false;
    }
    {
        QPointer<TerminalController> self(this);
        _movingHost        = true;
        const auto moving  = qScopeGuard([self] {
            if (self)
            {
                self->_movingHost = false;
            }
        });
        auto*      panel   = host->takeCentralWidget();
        const bool updates = panel->updatesEnabled();
        panel->setUpdatesEnabled(false);
        QPointer<TerminalDisplay> display  = terminal.display;
        const bool                blocked  = display->blockSignals(true);
        const auto                geometry = qScopeGuard([display, blocked] {
            if (display)
            {
                display->blockSignals(blocked);
            }
        });
        terminal.window                    = nullptr;
        terminal.hostActions.clear();
        host->allowClose();
        host->hide();
        host->deleteLater();
        if (!alive())
        {
            return false;
        }
        int position = 0;
        for (const auto& value : _session->presentation().value("tabOrder").toList())
        {
            const auto otherId = value.toString();
            if (otherId == id)
            {
                break;
            }
            const auto other = _terminals.find(otherId);
            if (other != _terminals.end() && !other->second->window)
            {
                ++position;
            }
        }
        auto&     tabs  = _window.terminalTabs();
        const int index = tabs.insertTab(std::min(position, tabs.count()), panel, QString());
        if (!alive())
        {
            return false;
        }
        _changed(id);
        tabs.setCurrentIndex(index);
        _window.showTerminals(true);
        _window.layout()->activate();
        if (!alive())
        {
            return false;
        }
        panel->layout()->activate();
        if (!alive())
        {
            return false;
        }
        panel->setUpdatesEnabled(updates);
    }
    QPointer<TerminalController>    self(this);
    const QPointer<TerminalDisplay> display = originalDisplay;
    const auto                      size    = display->terminalSize();
    _terminals.at(id)->device->resize(size.width(), size.height());
    if (!self || !display)
    {
        return false;
    }
    if (_window.isMinimized())
    {
        _window.showNormal();
    }
    _window.show();
    _window.raise();
    _window.activateWindow();
    if (!self || !display)
    {
        return false;
    }
    display->setFocus();
    if (!self)
    {
        return false;
    }
    refreshTerminalActions();
    if (self)
    {
        emit selectionChanged();
    }
    return true;
}

bool TerminalController::_captureDetached(const QString& id, QString* error)
{
    const auto it = _terminals.find(id);
    if (!_session || it == _terminals.end() || !it->second->window)
    {
        return true;
    }
    auto* host  = it->second->window.data();
    auto  state = _session->presentation().node("detachedWindows");
    auto  value = state.value(id).toMap();
    value.insert("geometry", QString::fromLatin1(host->saveGeometry().toBase64()));
    value.insert("toolbarVisible", !host->terminalToolbar().isHidden());
    value.insert("statusBarVisible", !host->statusBar()->isHidden());
    return state.setValue(id, value, error);
}

bool TerminalController::capturePresentation(QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!_session)
    {
        return true;
    }
    if (!_rememberTabOrder(error))
    {
        return false;
    }
    for (const auto& [id, terminal] : _terminals)
    {
        if (terminal->window && !_captureDetached(id, error))
        {
            return false;
        }
    }
    return true;
}

bool TerminalController::eventFilter(QObject* watched, QEvent* event)
{
    if (!_movingHost && _session
        && (event->type() == QEvent::Move || event->type() == QEvent::Resize
            || event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show
            || event->type() == QEvent::Hide))
    {
        auto* widget = qobject_cast<QWidget*>(watched);
        auto* host   = widget ? qobject_cast<DetachedTerminalWindow*>(widget->window()) : nullptr;
        if (host)
        {
            const auto id = host->property("terminalId").toString();
            const auto it = _terminals.find(id);
            if (it != _terminals.end() && it->second->window == host)
            {
                QString error;
                if (!_captureDetached(id, &error))
                {
                    showMessage(error);
                }
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void TerminalController::_restoreDetached()
{
    if (!_session)
    {
        return;
    }
    const auto saved = _session->presentation().node("detachedWindows").values();
    for (auto entry = saved.cbegin(); entry != saved.cend(); ++entry)
    {
        const auto it = _terminals.find(entry.key());
        if (it == _terminals.end() || it->second->window)
        {
            continue;
        }
        QString error;
        if (!detachTerminal(entry.key(), &error))
        {
            showMessage(error);
        }
    }
}

void TerminalController::_refreshHostActions(const QString& id)
{
    auto it = _terminals.find(id);
    if (it == _terminals.end() || !it->second->window)
    {
        return;
    }
    auto&                              terminal = *it->second;
    QPointer<DetachedTerminalWindow>   host     = terminal.window;
    const QPointer<TerminalController> self(this);
    const auto                         previousTarget = _commandTarget;
    _commandTarget                                    = id;
    const auto restoreTarget                          = qScopeGuard([self, previousTarget] {
        if (self)
        {
            self->_commandTarget = previousTarget;
        }
    });
    const auto alive                                  = [self, host, id] {
        if (!self || !host)
        {
            return false;
        }
        const auto it = self->_terminals.find(id);
        return it != self->_terminals.end() && it->second->window == host;
    };
    QStringList commands;
    for (const auto& [commandId, item] : _actions)
    {
        commands.append(commandId);
    }
    std::sort(commands.begin(), commands.end(), [this](const QString& left, const QString& right) {
        const auto a = _actions.at(left).registration.role;
        const auto b = _actions.at(right).registration.role;
        return a == b ? left < right : a < b;
    });
    for (const auto& commandId : commands)
    {
        const auto itemIt = _actions.find(commandId);
        if (itemIt == _actions.end())
        {
            continue;
        }
        const auto item = itemIt->second;

        if (item.registration.role == Action::Role::Create || item.registration.group == "allConnections")
        {
            continue;
        }
        if (!item.registration.terminalType.isEmpty() && item.registration.terminalType != terminal.type)
        {
            continue;
        }
        auto* action = terminal.hostActions.value(commandId).data();
        if (!action)
        {
            auto* command = _commands.find(commandId);
            if (!command)
            {
                continue;
            }
            action = command->newAction(host);
            action->setObjectName(commandId);
            action->setShortcutContext(Qt::WindowShortcut);
            host->addAction(action);
            terminal.hostActions.insert(commandId, action);
            if (commandId == "View.WrapLines")
            {
                host->viewMenu().addAction(action, "terminal");
            }
            else
            {
                const auto group = host->terminalMenu().groups().contains(item.registration.group) ?
                                       item.registration.group :
                                       QString("management");
                host->terminalMenu().addAction(action, group);
            }
            const auto role = item.registration.role;
            if ((role != Action::Role::Close && role != Action::Role::Additional) || commandId == "Terminal.Attach")
            {
                host->terminalToolbar().addAction(action);
            }
            connect(action,
                    &QAction::triggered,
                    this,
                    [this, commandId, id, window = QPointer<DetachedTerminalWindow>(host.data())] {
                        if (window)
                        {
                            _triggerAction(commandId, id, window);
                        }
                    });
        }
        const QPointer<QAction> guarded(action);
        try
        {
            const bool visible = !item.registration.visible || item.registration.visible();
            if (!alive())
            {
                return;
            }
            const bool enabled = _activeWindow() && (!item.registration.enabled || item.registration.enabled());
            if (!alive())
            {
                return;
            }
            if (!guarded)
            {
                continue;
            }
            guarded->setCheckable(item.action && item.action->isCheckable());
            if (!alive())
            {
                return;
            }
            if (!guarded)
            {
                continue;
            }
            guarded->setChecked(_actionChecked(commandId, _current()));
            if (!alive())
            {
                return;
            }
            if (!guarded)
            {
                continue;
            }
            guarded->setVisible(visible);
            if (!alive())
            {
                return;
            }
            if (guarded)
            {
                guarded->setEnabled(enabled);
            }
            if (!alive())
            {
                return;
            }
        }
        catch (const std::exception&)
        {
            if (!alive())
            {
                return;
            }
            if (guarded)
            {
                guarded->setEnabled(false);
            }
        }
    }
    const QStringList ids {"Terminal.BufferedInput", "Terminal.SendFile", "Terminal.Processing"};
    const QStringList names {"statusBufferedInput", "statusSendFile", "statusProcessing"};
    for (int index = 0; index < ids.size(); ++index)
    {
        auto* action = terminal.hostActions.value(ids[index]).data();
        if (!action)
        {
            continue;
        }
        auto* button = host->findChild<QToolButton*>(names[index]);
        if (button)
        {
            button->setDefaultAction(action);
        }
        else
        {
            host->addStatusAction(action, names[index]);
        }
    }
}

void TerminalController::_close(const QString& id, bool preservePlacement)
{
    qCDebug(terminalTrace) << "Closing terminal" << id;
    auto it = _terminals.find(id);
    if (it == _terminals.end())
    {
        return;
    }
    auto* host = it->second->window.data();
    if (host)
    {
        if (preservePlacement && _session)
        {
            _captureDetached(id, nullptr);
        }
        const QScopedValueRollback moving(_movingHost, true);
        host->allowClose();
        host->hide();
        host->takeCentralWidget();
    }
    if (!preservePlacement && _session)
    {
        _session->presentation().node("detachedWindows").remove(id);
    }
    auto terminal = std::move(it->second);
    _terminals.erase(it);
    if (terminal->panel)
    {
        _window.terminalTabs().removeTab(_window.terminalTabs().indexOf(terminal->panel));
    }
    terminal.reset();
    if (host)
    {
        host->deleteLater();
    }
    if (_batchDepth > 0)
    {
        _batchSelectionChanged = true;
        return;
    }
    _window.showTerminals(_window.terminalTabs().count() > 0);
    refreshTerminalActions();
    emit selectionChanged();
}
QString TerminalController::_mainParameter(const Runtime& terminal) const
{
    const auto transport = _effective(_types.at(terminal.type), terminal.settings).value("transport").toMap();
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
    auto* host = it != _terminals.end() && it->second->window ? static_cast<QMainWindow*>(it->second->window.data()) :
                                                                &_window;
    host->statusBar()->showMessage(owned(message));
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
    const auto         index     = _window.terminalTabs().indexOf(it->second->panel);
    const auto         type      = terminal.type == "socket"  ? tr("Socket") :
                                   terminal.type == "serial"  ? tr("Serial") :
                                   terminal.type == "console" ? tr("Console") :
                                                                _types.at(terminal.type).registration.title;
    const auto         parameter = _mainParameter(terminal);
    const QFontMetrics metrics(_window.terminalTabs().tabBar()->font());
    const auto detail   = metrics.elidedText(parameter.simplified(), Qt::ElideMiddle, 32 * metrics.averageCharWidth());
    const auto typeIcon = terminal.type == "serial" ? "cable" :
                          terminal.type == "socket" ? "network" :
                                                      "square-terminal";
    if (terminal.window)
    {
        terminal.window->setWindowTitle(tr("%1 — %2 — %3 — Termaster")
                                            .arg(terminal.name,
                                                 parameter.isEmpty() ? type : type + " · " + parameter,
                                                 _session ? _session->name() : QString()));
        terminal.window->setWindowIcon(Icons::icon(typeIcon));
        terminal.window->statusBar()->showMessage(
            tr("%1: %2").arg(type, status) + (terminal.lastError.isEmpty() ? QString() : " — " + terminal.lastError));
    }
    else
    {
        _window.setTerminalTab(index,
                               type,
                               detail,
                               Icons::icon(typeIcon),
                               QIcon(":/Termaster/App/Status/" + icon + ".svg"));
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
    _window.terminalTabs().setTabToolTip(index, Qt::convertFromPlainText(tooltip));
    _window.terminalTabs().tabBar()->setAccessibleTabName(index, tooltip);
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

std::optional<std::array<std::shared_ptr<Processor>, 2>> TerminalController::_prepareProcessing(
    const QVariantMap& stored,
    QString*           error,
    bool               txOnly)
{
    const QPointer<TerminalController> self(this);
    const QPointer<ProcessorRegistry>  processors(_processors);
    if (!TerminalProcessing::validateMode(stored, error))
    {
        return std::nullopt;
    }
    const auto                                configuration = TerminalProcessing::active(stored);
    std::array<std::shared_ptr<Processor>, 2> instances;
    const bool                                shared = configuration.contains("duplex");
    if (shared && (!configuration.value("rx").toMap().isEmpty() || !configuration.value("tx").toMap().isEmpty()))
    {
        fail(error, "Shared processing cannot be combined with independent Rx/Tx selections");
        return std::nullopt;
    }
    const QStringList directions = shared ? QStringList {"duplex"} : QStringList {"rx", "tx"};
    for (int index = txOnly && !shared ? 1 : 0; index < directions.size(); ++index)
    {
        const auto value = configuration.value(directions[index]);
        if (value.isValid() && value.metaType().id() != QMetaType::QVariantMap)
        {
            fail(error, "Processing selection must be a map: " + directions[index]);
            return std::nullopt;
        }
        const auto selection = value.toMap();
        for (const auto& key : {QStringLiteral("configuration"), QStringLiteral("overrides")})
        {
            if (selection.contains(key) && selection.value(key).metaType().id() != QMetaType::QVariantMap)
            {
                fail(error, "Processing configuration must be a map: " + directions[index] + "/" + key);
                return std::nullopt;
            }
        }
        if (selection.contains("type") && selection.value("type").metaType().id() != QMetaType::QString)
        {
            fail(error, "Processing identifier must be a string: " + directions[index]);
            return std::nullopt;
        }
        const auto type = selection.value("type").toString();
        if (type.isEmpty())
        {
            if (!selection.isEmpty())
            {
                fail(error, "Processing selection requires a type: " + directions[index]);
                return std::nullopt;
            }
            continue;
        }
        if (!self || !processors)
        {
            fail(error, "Processing registry is unavailable");
            return std::nullopt;
        }
        std::unique_ptr<ProcessorRegistry> scope;
        auto*                              registry = processors.data();
        auto                               target   = type;
        if (type == "@inplace")
        {
            const auto key = directions[index];
            scope          = processors->scoped(TerminalProcessing::diagram(stored, key), error);
            if (!self || !processors || !scope)
            {
                return std::nullopt;
            }
            registry = scope.get();
            target   = TerminalProcessing::rootId(key);
        }
        const auto prepared = registry->prepare(target, selection.value("configuration").toMap(), {}, error);
        if (!self || !processors || !prepared)
        {
            return std::nullopt;
        }
        const int ports = shared ? 2 : 1;
        if (prepared->inputCount() != ports || prepared->outputCount() != ports)
        {
            fail(error,
                 shared ? "Shared processing requires a 2x2 processor" :
                          "Independent Rx/Tx processing requires a 1x1 processor");
            return std::nullopt;
        }
        instances[index] = prepared->instantiate(selection.value("overrides").toMap(), error);
        if (!self || !processors || !instances[index])
        {
            return std::nullopt;
        }
    }

    if (shared)
    {
        instances[1] = instances[0];
    }
    return instances;
}

void TerminalController::setProcessorFavorites(ProcessorFavorites& favorites)
{
    _processorFavorites = &favorites;
}

bool TerminalController::_configureProcessing(Runtime& terminal, QString* error, bool txOnly)
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
    auto prepared = _prepareProcessing(configuration, error, txOnly);
    if (!prepared)
    {
        return false;
    }
    auto&      instances = *prepared;
    const auto active    = TerminalProcessing::active(configuration);
    auto*      live      = liveTerminal();
    if (!live)
    {
        return fail(error, "Terminal changed while preparing processing instances");
    }
    if (live->settings.value("processing").toMap() != configuration)
    {
        return fail(error, "Terminal processing settings changed while preparing processing instances");
    }

    if (active.contains("duplex"))
    {
        live->processing->replaceDuplex(std::move(instances[0]));
    }
    else
    {
        if (!txOnly)
        {
            live->processing->replace(ProcessingRouter::Direction::Rx, std::move(instances[0]));
            live = liveTerminal();
            if (!live || live->settings.value("processing").toMap() != configuration)
            {
                return fail(error, "Terminal changed while replacing processing instances");
            }
        }
        live->processing->replace(ProcessingRouter::Direction::Tx, std::move(instances[1]));
    }
    live = liveTerminal();
    if (!live || live->settings.value("processing").toMap() != configuration)
    {
        return fail(error, "Terminal changed while replacing processing instances");
    }
    live->processingConfiguration = configuration;
    live->advanced->setProcessingRouter(
        active.value("tx").toMap().isEmpty() && !active.contains("duplex") ? nullptr : live->processing.get());
    return true;
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
    const auto active   = TerminalProcessing::active(configuration);
    const auto previous = TerminalProcessing::active(baseline);
    const auto changed  = [&](const QString& key) {
        return active.value(key) != previous.value(key)
            || (active.value(key).toMap().value("type") == "@inplace"
                && TerminalProcessing::diagram(configuration, key) != TerminalProcessing::diagram(baseline, key));
    };
    const bool sharedChanged        = active.contains("duplex") != previous.contains("duplex") || changed("duplex");
    const bool rxChanged            = sharedChanged || changed("rx");
    const bool txChanged            = sharedChanged || changed("tx");
    auto       changedConfiguration = configuration;
    if (!rxChanged)
    {
        changedConfiguration.remove("rx");
    }
    if (!txChanged)
    {
        changedConfiguration.remove("tx");
    }
    std::optional<std::array<std::shared_ptr<Processor>, 2>> prepared;
    if (rxChanged || txChanged)
    {
        prepared = _prepareProcessing(changedConfiguration, error);
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
    if (!rxChanged && !txChanged)
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
    if (txChanged)
    {
        terminal.advanced->stop();
    }
    auto* live = liveTerminal();
    if (!live)
    {
        return true;
    }
    live->processingUpdate = false;
    if (active.contains("duplex"))
    {
        live->processing->replaceDuplex(std::move((*prepared)[0]));
    }
    else if (rxChanged)
    {
        live->processing->replace(ProcessingRouter::Direction::Rx, std::move((*prepared)[0]));
    }
    live = liveTerminal();
    if (!live)
    {
        return true;
    }
    if (txChanged && !active.contains("duplex"))
    {
        live->processing->replace(ProcessingRouter::Direction::Tx, std::move((*prepared)[1]));
    }
    live = liveTerminal();
    if (!live)
    {
        return true;
    }
    live->processingConfiguration = configuration;
    live->advanced->setProcessingRouter(
        active.value("tx").toMap().isEmpty() && !active.contains("duplex") ? nullptr : live->processing.get());
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
        dialog.addPage("terminal.connection",
                       "Terminal",
                       "Connection",
                       "Transport settings take effect on the next connection.",
                       _types.at(terminal->type).schema,
                       terminal->settings);
    }
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
    terminal.device->close();
    _changed(id);
}

void TerminalController::_showTabMenu(const QPoint& position)
{
    auto&      tabs  = _window.terminalTabs();
    const auto index = tabs.tabBar()->tabAt(position);
    if (index < 0)
    {
        return;
    }
    tabs.setCurrentIndex(index);
    refreshTerminalActions();
    const auto terminalId = selectedTerminal();
    if (terminalId.isEmpty())
    {
        return;
    }

    auto* menu = new QMenu(&_window);
    menu->setObjectName("terminalTabMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QList<QString> ids;
    for (const auto& [id, item] : _actions)
    {
        if (item.action && item.action->isVisible() && item.registration.role != Action::Role::Create
            && (item.registration.terminalType == _terminals.at(terminalId)->type || id == "Terminal.Detach"))
        {
            ids.append(id);
        }
    }
    const auto rank = [this](const QString& id) {
        const auto role = _actions.at(id).registration.role;
        return role == Action::Role::Close ? 100 : role == Action::Role::Additional ? 90 : static_cast<int>(role);
    };
    std::sort(ids.begin(), ids.end(), [&](const QString& left, const QString& right) {
        return rank(left) == rank(right) ? left < right : rank(left) < rank(right);
    });
    bool    hasClose = false;
    QString previousGroup;
    for (const auto& id : ids)
    {
        const auto& item = _actions.at(id);
        hasClose         = hasClose || item.registration.role == Action::Role::Close;
        if (!previousGroup.isEmpty() && previousGroup != item.registration.group)
        {
            menu->addSeparator();
        }
        previousGroup                  = item.registration.group;
        const QPointer<QAction> source = item.action;
        auto*                   proxy  = menu->addAction(source->icon(), source->text());
        proxy->setObjectName(id);
        proxy->setToolTip(source->toolTip());
        proxy->setEnabled(source->isEnabled());
        proxy->setCheckable(source->isCheckable());
        proxy->setChecked(source->isChecked());
        connect(source, &QAction::changed, proxy, [source, proxy] {
            if (source)
            {
                proxy->setEnabled(source->isEnabled());
                proxy->setVisible(source->isVisible());
                proxy->setCheckable(source->isCheckable());
                proxy->setChecked(source->isChecked());
            }
        });
        connect(source, &QObject::destroyed, proxy, [proxy] { proxy->setEnabled(false); });
        connect(proxy, &QAction::triggered, this, [this, id, source, terminalId] {
            // Revalidate identity and registration; a tab index may have moved or disappeared.
            const auto terminal = _terminals.find(terminalId);
            const auto action   = _actions.find(id);
            if (!source || terminal == _terminals.end() || action == _actions.end() || action->second.action != source)
            {
                return;
            }
            _window.terminalTabs().setCurrentWidget(terminal->second->panel);
            refreshTerminalActions();
            if (source->isVisible() && source->isEnabled())
            {
                source->trigger();
            }
        });
    }
    if (!hasClose)
    {
        if (!menu->actions().isEmpty())
        {
            menu->addSeparator();
        }
        auto* close = menu->addAction(Icons::icon("x"), tr("Close"));
        close->setObjectName("Terminal.Close");
        connect(close, &QAction::triggered, this, [this, terminalId] {
            if (_activeWindow())
            {
                _close(terminalId);
            }
        });
    }
    connect(this, &TerminalController::selectionChanged, menu, [this, menu, terminalId] {
        if (selectedTerminal() != terminalId)
        {
            menu->close();
        }
    });
    if (menu->actions().isEmpty())
    {
        delete menu;
        return;
    }
    menu->popup(tabs.tabBar()->mapToGlobal(position));
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
