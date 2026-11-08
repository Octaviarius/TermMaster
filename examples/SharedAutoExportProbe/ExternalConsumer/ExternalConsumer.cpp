#include "ExternalConsumer.h"

#include <ProviderApi.h>
#include <QDebug>

ExternalConsumer::ExternalConsumer()
{
    qInfo("Consumer root constructed");
}

QString ExternalConsumer::id() const
{
    return QStringLiteral("probe.consumer");
}

int ExternalConsumer::calculate() const
{
    ProviderAccumulator accumulator(40);
    if (accumulator.add(2) != 42)
    {
        return -1;
    }
    return providerAdd(20, 22) + ProviderMath::multiply(6, 7);
}
