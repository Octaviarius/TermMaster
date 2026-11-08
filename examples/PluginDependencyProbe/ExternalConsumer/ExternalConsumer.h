#pragma once

#include <ProbeInterface.h>
#include <QObject>

class ExternalConsumer final : public QObject, public IProbePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.octaviarius.TermMaster.PluginDependencyProbe/1.0" FILE "ExternalConsumer.json")
    Q_INTERFACES(IProbePlugin)

public:
    ExternalConsumer();
    QString id() const override;
    int     calculate() const override;
};
