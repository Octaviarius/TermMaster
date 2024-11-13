#ifndef UTILS_H
#define UTILS_H

#include "CompileTime.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QPoint>
#include <QSharedPointer>
#include <QStringList>
#include <QTimeZone>
#include <QVector>
#include <qsize.h>

using KeySequenceList = QList<QKeySequence>;

template <typename T>
class SmartPointer
{
public:
    template <typename... Args>
    static constexpr SmartPointer create(Args... args)
    {
        union
        {
            SmartPointer sptr;
            int          dummy;
        } u;

        u.sptr._pcb           = malloc(sizeof(Pcb));
        u.sptr._pcb->refCount = 1;
        new (&u.sptr._pcb->instance) T(args...);

        return u.ptr;
    }

    SmartPointer(const SmartPointer& other)
    {
        _pcb = other._pcb;
        inc();
    }

    ~SmartPointer()
    {
        dec();
    }

    SmartPointer& operator=(const SmartPointer& other)
    {
        dec();
        if (!_pcb)
        {
            _pcb = other._pcb;
        }
        inc();
        return *this;
    }

    constexpr size_t refCount() const
    {
        return _pcb->_refCount;
    };

    T* operator->()
    {
        return &_pcb->_instance;
    }

    T& operator*()
    {
        return _pcb->_instance;
    }

private:
    constexpr void inc()
    {
        _pcb->refCount++;
    }

    constexpr void dec()
    {
        if (_pcb->_refCount == 1)
        {
            delete _pcb;
            _pcb = nullptr;
        }
        else
        {
            _pcb->_refCount--;
        }
    }

    struct Pcb
    {
        size_t refCount;
        T      instance;
    };

    Pcb* _pcb;
};

template <typename E, E from, E to>
constexpr E integerToEnum(int value)
{
    return static_cast<E>(std::clamp<int>(value, from, to));
}

//---------------------------------------------------------------------------------------

template <typename T>
inline T slice(T& item, qsizetype start, qsizetype end);

template <typename T>
inline QList<T> slice(QList<T>& item, qsizetype start, qsizetype end)
{
    if (start < 0)
    {
        start += item.count();
    }

    if (end < 0)
    {
        end += item.count();
    }

    return QList<T>(item.begin() + start, item.begin() + end);
}

template <>
inline QString slice(QString& item, qsizetype start, qsizetype end)
{
    if (start < 0)
    {
        start += item.length();
    }

    if (end < 0)
    {
        end += item.length();
    }

    return item.sliced(start, end - start);
}

//---------------------------------------------------------------------------------------

template <typename T>
QSet<T> qListToSet(const QList<T>& list)
{
    return QSet<T>(list.constBegin(), list.constEnd());
}

template <typename T>
QList<T> qSetToList(const QSet<T>& set)
{
    return QList<T>(set.constBegin(), set.constEnd());
}

//---------------------------------------------------------------------------------------

template <typename T>
typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value, T>::type divCeil(const T& a, const T& b)
{
    return (a + (b > 0 ? b - 1 : b + 1)) / b;
}

template <typename T>
typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value, T>::type divCeil(const T& a,
                                                                                                   const T& b)
{
    return (a + (b - 1)) / b;
}

template <typename T>
typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value, T>::type divFloor(const T& a,
                                                                                                  const T& b)
{
    return (a - (b > 0 ? b - 1 : b + 1)) / b;
}

template <typename T>
typename std::enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value, T>::type divFloor(const T& a,
                                                                                                    const T& b)
{
    return a / b;
}

template <typename T>
typename std::enable_if<std::is_integral<T>::value, T>::type divRound(const T& a, const T& b)
{
    return (a + b / 2) / b;
}

//---------------------------------------------------------------------------------------

constexpr QSize abs(const QSize& s)
{
    return QSize(abs(s.width()), abs(s.height()));
}

constexpr QSize operator/(const QSize& a, const QSize& b)
{
    return QSize(a.width() / b.width(), a.height() / b.height());
}

constexpr QSize operator*(const QSize& a, const QSize& b)
{
    return QSize(a.width() * b.width(), a.height() * b.height());
}

constexpr bool operator>(const QSize& a, const QSize& b)
{
    return a.width() > b.width() || a.height() > b.height();
}

constexpr bool operator<(const QSize& a, const QSize& b)
{
    return a.width() < b.width() || a.height() < b.height();
}

//---------------------------------------------------------------------------------------

template <typename T>
inline T positive(const T& value);

template <>
inline QPoint positive(const QPoint& value)
{
    return QPoint(std::max(0, value.x()), std::max(0, value.y()));
}

//---------------------------------------------------------------------------------------

inline size_t getValidUtf8Length(const QByteArray& data)
{
    int length      = data.size();
    int validLength = length;

    for (int i = length - 1; i >= 0; --i)
    {
        auto byte = data[i];

        if ((byte & 0x80) == 0)
        { // ASCII символ (1 байт)
            break;
        }
        else if ((byte & 0xC0) == 0xC0)
        { // Начало много байтовой последовательности
            int sequenceLength = 0;

            if ((byte & 0xE0) == 0xC0)
            {
                sequenceLength = 2; // 2 байта
            }
            else if ((byte & 0xF0) == 0xE0)
            {
                sequenceLength = 3; // 3 байта
            }
            else if ((byte & 0xF8) == 0xF0)
            {
                sequenceLength = 4; // 4 байта
            }

            if (length - i >= sequenceLength)
            {
                break;
            }
            else
            {
                validLength = i;
            }
        }
    }
    return validLength;
}

//---------------------------------------------------------------------------------------

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
