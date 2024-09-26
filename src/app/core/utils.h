#ifndef UTILS_H
#define UTILS_H

#include "CompileTime.h"

#include <QDateTime>
#include <QMap>
#include <QSharedPointer>
#include <QStringList>
#include <QTimeZone>
#include <QVector>

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
