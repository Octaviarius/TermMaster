#pragma once

#include <QSettings>

class Settings : public QSettings
{
public:
    using QSettings::QSettings;

    template <typename T>
    void setValue(QAnyStringView key, const T& value)
    {
        QSettings::setValue(key, value);
    }

    template <typename T>
    void setValues(QAnyStringView key, const QList<T>& value)
    {
        auto count = beginWriteArray(value.count());
        for (int i = 0; i < value.count(); i++)
        {
            setArrayIndex(i);
            setValue<T>("", value[i]);
        }
        endArray();
    }

    template <typename T>
    T value(QAnyStringView key, const T& defaultValue)
    {
        if (_autocreate && !contains(key))
        {
            setValue<T>(key, defaultValue);
            return defaultValue;
        }
        else
        {
            return QSettings::value(key, defaultValue).template value<T>();
        }
    }

    template <typename T>
    T value(QAnyStringView key)
    {
        return value<T>(key, T());
    }

    template <typename T>
    QList<T> values(QAnyStringView key)
    {
        QList<T> res;

        auto count = beginReadArray(key);
        for (int i = 0; i < count; i++)
        {
            setArrayIndex(i);
            res.append(value<T>(""));
        }
        endArray();

        return res;
    }

    void setAutoCreate(bool value)
    {
        _autocreate = true;
    }

    bool isAutoCreate() const
    {
        return _autocreate;
    }

private:
    bool _autocreate = false;
};
