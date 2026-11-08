#pragma once

int providerAdd(int left, int right);

class ProviderMath
{
public:
    static int multiply(int left, int right);
};

class ProviderAccumulator
{
private:
    int _value;

public:
    explicit ProviderAccumulator(int value);
    ~ProviderAccumulator();
    int add(int value);
};
