#pragma once

#include <QtPlugin>

class PluginHost;

/** Optional capability installed before Plugin::startup; the host outlives shutdown. */
class HostedPlugin
{
public:
    virtual ~HostedPlugin()                = default;
    virtual void setHost(PluginHost* host) = 0;
};

Q_DECLARE_INTERFACE(HostedPlugin, "org.octaviarius.Termaster.HostedPlugin/9.0")
