#include "SocketPlugin.h"

#include <QFile>
#include <Termaster/SocketTerminal/SocketTerminal.h>
#include <exception>

void SocketPlugin::setHost(PluginHost* host)
{
    if (_state == State::Stopped)
    {
        _host = host;
    }
}
bool SocketPlugin::startup(QString* error)
{
    if (_state == State::Running)
    {
        return true;
    }
    if (_typeRegistered || !_actionsRegistered.isEmpty())
    {
        if (error)
        {
            *error = "Socket plugin shutdown is incomplete.";
        }
        return false;
    }
    if (!_host)
    {
        if (error)
        {
            *error = "Socket plugin requires an application host.";
        }
        _state = State::Failed;
        return false;
    }
    _state = State::Starting;
    QFile schema(":/SocketTerminal/Settings.yaml");
    QFile defaultSchema(":/SocketTerminal/Defaults.yaml");
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
        PluginHost::TerminalType type;
        type.id                    = "socket";
        type.title                 = "Socket";
        type.plugin                = "SocketTerminal";
        type.settingsSchema        = schema.readAll();
        type.defaultSettingsSchema = defaultSchema.readAll();
        type.createDevice          = createSocketDevice;
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
            *error = "Socket plugin registration failed." + (cleanupError.isEmpty() ? QString() : " " + cleanupError);
        }
        return false;
    }
    _state = State::Running;
    _host->refreshTerminalActions();
    return true;
}
bool SocketPlugin::shutdown(QString* error)
{
    if (_state == State::Stopped)
    {
        return true;
    }
    _state = State::Stopping;
    if (_host && _typeRegistered && !_host->unregisterTerminalType("socket", error))
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
    }
    _state = State::Stopped;
    return true;
}
Plugin::Info SocketPlugin::info() const
{
    return {"SocketTerminal", "Socket", "1.0.0", {}};
}
Plugin::State SocketPlugin::state() const
{
    return _state;
}
void SocketPlugin::_registerActions()
{
    _actionsRegistered.reserve(6);
    struct Description
    {
        QString id;
        QString title;
        QString group;
        QString icon;
    };
    const QList<Description> descriptions {
        {       "Socket.New",   "New socket",     "create",            "network"},
        {   "Socket.Connect",      "Connect", "connection",               "plug"},
        {"Socket.Disconnect",   "Disconnect", "connection",             "unplug"},
        {     "Socket.Close",        "Close", "management",                  "x"},
        {     "Socket.Clear", "Clear screen",     "output",             "eraser"},
        {  "Socket.Settings",  "Settings...",   "settings", "sliders-horizontal"}
    };
    for (const auto& item : descriptions)
    {
        PluginHost::Action action;
        action.id       = item.id;
        action.title    = item.title;
        action.group    = item.group;
        action.iconName = item.icon;
        action.visible  = [this, id = item.id] {
            return _host && (id == "Socket.New" ? true : _host->hasSelectedTerminal("socket"));
        };
        action.enabled = [this, id = item.id] {
            if (!_host || _state != State::Running)
            {
                return false;
            }
            if (id == "Socket.New")
            {
                return _host->canCreateTerminal();
            }
            if (!_host->hasSelectedTerminal("socket"))
            {
                return false;
            }
            auto* device = _host->selectedDevice("socket");
            if (id == "Socket.Connect")
            {
                return device && !_host->connectionRequested("socket")
                    && (device->state() == SerialDevice::State::Closed
                        || device->state() == SerialDevice::State::Failed);
            }
            if (id == "Socket.Disconnect")
            {
                return device
                    && (_host->connectionRequested("socket") || device->state() == SerialDevice::State::Open
                        || device->state() == SerialDevice::State::Opening);
            }
            return true;
        };
        action.trigger = [this, id = item.id] {
            if (!_host || _state != State::Running)
            {
                return;
            }
            if (id == "Socket.New")
            {
                if (_host->canCreateTerminal())
                {
                    _host->showCreateTerminal("socket");
                }
                return;
            }
            if (!_host->hasSelectedTerminal("socket"))
            {
                return;
            }
            auto* device = _host->selectedDevice("socket");
            if (id == "Socket.Connect" && device && !_host->connectionRequested("socket")
                && (device->state() == SerialDevice::State::Closed || device->state() == SerialDevice::State::Failed))
            {
                _host->connectSelectedTerminal();
            }
            else if (id == "Socket.Disconnect" && device
                     && (_host->connectionRequested("socket") || device->state() == SerialDevice::State::Open
                         || device->state() == SerialDevice::State::Opening))
            {
                _host->disconnectSelectedTerminal();
            }
            else if (id == "Socket.Close")
            {
                _host->closeSelectedTerminal();
            }
            else if (id == "Socket.Clear")
            {
                _host->clearSelectedTerminal();
            }
            else if (id == "Socket.Settings")
            {
                _host->showSelectedTerminalSettings();
            }
        };
        _host->registerAction(std::move(action));
        _actionsRegistered.append(item.id);
    }
}
