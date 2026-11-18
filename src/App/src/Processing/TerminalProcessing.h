#pragma once

#include <QString>
#include <QVariantMap>

/** Terminal-owned processing Lineflow. Its selection and diagram remain persisted in Bypass. */
class TerminalProcessing final
{
public:
    static QString     mode(const QVariantMap& configuration);
    static bool        validateMode(const QVariantMap& configuration, QString* error);
    static QVariantMap active(const QVariantMap& configuration);
    static bool        sameActive(const QVariantMap& left, const QVariantMap& right);
    static QString     rootId();
    static QVariantMap diagram(const QVariantMap& configuration);
};
