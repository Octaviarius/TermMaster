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
    // Normal C++ calls resolved by the linker, without resolve() or interface lookup.
    return providerAdd(20, 22) + ProviderMath::multiply(6, 7);
}
