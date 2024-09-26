#include "ISettings.h"

template <>
void ISettings::setValue<QVariant>(QString path, const QVariant& v)
{
    _setValue(path, v);
}

template <>
QVariant ISettings::value<QVariant>(QString path, const QVariant& defaultValue)
{
    return _value(path, defaultValue);
}

template <>
void ISettings::setValue<int>(QString path, const int& v)
{
    _setValue(path, v);
}

template <>
int ISettings::value<int>(QString path, const int& defaultValue)
{
    return _value(path, defaultValue).toInt();
}

template <>
void ISettings::setValue<uint>(QString path, const uint& v)
{
    _setValue(path, v);
}

template <>
uint ISettings::value<uint>(QString path, const uint& defaultValue)
{
    return _value(path, defaultValue).toUInt();
}

template <>
void ISettings::setValue<QString>(QString path, const QString& v)
{
    _setValue(path, v);
}

template <>
QString ISettings::value<QString>(QString path, const QString& defaultValue)
{
    return _value(path, defaultValue).toString();
}
