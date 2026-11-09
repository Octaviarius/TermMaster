#include "ConsolePlugin.h"

#include "ConsoleEnvironment.h"
#include "Gui/Environment/EnvironmentEditor.h"
#include "Gui/Launch/ShellSelector.h"

#include <QFile>
#include <Termaster/ConsoleTerminal/ConsoleTerminal.h>
#include <exception>

void ConsolePlugin::setHost(PluginHost* host)
{
    if (_state == State::Stopped)
    {
        _host = host;
    }
}
bool ConsolePlugin::startup(QString* error)
{
    if (_state == State::Running)
    {
        return true;
    }
    if (!_editorsRegistered.isEmpty() || _typeRegistered || !_actionsRegistered.isEmpty())
    {
        if (error)
        {
            *error = "Console plugin shutdown is incomplete.";
        }
        return false;
    }
    if (!_host)
    {
        if (error)
        {
            *error = "Console plugin requires an application host.";
        }
        _state = State::Failed;
        return false;
    }
    _state = State::Starting;
    QFile schema(":/ConsoleTerminal/Settings.yaml");
    QFile defaultSchema(":/ConsoleTerminal/Defaults.yaml");
    if (!schema.open(QIODevice::ReadOnly) || !defaultSchema.open(QIODevice::ReadOnly))
    {
        if (error)
        {
            *error = !schema.isOpen() ? schema.errorString() : defaultSchema.errorString();
        }
        _state = State::Failed;
        return false;
    }
    try
    {
        _host->registerEditor("consoleEnvironment", [this](const QVariantMap&, QWidget* parent) {
            auto editor = std::make_unique<EnvironmentEditor>(parent);
            _editors.removeIf([](const auto& item) { return item.isNull(); });
            _editors.append(editor.get());
            return editor;
        });
        _editorsRegistered.append("consoleEnvironment");
        _host->registerEditor("consoleShellSelector", [](const QVariantMap&, QWidget* parent) {
            return std::make_unique<ShellSelector>(parent);
        });
        _editorsRegistered.append("consoleShellSelector");
        PluginHost::TerminalType type;
        type.id                    = "console";
        type.title                 = "Local console";
        type.plugin                = "ConsoleTerminal";
        type.settingsSchema        = schema.readAll();
        type.defaultSettingsSchema = defaultSchema.readAll();
        type.createDevice          = createConsoleDevice;
        _host->registerTerminalType(std::move(type));
        _typeRegistered = true;
        _registerActions();
    }
    catch (const std::exception& exception)
    {
        const auto reason = QString::fromUtf8(exception.what());
        QString    cleanupError;
        shutdown(&cleanupError);
        _state = State::Failed;
        if (error)
        {
            *error = cleanupError.isEmpty() ? reason : reason + "; " + cleanupError;
        }
        return false;
    }
    catch (...)
    {
        QString cleanupError;
        shutdown(&cleanupError);
        _state = State::Failed;
        if (error)
        {
            *error = "Console plugin registration failed." + (cleanupError.isEmpty() ? QString() : " " + cleanupError);
        }
        return false;
    }
    _state = State::Running;
    _host->refreshTerminalActions();
    return true;
}
bool ConsolePlugin::shutdown(QString* error)
{
    if (_state == State::Stopped)
    {
        return true;
    }
    _state = State::Stopping;
    if (_host && _typeRegistered && !_host->unregisterTerminalType("console", error))
    {
        _state = State::Failed;
        return false;
    }
    _typeRegistered = false;
    if (_host)
    {
        while (!_actionsRegistered.isEmpty())
        {
            _host->unregisterAction(_actionsRegistered.last());
            _actionsRegistered.removeLast();
        }
        while (!_editorsRegistered.isEmpty())
        {
            _host->unregisterEditor(_editorsRegistered.last());
            _editorsRegistered.removeLast();
        }
    }
    _editors.clear();
    _state = State::Stopped;
    return true;
}
Plugin::Info ConsolePlugin::info() const
{
    return {"ConsoleTerminal", "Local console", "1.0.0", {}};
}
Plugin::State ConsolePlugin::state() const
{
    return _state;
}

bool ConsolePlugin::_actionEnabled(const QString& id) const
{
    if (!_host || _state != State::Running)
    {
        return false;
    }
    if (id == "Console.New")
    {
        return _host->canCreateTerminal();
    }
    if (id == "Console.Environment.Refresh")
    {
        return true;
    }
    if (!_host->hasSelectedTerminal("console"))
    {
        return false;
    }
    auto* device = _host->selectedDevice("console");
    if (id == "Console.Start")
    {
        return device && !_host->connectionRequested("console")
            && (device->state() == SerialDevice::State::Closed || device->state() == SerialDevice::State::Failed);
    }
    if (id == "Console.Stop")
    {
        return device
            && (_host->connectionRequested("console") || device->state() == SerialDevice::State::Open
                || device->state() == SerialDevice::State::Opening);
    }
    if (id == "Console.Restart" || id == "Console.Environment.RefreshRestart")
    {
        return device && device->state() != SerialDevice::State::Closing;
    }
    return true;
}

bool ConsolePlugin::_refreshEnvironment(bool restart)
{
    QString error;
    ConsoleEnvironment::userEnvironment(&error);
    if (!error.isEmpty())
    {
        _host->showMessage(error);
        return false;
    }
    _editors.removeIf([](const auto& item) { return item.isNull(); });
    for (const auto& editor : _editors)
    {
        if (!editor->refreshFromUser(&error))
        {
            _host->showMessage(error);
            return false;
        }
    }
    if (restart)
    {
        if (!_actionEnabled("Console.Environment.RefreshRestart"))
        {
            return false;
        }
        _host->disconnectSelectedTerminal();
        _host->connectSelectedTerminal();
        _host->showMessage(tr("User environment refreshed; console restart requested."));
    }
    else
    {
        _host->showMessage(tr("User environment refreshed for the next console launch. Running shells are unchanged."));
    }
    return true;
}

void ConsolePlugin::_registerActions()
{
    struct Description
    {
        QString id;
        QString title;
        QString group;
        QString icon;
    };
    const QList<Description> descriptions {
        {                       "Console.New",                              "New console",     "create",    "square-terminal"},
        {                     "Console.Start",                                    "Start", "connection",               "play"},
        {                      "Console.Stop",                                     "Stop", "connection",             "square"},
        {                   "Console.Restart",                                  "Restart", "connection",          "rotate-cw"},
        {                     "Console.Close",                                    "Close", "management",                  "x"},
        {                     "Console.Clear",                             "Clear screen",     "output",             "eraser"},
        {                  "Console.Settings",                              "Settings...",   "settings", "sliders-horizontal"},
        {       "Console.Environment.Refresh", "Refresh user environment for next launch",   "settings",         "refresh-cw"},
        {"Console.Environment.RefreshRestart",     "Refresh user environment and restart",   "settings",          "rotate-cw"}
    };
    _actionsRegistered.reserve(descriptions.size());
    for (const auto& item : descriptions)
    {
        PluginHost::Action action;
        action.id       = item.id;
        action.title    = item.title;
        action.group    = item.group;
        action.iconName = item.icon;
        action.visible  = [this, id = item.id] {
            return _host
                && (id == "Console.New" || id == "Console.Environment.Refresh"
                    || _host->hasSelectedTerminal("console"));
        };
        action.enabled = [this, id = item.id] { return _actionEnabled(id); };
        action.trigger = [this, id = item.id] {
            if (!_actionEnabled(id))
            {
                return;
            }
            if (id == "Console.New")
            {
                _host->showCreateTerminal("console");
            }
            else if (id == "Console.Start")
            {
                _host->connectSelectedTerminal();
            }
            else if (id == "Console.Stop")
            {
                _host->disconnectSelectedTerminal();
            }
            else if (id == "Console.Restart")
            {
                _host->disconnectSelectedTerminal();
                _host->connectSelectedTerminal();
            }
            else if (id == "Console.Close")
            {
                _host->closeSelectedTerminal();
            }
            else if (id == "Console.Clear")
            {
                _host->clearSelectedTerminal();
            }
            else if (id == "Console.Settings")
            {
                _host->showSelectedTerminalSettings();
            }
            else if (id == "Console.Environment.Refresh")
            {
                _refreshEnvironment(false);
            }
            else if (id == "Console.Environment.RefreshRestart")
            {
                _refreshEnvironment(true);
            }
        };
        _host->registerAction(std::move(action));
        _actionsRegistered.append(item.id);
    }
}
