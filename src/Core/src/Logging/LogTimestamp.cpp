#include "LogTimestamp.h"

#include <QDateTime>

namespace TermasterLoggingDetail
{
std::string localLogTimestamp(std::uint64_t milliseconds)
{
    const auto date     = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(milliseconds)).toLocalTime();
    const auto offset   = date.offsetFromUtc() / 60;
    const auto absolute = offset < 0 ? -offset : offset;
    return (date.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"))
            + (offset < 0 ? QLatin1Char('-') : QLatin1Char('+'))
            + QStringLiteral("%1:%2")
                  .arg(absolute / 60, 2, 10, QLatin1Char('0'))
                  .arg(absolute % 60, 2, 10, QLatin1Char('0')))
        .toStdString();
}
} // namespace TermasterLoggingDetail
