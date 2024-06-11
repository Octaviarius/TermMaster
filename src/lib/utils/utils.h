#ifndef UTILS_H
#define UTILS_H

#include "CompileTime.h"

#include <QDateTime>
#include <QMap>
#include <QStringList>
#include <QTimeZone>
#include <QVector>

using KeySequenceList = QList<QKeySequence>;

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

class BuildTime
{
public:
    uint16_t year;
    uint8_t  month;
    uint8_t  day;

    uint8_t hour;
    uint8_t minute;
    uint8_t second;

    operator QDateTime() const
    {
        return QDateTime(QDate(year, month, day), QTime(hour, minute, second), QTimeZone::UTC);
    }

    static constexpr BuildTime compileTime()
    {
        return BuildTime {COMPILETIME_YEAR,
                          COMPILETIME_MONTH,
                          COMPILETIME_DAY,
                          COMPILETIME_HOUR,
                          COMPILETIME_MIN,
                          COMPILETIME_SEC};
    }
};

#endif // UTILS_H
