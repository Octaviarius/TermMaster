#pragma once
#include <QObject>
#include <Termaster/Core/Plugin/HostedPlugin.h>
#include <Termaster/Core/Plugin/Plugin.h>
#include <Termaster/Core/Plugin/PluginHost.h>
class SerialPlugin final : public QObject, public Plugin, public HostedPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.octaviarius.Termaster.Plugin/1.0" FILE "SerialPlugin.json")
    Q_INTERFACES(Plugin HostedPlugin)
public:
    void  setHost(PluginHost* host) override;
    bool  startup(QString* error = nullptr) override;
    bool  shutdown(QString* error = nullptr) override;
    Info  info() const override;
    State state() const override;

private:
    PluginHost* _host  = nullptr;
    State       _state = State::Stopped;
    QStringList _editorsRegistered;
    bool        _typeRegistered = false;
    QStringList _actionsRegistered;
    void        _registerActions();
};
