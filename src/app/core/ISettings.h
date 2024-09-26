#pragma once

#include <QVariant>

class ISettings
{
public:
    virtual void     _setValue(QString path, const QVariant& v)                      = 0;
    virtual QVariant _value(QString path, const QVariant& defaultValue = QVariant()) = 0;

    template <typename T = QVariant>
    void setValue(QString path, const T& v);

    template <typename T = QVariant>
    T value(QString path = "", const T& defaultValue = T());

    template <typename E>
    E enumValue(QString path, const E& defaultValue = E());

    virtual bool exists(QString path) const = 0;

    virtual void sync() = 0;

    virtual ISettings* node(QString prefixPath) = 0;
};

template <typename E>
E ISettings::enumValue(QString path, const E& defaultValue)
{
    return static_cast<E>(value<int>(path, defaultValue));
}

template <>
void ISettings::setValue<QVariant>(QString path, const QVariant& v);

template <>
QVariant ISettings::value<QVariant>(QString path, const QVariant& defaultValue);

template <>
void ISettings::setValue<int>(QString path, const int& v);

template <>
int ISettings::value<int>(QString path, const int& defaultValue);

template <>
void ISettings::setValue<uint>(QString path, const uint& v);

template <>
uint ISettings::value<uint>(QString path, const uint& defaultValue);

template <>
void ISettings::setValue<QString>(QString path, const QString& v);

template <>
QString ISettings::value<QString>(QString path, const QString& defaultValue);
