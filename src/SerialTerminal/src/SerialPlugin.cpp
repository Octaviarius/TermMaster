#include "SerialPlugin.h"

#include "Gui/Serial/BaudRateSelector.h"
#include "Gui/Serial/PortSelector.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>
#include <Termaster/SerialTerminal/SerialTerminal.h>
#include <exception>

void SerialPlugin::setHost(PluginHost* host)
{
    if (_state == State::Stopped)
    {
        _host = host;
    }
}
bool SerialPlugin::startup(QString* error)
{
    if (_state == State::Running)
    {
        return true;
    }
    if (!_editorsRegistered.isEmpty() || _typeRegistered || !_actionsRegistered.isEmpty())
    {
        if (error)
        {
            *error = "Serial plugin shutdown is incomplete.";
        }
        return false;
    }
    if (!_host)
    {
        if (error)
        {
            *error = "Serial plugin requires an application host.";
        }
        _state = State::Failed;
        return false;
    }
    _state = State::Starting;
    QFile schema(":/SerialTerminal/Settings.yaml");
    QFile workspaceSchema(":/SerialTerminal/WorkspaceSettings.yaml");
    if (!schema.open(QIODevice::ReadOnly) || !workspaceSchema.open(QIODevice::ReadOnly))
    {
        if (error)
        {
            *error = schema.isOpen() ? workspaceSchema.errorString() : schema.errorString();
        }
        _state = State::Failed;
        return false;
    }
    try
    {
        _host->registerEditor("serialPortSelector", [](const QVariantMap& options, QWidget* parent) {
            return std::make_unique<PortSelector>(options, parent);
        });
        _editorsRegistered.append("serialPortSelector");
        _host->registerEditor("serialBaudRateSelector", [this](const QVariantMap&, QWidget* parent) {
            const auto rates = _host->workspaceSettings().value("serial").toMap().value("baudRates");
            return std::make_unique<BaudRateSelector>(rates, parent);
        });
        _editorsRegistered.append("serialBaudRateSelector");
        PluginHost::TerminalType type;
        type.id                      = "serial";
        type.title                   = "Serial port";
        type.plugin                  = "SerialTerminal";
        type.settingsSchema          = schema.readAll();
        type.workspaceSettingsSchema = workspaceSchema.readAll();
        type.createDevice            = createSerialDevice;
        type.createPanel             = [](QWidget* display, const QVariantMap& configuration, QWidget* parent) {
            auto*      panel     = new QWidget(parent);
            auto*      layout    = new QVBoxLayout(panel);
            const auto transport = configuration.value("transport").toMap();
            auto*      header    = new QLabel(QString("%1 / %2 baud")
                                                  .arg(transport.value("portName").toString())
                                                  .arg(transport.value("baudRate", 115200).toInt()),
                                              panel);
            header->setTextFormat(Qt::PlainText);
            layout->addWidget(header);
            display->setParent(panel);
            layout->addWidget(display, 1);
            return panel;
        };
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
            *error = "Serial plugin registration failed." + (cleanupError.isEmpty() ? QString() : " " + cleanupError);
        }
        return false;
    }
    _state = State::Running;
    _host->refreshTerminalActions();
    return true;
}
bool SerialPlugin::shutdown(QString* error)
{
    if (_state == State::Stopped)
    {
        return true;
    }
    _state = State::Stopping;
    if (_host && _typeRegistered && !_host->unregisterTerminalType("serial", error))
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
    _state = State::Stopped;
    return true;
}
Plugin::Info SerialPlugin::info() const
{
    return {"SerialTerminal", "COM ports", "1.0.0", {}};
}
Plugin::State SerialPlugin::state() const
{
    return _state;
}
void SerialPlugin::_registerActions()
{
    _actionsRegistered.reserve(6);
    struct Description
    {
        QString                id;
        QString                title;
        QString                group;
        QStyle::StandardPixmap icon;
    };
    const QList<Description> descriptions {
        {       "Serial.New",      "New serial terminal",     "create",               QStyle::SP_FileIcon},
        {   "Serial.Connect",      "Connect serial port", "connection",              QStyle::SP_MediaPlay},
        {"Serial.Disconnect",   "Disconnect serial port", "connection",              QStyle::SP_MediaStop},
        {     "Serial.Close",    "Close serial terminal", "management",      QStyle::SP_DialogCloseButton},
        {     "Serial.Clear",   "Clear terminal display",     "output",      QStyle::SP_DialogResetButton},
        {  "Serial.Settings", "Serial terminal settings",   "settings", QStyle::SP_FileDialogDetailedView}
    };
    for (const auto& item : descriptions)
    {
        PluginHost::Action action;
        action.id      = item.id;
        action.title   = item.title;
        action.group   = item.group;
        action.icon    = QApplication::style()->standardIcon(item.icon);
        action.visible = [this, id = item.id] {
            return _host && (id == "Serial.New" ? true : _host->hasSelectedTerminal("serial"));
        };
        action.enabled = [this, id = item.id] {
            if (!_host || _state != State::Running)
            {
                return false;
            }
            if (id == "Serial.New")
            {
                return _host->canCreateTerminal();
            }
            if (!_host->hasSelectedTerminal("serial"))
            {
                return false;
            }
            auto* device = _host->selectedDevice("serial");
            if (id == "Serial.Connect")
            {
                return device && !_host->connectionRequested("serial")
                    && (device->state() == SerialDevice::State::Closed
                        || device->state() == SerialDevice::State::Failed);
            }
            if (id == "Serial.Disconnect")
            {
                return device
                    && (_host->connectionRequested("serial") || device->state() == SerialDevice::State::Open
                        || device->state() == SerialDevice::State::Opening);
            }
            return true;
        };
        action.trigger = [this, id = item.id] {
            if (!_host || _state != State::Running)
            {
                return;
            }
            if (id == "Serial.New")
            {
                if (_host->canCreateTerminal())
                {
                    _host->showCreateTerminal("serial");
                }
                return;
            }
            if (!_host->hasSelectedTerminal("serial"))
            {
                return;
            }
            auto* device = _host->selectedDevice("serial");
            if (id == "Serial.Connect" && device && !_host->connectionRequested("serial")
                && (device->state() == SerialDevice::State::Closed || device->state() == SerialDevice::State::Failed))
            {
                _host->connectSelectedTerminal();
            }
            else if (id == "Serial.Disconnect" && device
                     && (_host->connectionRequested("serial") || device->state() == SerialDevice::State::Open
                         || device->state() == SerialDevice::State::Opening))
            {
                _host->disconnectSelectedTerminal();
            }
            else if (id == "Serial.Close")
            {
                _host->closeSelectedTerminal();
            }
            else if (id == "Serial.Clear")
            {
                _host->clearSelectedTerminal();
            }
            else if (id == "Serial.Settings")
            {
                _host->showSelectedTerminalSettings();
            }
        };
        _host->registerAction(std::move(action));
        _actionsRegistered.append(item.id);
    }
}
