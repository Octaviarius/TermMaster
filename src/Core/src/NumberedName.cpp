#include <Termaster/Core/NumberedName.h>

QString numberedName(const QString& stem, const QSet<QString>& occupied)
{
    // A set of N names cannot occupy all N + 1 candidates.
    for (qsizetype number = 1;; ++number)
    {
        const auto candidate = stem + ' ' + QString::number(number);
        if (!occupied.contains(candidate))
        {
            return candidate;
        }
    }
}
