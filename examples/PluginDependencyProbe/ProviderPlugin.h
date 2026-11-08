#pragma once

#include "ProbeInterface.h"

#include <QObject>

class ProviderPlugin final : public QObject, public IProbePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.octaviarius.Termaster.PluginDependencyProbe/1.0" FILE "Provider.json")
    Q_INTERFACES(IProbePlugin)

public:
    ProviderPlugin();
    QString id() const override;
    int     calculate() const override;
};
