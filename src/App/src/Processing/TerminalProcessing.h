#pragma once

#include <QString>
#include <QVariantMap>

/** Terminal-owned processing profiles. Inactive selections and diagrams remain persisted. */
class TerminalProcessing final
{
public:
    static QString     mode(const QVariantMap& configuration);
    static bool        validateMode(const QVariantMap& configuration, QString* error);
    static QVariantMap active(const QVariantMap& configuration);
    static bool        sameActive(const QVariantMap& left, const QVariantMap& right);
    static QString     rootId(const QString& direction);
    static QVariantMap diagram(const QVariantMap& configuration, const QString& direction);
};
