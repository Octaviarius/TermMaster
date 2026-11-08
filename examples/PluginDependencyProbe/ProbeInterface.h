#pragma once

#include <QString>
#include <QtPlugin>

class IProbePlugin
{
public:
    virtual ~IProbePlugin()           = default;
    virtual QString id() const        = 0;
    virtual int     calculate() const = 0;
};

Q_DECLARE_INTERFACE(IProbePlugin, "org.octaviarius.TermMaster.PluginDependencyProbe/1.0")
