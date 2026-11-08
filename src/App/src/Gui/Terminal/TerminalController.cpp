#include "TerminalController.h"

#include "../Main/MainWindow.h"
#include "../Settings/SettingsDialog.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QFile>
#include <QLabel>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/RandomNames.h>
#include <Termaster/Core/Session.h>
#include <Termaster/Core/SettingsSchema.h>
#include <Termaster/Core/Workspace.h>
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
    QString                       id;
    QString                       type;
    QString                       name;
    SettingsOverlay               settings;
    std::unique_ptr<SerialDevice> device;
    QPointer<TerminalDisplay>     display;
    QPointer<QWidget>             panel;
    explicit Runtime(SettingsOverlay configuration) : settings(std::move(configuration))
    {
    }
    ~Runtime()
    {
        if (device)
        {
            device->setCallbacks({});
            device->close();
            device.reset();
        }
        delete panel.data();
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
    connect(qApp, &QApplication::focusChanged, this, [this] { refreshTerminalActions(); });
    connect(qApp, &QGuiApplication::focusWindowChanged, this, [this] { refreshTerminalActions(); });
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
    _types.emplace(id, Type {std::move(type), std::move(schema), std::move(workspaceSchema)});
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
    if (!action.trigger || action.icon.isNull() || _actions.contains(action.id))
    {
        throw std::invalid_argument("Invalid terminal action registration");
    }
    auto* menu = _window.mainMenu().findMenu("/terminal");
    if (!menu || !menu->groups().contains(action.group))
    {
        throw std::invalid_argument("Unknown terminal action group");
    }
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
        _window.toolbar().addAction(presentation, "terminal");
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
    _editors.unregisterWidget(id);
}
void TerminalController::refreshTerminalActions()
{
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
    }
    catch (const std::exception& exception)
    {
        delete terminal->display.data();
        return fail(error, QString::fromUtf8(exception.what()));
    }
    auto* device  = terminal->device.get();
    auto* panel   = terminal->panel.data();
    auto* display = terminal->display.data();
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
            it->second->display->receive(bytes);
        }
    };
    callbacks.stateChanged = [self, id](SerialDevice::State) {
        if (self)
        {
            self->_changed(id);
        }
    };
    callbacks.error = [self](const QString& error) {
        if (self)
        {
            self->_window.statusBar()->showMessage(owned(error));
        }
    };
    device->setCallbacks(std::move(callbacks));
    connect(display,
            &TerminalDisplay::outputBytes,
            this,
            [this, id](const QByteArray& data, TerminalDisplay::OutputKind kind, const QByteArray& echo) {
                auto it = _terminals.find(id);
                if (it == _terminals.end() || it->second->device->state() != SerialDevice::State::Open)
                {
                    return;
                }
                QString    error;
                const auto accepted = it->second->device->write(data, &error);
                if (accepted < 0)
                {
                    _window.statusBar()->showMessage(owned(error));
                }
                else if ((kind == TerminalDisplay::OutputKind::Keyboard || kind == TerminalDisplay::OutputKind::Paste)
                         && accepted == data.size() && it->second->settings.value("input/localEcho", false).toBool())
                {
                    it->second->display->localEcho(echo);
                }
            });
    connect(display, &TerminalDisplay::linkActivated, this, [](const QUrl& url) { QDesktopServices::openUrl(url); });
    connect(display, &TerminalDisplay::titleChanged, this, [this, id](const QString& title) {
        auto it = _terminals.find(id);
        if (it != _terminals.end())
        {
            const int index = _window.terminalTabs().indexOf(it->second->panel);
            _window.terminalTabs().setTabToolTip(index, Qt::convertFromPlainText(title));
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
void TerminalController::_changed(const QString& id)
{
    auto it = _terminals.find(id);
    if (it == _terminals.end())
    {
        return;
    }
    const auto state = it->second->device->state();
    QString    icon;
    QString    status;
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
    const auto index = _window.terminalTabs().indexOf(it->second->panel);
    _window.terminalTabs().setTabText(index, it->second->name);
    _window.terminalTabs().setTabIcon(index, QIcon(":/Termaster/App/Status/" + icon + ".svg"));
    _window.terminalTabs().tabBar()->setAccessibleTabName(index, it->second->name + ", " + status);
    refreshTerminalActions();
}
void TerminalController::connectSelectedTerminal()
{
    auto* terminal = _current();
    if (!terminal || !_activeWindow())
    {
        return;
    }
    const auto type = _types.find(terminal->type);
    if (type == _types.end())
    {
        return;
    }
    QString error;
    if (!_validate(type->second, terminal->settings, &error)
        || !terminal->device->open(_effective(type->second, terminal->settings).value("transport").toMap(), &error))
    {
        _window.statusBar()->showMessage(owned(error));
    }
    refreshTerminalActions();
}
void TerminalController::disconnectSelectedTerminal()
{
    if (auto* terminal = _current(); terminal && _activeWindow())
    {
        terminal->device->close();
    }
    refreshTerminalActions();
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
void TerminalController::addSettingsPages(SettingsDialog& dialog)
{
    if (auto* workspace = _workspace ? _workspace() : nullptr)
    {
        for (const auto& [id, type] : _types)
        {
            if (type.workspaceSchema)
            {
                dialog.addPage("workspace." + id,
                               "Workspace",
                               type.registration.title,
                               "Workspace-wide suggestions for this terminal type.",
                               type.workspaceSchema,
                               workspace->settings());
            }
        }
    }
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
