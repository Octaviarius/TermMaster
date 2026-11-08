#include "ConsumerPlugin.h"

#include "ProviderApi.h"

#include <QDebug>

ConsumerPlugin::ConsumerPlugin()
{
    qInfo("Consumer root constructed");
}

QString ConsumerPlugin::id() const
{
    return QStringLiteral("probe.consumer");
}

int ConsumerPlugin::calculate() const
{
    ProviderAccumulator accumulator(40);
    if (accumulator.add(2) != 42)
    {
        return -1;
    }
    return providerAdd(20, 22) + ProviderMath::multiply(6, 7);
}
