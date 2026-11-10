#include "TerminalController.h"

#include "../../Processing/TerminalProcessing.h"
#include "../../Terminal/AdvancedSender.h"
#include "../../Terminal/ProcessingRouter.h"
#include "../Main/MainWindow.h"
#include "../Processing/ProcessorFavorites.h"
#include "../Settings/SettingsDialog.h"
#include "CommandEditor.h"
#include "TerminalProcessingPanel.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
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
    connect(qApp, &QApplication::focusChanged, this, [this] { refreshTerminalActions(); });
    connect(qApp, &QGuiApplication::focusWindowChanged, this, [this] { refreshTerminalActions(); });
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
    closeAll();
    _session = session;
    _restore();
    refreshTerminalActions();
}
void TerminalController::closeAll()
{
    delete _creation.data();
    while (!_terminals.empty())
    {
        _close(_terminals.begin()->first);
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
    _window.statusBar()->showMessage(owned(message));
}
QObject* TerminalController::context()
{
    return this;
}
bool TerminalController::_activeWindow() const
{
    return QApplication::activeWindow() == &_window;
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
    return _activeWindow() && current && current->type == type;
}
SerialDevice* TerminalController::selectedDevice(const QString& type)
{
    auto* current = _current();
    return hasSelectedTerminal(type) ? current->device.get() : nullptr;
}
TerminalController::Runtime* TerminalController::_current() const
{
    auto* selected = _window.terminalTabs().currentWidget();
    for (const auto& [id, terminal] : _terminals)
    {
        if (terminal->panel == selected)
        {
            return terminal.get();
        }
    }
    return nullptr;
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
    refreshTerminalActions();
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
        _close(id);
    }
    _types.erase(type);
    refreshTerminalActions();
    return true;
}
void TerminalController::registerAction(Action action)
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
    auto* menu = _window.mainMenu().findMenu("/terminal");
    if (!menu || !menu->groups().contains(action.group))
    {
        throw std::invalid_argument("Unknown terminal action group");
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
        _actions.emplace(id, RegisteredAction {std::move(action), presentation});
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
    connect(presentation, &QAction::triggered, this, [this, id] {
        auto it = _actions.find(id);
        if (it == _actions.end() || !_activeWindow())
        {
            return;
        }
        auto& registration = it->second.registration;
        if ((!registration.visible || registration.visible()) && (!registration.enabled || registration.enabled()))
        {
            registration.trigger();
        }
        refreshTerminalActions();
    });
    refreshTerminalActions();
}
void TerminalController::unregisterAction(const QString& id)
{
    auto it = _actions.find(id);
    if (it == _actions.end())
    {
        return;
    }
    auto* action = it->second.action.data();
    if (action)
    {
        _window.mainMenu().findMenu("/terminal")->removeAction(action);
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
    const auto* selected = _current();
    const auto  buffered = _actions.find("Terminal.BufferedInput");
    if (buffered != _actions.end() && buffered->second.action)
    {
        buffered->second.action->setChecked(selected && selected->editor && selected->editor->buffered());
    }
    const auto echo = _actions.find("Terminal.LocalEcho");
    if (echo != _actions.end() && echo->second.action)
    {
        echo->second.action->setChecked(selected && selected->settings.value("input/localEcho", false).toBool());
    }
    const auto processing = _actions.find("Terminal.Processing");
    if (processing != _actions.end() && processing->second.action)
    {
        processing->second.action->setChecked(selected && selected->processingPanel
                                              && !selected->processingPanel->isHidden());
    }
    for (auto& [id, item] : _actions)
    {
        if (!item.action)
        {
            continue;
        }
        try
        {
            item.action->setVisible(!item.registration.visible || item.registration.visible());
            item.action->setEnabled(_activeWindow() && (!item.registration.enabled || item.registration.enabled()));
        }
        catch (const std::exception&)
        {
            item.action->setEnabled(false);
        }
    }
}

QVariantMap TerminalController::_effective(const Type& type, SettingsOverlay settings) const
{
    auto values = settings.values();
    for (const auto& field : type.schema->fields())
    {
        if (!settings.contains(field.settingsPath) && field.hasDefault)
        {
            put(values, field.settingsPath.split('/'), 0, field.defaultValue);
        }
    }
    return values;
}
bool TerminalController::_validate(const Type& type, SettingsOverlay settings, QString* error) const
{
    QVariantMap document;
    for (const auto& field : type.schema->fields())
    {
        if (settings.contains(field.settingsPath))
        {
            document.insert(field.key, settings.value(field.settingsPath));
        }
        else if (field.hasDefault)
        {
            document.insert(field.key, field.defaultValue);
        }
    }
    const auto errors = type.schema->validate(document);
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
    if (!_session->sync(error))
    {
        _close(id);
        _session->objects().remove(id);
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
        terminal->targetConfiguration = _effective(type, terminal->settings).value("transport").toMap();
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
                    self->_window.statusBar()->showMessage(owned(error));
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
            it->second->device->resize(columns, rows);
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
            showMessage(error);
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
            showMessage((direction == ProcessingRouter::Direction::Rx ? tr("Rx processing: ") : tr("Tx processing: "))
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
                showMessage(error);
            }
        }
    });
    connect(advanced, &AdvancedSender::stateChanged, editor, &CommandEditor::setSendingState);
    connect(advanced, &AdvancedSender::failed, this, [this](const QString& error) { showMessage(error); });
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
                    showMessage(error);
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
        const QString                      path = QFileDialog::getOpenFileName(&_window, tr("Select file to send"));
        if (!self || path.isEmpty())
        {
            return;
        }
        if (QMessageBox::question(&_window,
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
            showMessage(error);
        }
    });
    connect(_terminals.at(id)->sender.get(), &TerminalSendQueue::failed, this, [this](const QString& error) {
        showMessage(error);
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
            showMessage(error);
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
                    showMessage(error);
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
    const int index = _window.terminalTabs().addTab(panel, _terminals.at(id)->name);
    _window.terminalTabs().setCurrentIndex(index);
    _window.showTerminals(true);
    display->setFocus();
    _changed(id);
    return true;
}
void TerminalController::_restore()
{
    if (!_session)
    {
        return;
    }
    const auto objects = _session->objects().values();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it)
    {
        if (!_types.contains(it.value().toMap().value("type").toString()) || _terminals.contains(it.key()))
        {
            continue;
        }
        QString error;
        if (!_materialize(it.key(), &error))
        {
            _window.statusBar()->showMessage(owned(error));
        }
    }
}
void TerminalController::_close(const QString& id)
{
    auto it = _terminals.find(id);
    if (it == _terminals.end())
    {
        return;
    }
    auto terminal = std::move(it->second);
    _terminals.erase(it);
    if (terminal->panel)
    {
        _window.terminalTabs().removeTab(_window.terminalTabs().indexOf(terminal->panel));
    }
    terminal.reset();
    _window.showTerminals(!_terminals.empty());
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
    const auto label    = parameter.isEmpty() ? type : type + '\n' + detail;
    const auto typeIcon = terminal.type == "serial" ? "cable" :
                          terminal.type == "socket" ? "network" :
                                                      "square-terminal";
    _window.terminalTabs().tabBar()->setTabData(index, Icons::icon(typeIcon));
    _window.terminalTabs().setTabText(index, QString(label).replace('&', "&&"));
    _window.setTerminalStatusIcon(index, QIcon(":/Termaster/App/Status/" + icon + ".svg"));
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
    QFile file(":/Termaster/App/TerminalAppearance.yaml");
    if (!file.open(QIODevice::ReadOnly))
    {
        throw std::runtime_error("Terminal appearance schema is unavailable");
    }
    return std::make_shared<SettingsSchema>(file.readAll());
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
        terminal->display->setAppearance(
            _effective(_types.at(terminal->type), terminal->settings).value("appearance").toMap());
        terminal->editor->setSettings(terminal->settings.value("input").toMap());
        if (terminal->advanced)
        {
            terminal->advanced->configure(terminal->settings.value("input").toMap());
        }
        terminal->display->setFocusProxy(terminal->editor->buffered() ? terminal->editor.data() : nullptr);
        const auto target = _effective(_types.at(terminal->type), terminal->settings).value("transport").toMap();
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
        const auto processing = terminal->settings.value("processing").toMap();
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
            terminal->processingPanel->setConfiguration(terminal->settings.value("processing").toMap());
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
            && item.registration.terminalType == _terminals.at(terminalId)->type)
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
    if (!_validate(type->second, terminal.settings, &error)
        || !terminal.device->open(_effective(type->second, terminal.settings).value("transport").toMap(), &error))
    {
        if (error.isEmpty())
        {
            error = tr("Connection attempt failed.");
        }
        if (terminal.lastError != error)
        {
            _window.statusBar()->showMessage(owned(error));
        }
        terminal.lastError = owned(error);
    }
    _changed(id);
}
