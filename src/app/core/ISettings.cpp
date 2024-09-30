#include "ISettings.h"

template <>
void ISettings::setValue<QVariant>(QString path, const QVariant& v)
{
    _setValue(path, v);
}
template <>
QVariant ISettings::value<QVariant>(QString path, const QVariant& fallback)
{
    return _value(path, fallback);
}

template <>
void ISettings::setValue<int>(QString path, const int& v)
{
    _setValue(path, v);
}
template <>
int ISettings::value<int>(QString path, const int& fallback)
{
    return _value(path, fallback).toInt();
}

template <>
void ISettings::setValue<uint>(QString path, const uint& v)
{
    _setValue(path, v);
}
template <>
uint ISettings::value<uint>(QString path, const uint& fallback)
{
    return _value(path, fallback).toUInt();
}

template <>
void ISettings::setValue<QString>(QString path, const QString& v)
{
    _setValue(path, v);
}
template <>
QString ISettings::value<QString>(QString path, const QString& fallback)
{
    return _value(path, fallback).toString();
}

template <>
void ISettings::setValue<QList<int>>(QString path, const QList<int>& v)
{
    QVariantList list;

    for (auto it : v)
    {
        list.append(it);
    }

    _setValue(path, list);
}
