#pragma once

#include <QSet>
#include <QString>

/** Returns the first unoccupied stem plus a space and positive decimal number.
 * Matching is exact; existing names and stable identities are not modified.
 */
QString numberedName(const QString& stem, const QSet<QString>& occupied);
