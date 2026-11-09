#pragma once

#include <QProcessEnvironment>
#include <QVariantMap>

/** Read-only environment snapshots and bounded explicit shell overrides. */
class ConsoleEnvironment
{
public:
    static QProcessEnvironment userEnvironment(QString* error = nullptr);
    static QProcessEnvironment apply(const QProcessEnvironment& base,
                                     const QVariantMap&         overrides,
                                     QString*                   error = nullptr);
    static bool                validateOverrides(const QVariantMap& overrides, QString* error = nullptr);
};
