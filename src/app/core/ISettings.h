#pragma once

#include <QVariant>

class ISettings
{
public:
    virtual void     _setValue(QString path, const QVariant& v)                  = 0;
    virtual QVariant _value(QString path, const QVariant& fallback = QVariant()) = 0;
    virtual int      _beginArrayRead(QString path)                               = 0;
    virtual void     _beginArrayWrite(QString path, int count)                   = 0;
    virtual void     _endArray()                                                 = 0;

    template <typename T = QVariant>
    void setValue(QString path, const T& v);

    template <typename T>
    void setValues(QString path, const QList<T>& v)
    {
        return;
    }

    template <typename T>
    T value(QString path = "", const T& fallback = T());

    template <typename T>
    QList<T> values(QString path = "")
    {
        QList<T> res;

        auto count = _beginArrayRead(path);

        for (int i = 0; i < count; i++)
        {
            res.append(value<T>());
        }

        _endArray();

        return res;
    }

    template <typename E>
    E enumValue(QString path, const E& fallback = E());

    virtual bool exists(QString path) const = 0;

    virtual void sync() = 0;

    virtual ISettings* node(QString prefixPath) = 0;
};

template <typename E>
E ISettings::enumValue(QString path, const E& fallback)
{
    return static_cast<E>(value<int>(path, fallback));
}

template <>
void ISettings::setValue<QVariant>(QString path, const QVariant& v);
template <>
QVariant ISettings::value<QVariant>(QString path, const QVariant& fallback);

template <>
void ISettings::setValue<int>(QString path, const int& v);
template <>
int ISettings::value<int>(QString path, const int& fallback);

template <>
void ISettings::setValue<uint>(QString path, const uint& v);
template <>
uint ISettings::value<uint>(QString path, const uint& fallback);

template <>
void ISettings::setValue<QString>(QString path, const QString& v);
template <>
QString ISettings::value<QString>(QString path, const QString& fallback);
