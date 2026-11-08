#pragma once

#include <QDateTime>
#include <QTimeZone>
#include <Termaster/Core/CompileTime.h>

/** Compiler timestamp used by Version. */
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
