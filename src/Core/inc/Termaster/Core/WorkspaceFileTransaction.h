#pragma once
#include <QByteArray>
#include <QList>
#include <Termaster/Core/Path.h>
#include <optional>

/** Bounded workspace-local file transaction shared by normal saves and editor
 * refactoring. An absent byte state means no file, distinct from an empty file.
 * A durable journal remains until cache adoption is acknowledged by finish().
 */
class WorkspaceFileTransaction
{
public:
    struct Change
    {
        QString                   path;
        std::optional<QByteArray> original;
        std::optional<QByteArray> replacement;
    };
    static QString journalPath(const Path& workspace);
    static bool    commit(const Path& workspace, const QList<Change>& changes, QString* error = nullptr);
    static bool    finish(const Path& workspace, QString* error = nullptr);
    static bool    recover(const Path& workspace, QString* error = nullptr);
};
