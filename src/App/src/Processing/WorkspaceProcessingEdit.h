#pragma once

#include <Termaster/Core/Path.h>
#include <Termaster/Core/Processing/ProcessorRefactoring.h>

class Workspace;
class Session;
class ProcessorRegistry;

/** Application-thread multi-document editor transaction; never restarts running processors. */
class WorkspaceProcessingEdit
{
public:
    static bool apply(Workspace&                                 workspace,
                      Session*                                   active,
                      ProcessorRegistry&                         registry,
                      const QVariantMap&                         baselineDefinitions,
                      const QVariantMap&                         draftDefinitions,
                      const QList<ProcessorRefactoring::Rename>& renames,
                      QString*                                   error = nullptr);
    /** Rolls back an interrupted transaction after confinement and exact-byte checks.
     * Unexpected external edits retain the journal and all files for manual resolution.
     */
    static bool recover(const Path& workspace, QString* error = nullptr);
};
