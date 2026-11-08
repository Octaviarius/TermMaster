#pragma once

#include "ProbeInterface.h"

#include <QObject>

class ConsumerPlugin final : public QObject, public IProbePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.octaviarius.TermMaster.PluginDependencyProbe/1.0" FILE "Consumer.json")
    Q_INTERFACES(IProbePlugin)

public:
    ConsumerPlugin();
    QString id() const override;
    int     calculate() const override;
};
