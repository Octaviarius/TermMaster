#pragma once

#include <QList>
#include <QMap>

template <typename E>
class EnumReflection
{
public:
    using Pair = QPair<const char*, E>;

    constexpr EnumReflection(std::initializer_list<Pair> list)
    {
        _pairs = list;

        for (auto it : list)
        {
            _names.append(it.first);
            _enums.append(it.second);
            _enum2name.insert(it.second, it.first);
            _name2enum.insert(it.first, it.second);
        }
    }

    QString nameOfEnum(E value) const
    {
        return _enum2name[value];
    }

    E enumOfName(const QString& name) const
    {
        return _name2enum[name];
    }

    E enumValue(int index) const
    {
        return _enums[index];
    }

    E enumValue(int index, E fallBack) const
    {
        return _enums.value(index, fallBack);
    }

    E fixEnumValue(E enumValue, E fallBack) const
    {
        return _enums.contains(enumValue) ? enumValue : fallBack;
    }

    QString nameValue(int index) const
    {
        return _names[index];
    }

    QString nameValue(int index, E fallBack) const
    {
        return _names.value(index, _enum2name[fallBack]);
    }

    int indexOfEnum(E enumValue) const
    {
        return _enums.indexOf(enumValue);
    }

    int indexOfName(const QString& name) const
    {
        return _names.indexOf(name);
    }

    QList<QString> names() const
    {
        return _names;
    }

    QList<E> enums() const
    {
        return _enums;
    }

    QString operator[](E enumValue) const
    {
        return nameOfEnum(enumValue);
    }

    E operator[](QString name) const
    {
        return enumOfName(name);
    }

    Pair operator[](int index) const
    {
        return _pairs[index];
    }

private:
    QList<Pair>      _pairs;
    QMap<QString, E> _name2enum;
    QMap<E, QString> _enum2name;
    QList<QString>   _names;
    QList<E>         _enums;
};
