#include "ProviderPlugin.h"

#include "ProviderApi.h"

#include <QDebug>

int providerAdd(int left, int right)
{
    return left + right;
}

int ProviderMath::multiply(int left, int right)
{
    return left * right;
}

ProviderAccumulator::ProviderAccumulator(int value) : _value(value)
{
}

ProviderAccumulator::~ProviderAccumulator() = default;

int ProviderAccumulator::add(int value)
{
    _value += value;
    return _value;
}

ProviderPlugin::ProviderPlugin()
{
    qInfo("Provider root constructed");
}

QString ProviderPlugin::id() const
{
    return QStringLiteral("probe.provider");
}

int ProviderPlugin::calculate() const
{
    return providerAdd(20, 22);
}
