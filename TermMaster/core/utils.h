#ifndef UTILS_H
#define UTILS_H

#include <QMap>
#include <QStringList>
#include <QVector>

template <typename K, typename V>
constexpr QMap<V, K> inverseQMap(const QMap<K, V>& map)
{
    QMap<V, K> res;

    for (auto it = map.constKeyValueBegin(); it != map.constKeyValueEnd(); ++it)
    {
        res[(*it).second] = (*it).first;
    }

    return res;
}

template <typename V>
constexpr QMap<int, V> qVectorToQMap(const QVector<V>& vec)
{
    QMap<int, V> res;

    for (int i = 0; i < vec.size(); i++)
    {
        res[i] = vec[i];
    }

    return res;
}

template <typename V>
constexpr QMap<V, int> qVectorToInversedQMap(const QVector<V>& vec)
{
    QMap<V, int> res;

    for (int i = 0; i < vec.size(); i++)
    {
        res[vec[i]] = i;
    }

    return res;
}


#endif // UTILS_H
