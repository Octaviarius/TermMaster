#pragma once

#include <QList>
#include <QVariantMap>
#include <optional>

/** Pure, bounded occurrence refactoring transaction. Inputs and files remain unchanged on failure. */
class ProcessorRefactoring
{
public:
    struct Rename
    {
        QString block;
        QString from;
        QString to;
    };
    struct Result
    {
        QVariantMap definitions;
        QVariantMap sessionObjects;
    };

    /** Rename records describe sequential edits to existing node identities.
     * Rewrites private occurrence paths, definition defaults and nested node overrides.
     * Missing providers remain opaque. Valid original paths to removed/replaced occurrences
     * are pruned; malformed or stale original paths reject the complete transaction.
     */
    static std::optional<Result> transform(const QVariantMap&   originalDefinitions,
                                           const QVariantMap&   finalDraftDefinitions,
                                           const QList<Rename>& renames,
                                           const QVariantMap&   sessionObjects,
                                           QString*             error = nullptr);
};
