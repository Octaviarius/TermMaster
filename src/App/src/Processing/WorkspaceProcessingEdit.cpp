#include "WorkspaceProcessingEdit.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/Session.h>
#include <Termaster/Core/Workspace.h>
#include <Termaster/Core/WorkspaceFileTransaction.h>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
constexpr qint64 MaximumDocument = 4 * 1024 * 1024;
using Change                     = WorkspaceFileTransaction::Change;
void reject(const QString& text)
{
    throw std::runtime_error(text.toStdString());
}
QByteArray read(const QString& path, qint64 maximum = MaximumDocument)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        reject("Cannot read " + path + ": " + file.errorString());
    }
    const auto bytes = file.read(maximum + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maximum)
    {
        reject("Cannot read bounded document: " + path);
    }
    return bytes;
}
QString canonical(const QString& path)
{
    const QFileInfo info(path);
    const auto      result = info.canonicalFilePath();
    if (!info.isFile() || result.isEmpty())
    {
        reject("Transaction document is not an existing regular file: " + path);
    }
    return result;
}
QString journalPath(const Path& workspace)
{
    return WorkspaceFileTransaction::journalPath(workspace);
}
bool confined(const QString& path, const QString& workspace)
{
    const auto root     = QFileInfo(QFileInfo(workspace).dir().filePath("sessions")).canonicalFilePath();
    const auto relative = QDir(root).relativeFilePath(path);
    return !root.isEmpty() && QFileInfo(path).fileName() == "session.yaml" && relative != ".."
        && !relative.startsWith("../") && !QDir::isAbsolutePath(relative);
}
QVariant merge(const QVariant& baseline,
               bool            hadBaseline,
               const QVariant& draft,
               bool            hasDraft,
               const QVariant& current,
               bool            hasCurrent,
               const QString&  path,
               bool&           present)
{
    if (hadBaseline == hasDraft && (!hadBaseline || baseline == draft))
    {
        present = hasCurrent;
        return current;
    }
    if ((hadBaseline == hasCurrent && (!hadBaseline || baseline == current))
        || (hasDraft == hasCurrent && (!hasDraft || draft == current)))
    {
        present = hasDraft;
        return draft;
    }
    if (hasDraft && hasCurrent && draft.typeId() == QMetaType::QVariantMap && current.typeId() == QMetaType::QVariantMap
        && (!hadBaseline || baseline.typeId() == QMetaType::QVariantMap))
    {
        const auto    before = baseline.toMap();
        const auto    edited = draft.toMap();
        const auto    latest = current.toMap();
        QSet<QString> keys;
        for (const auto& key : before.keys())
        {
            keys.insert(key);
        }
        for (const auto& key : edited.keys())
        {
            keys.insert(key);
        }
        for (const auto& key : latest.keys())
        {
            keys.insert(key);
        }
        QVariantMap result;
        for (const auto& key : keys)
        {
            bool       childPresent = false;
            const auto value        = merge(before.value(key),
                                            before.contains(key),
                                            edited.value(key),
                                            edited.contains(key),
                                            latest.value(key),
                                            latest.contains(key),
                                            path + '/' + key,
                                            childPresent);
            if (childPresent)
            {
                result.insert(key, value);
            }
        }
        present = true;
        return result;
    }
    reject("Concurrent processor edit conflicts at " + path);
    return {};
}
} // namespace

bool WorkspaceProcessingEdit::recover(const Path& workspace, QString* error)
{
    return WorkspaceFileTransaction::recover(workspace, error);
}

bool WorkspaceProcessingEdit::apply(Workspace&                                 workspace,
                                    Session*                                   active,
                                    ProcessorRegistry&                         registry,
                                    const QVariantMap&                         baselineDefinitions,
                                    const QVariantMap&                         draftDefinitions,
                                    const QList<ProcessorRefactoring::Rename>& renames,
                                    QString*                                   error)
{
    if (error)
    {
        error->clear();
    }
    QString                   journal;
    bool                      journalCommitted = false;
    bool                      activeAdopted    = false;
    bool                      workspaceAdopted = false;
    QByteArray                activeSnapshot;
    QByteArray                workspaceSnapshot;
    QVariantMap               processorSnapshot;
    Workspace::ProcessorFiles processorDiskSnapshot;
    const bool                activeDirty    = active && active->isDirty();
    const bool                workspaceDirty = workspace.isDirty();
    try
    {
        if (baselineDefinitions == draftDefinitions && renames.isEmpty())
        {
            return true;
        }
        journal = journalPath(workspace.filePath());
        if (QFileInfo::exists(journal) || QFileInfo(journal).isSymLink())
        {
            reject("An interrupted processing transaction must be recovered before editing");
        }
        if (!renames.isEmpty() && workspace.sessions().values().size() > 4096)
        {
            reject("Processing rename preflight supports at most 4096 inspected session documents");
        }
        qint64     readBudget  = 32 * 1024 * 1024;
        const auto reserveRead = [&](qint64 count) {
            if (count < 0 || count > MaximumDocument || count > readBudget)
            {
                reject("Processing preflight exceeds its 32 MiB read budget or 4 MiB document limit");
            }
            readBudget -= count;
        };
        const auto preflightRead = [&](const QString& path) {
            const auto size = QFileInfo(path).size();
            if (size > readBudget || readBudget <= 0)
            {
                reject("Processing preflight exceeds its 32 MiB read budget");
            }
            const auto bytes = read(path, qMin(MaximumDocument, readBudget));
            reserveRead(bytes.size());
            return bytes;
        };
        reserveRead(QFileInfo(workspace.filePath().toString()).size());
        for (const auto& bytes : workspace.processorDiskFiles())
        {
            reserveRead(bytes.size());
        }
        if (active && !renames.isEmpty())
        {
            reserveRead(QFileInfo(active->filePath().toString()).size());
        }
        if (!workspace.diskUnchanged(error) || (active && !renames.isEmpty() && !active->diskUnchanged(error)))
        {
            return false;
        }
        const auto workspaceFile = canonical(workspace.filePath().toString());
        const auto current       = workspace.processorDefinitions().values();
        bool       present       = false;
        const auto merged        = merge(baselineDefinitions,
                                         true,
                                         draftDefinitions,
                                         true,
                                         current,
                                         true,
                                         "processorDefinitions",
                                         present)
                                       .toMap();
        const auto refactored    = ProcessorRefactoring::transform(current, merged, renames, {}, error);
        if (!refactored)
        {
            return false;
        }
        const auto definitions = refactored->definitions;
        const auto parsed      = ProcessorRegistry::parseDefinitions(definitions, error);
        if (!parsed || !registry.validatePublication("workspace", *parsed, error))
        {
            return false;
        }
        const auto originalWorkspaceCache = workspace.prepareProcessorDefinitions(current, error);
        const auto replacementWorkspace   = workspace.prepareProcessorDefinitions(definitions, error);
        const auto replacementProcessors  = workspace.prepareProcessorFiles(definitions, error);
        if (!originalWorkspaceCache || !replacementWorkspace || !replacementProcessors)
        {
            return false;
        }
        workspaceSnapshot     = *originalWorkspaceCache;
        processorSnapshot     = current;
        processorDiskSnapshot = workspace.processorDiskFiles();
        QList<Change> changes;
        qint64        journalPayload = 0;
        const auto    append         = [&](Change change) {
            journalPayload += (change.original ? change.original->size() : 0)
                            + (change.replacement ? change.replacement->size() : 0) + change.path.toUtf8().size();
            if (changes.size() >= 4096 || journalPayload > 20 * 1024 * 1024)
            {
                reject("Processing transaction exceeds bounded journal capacity");
            }
            changes.push_back(std::move(change));
        };
        append({workspaceFile, preflightRead(workspaceFile), *replacementWorkspace});
        QSet<QString> processorPaths;
        for (const auto& path : processorDiskSnapshot.keys())
        {
            processorPaths.insert(path);
        }
        for (const auto& path : replacementProcessors->keys())
        {
            processorPaths.insert(path);
        }
        for (const auto& path : processorPaths)
        {
            const auto before = processorDiskSnapshot.contains(path) ?
                                    std::optional<QByteArray>(processorDiskSnapshot.value(path)) :
                                    std::nullopt;
            const auto after  = replacementProcessors->contains(path) ?
                                    std::optional<QByteArray>(replacementProcessors->value(path)) :
                                    std::nullopt;
            append({path, before, after});
        }
        QByteArray    activeReplacement;
        QSet<QString> visited;
        qsizetype     inspected  = 0;
        const auto    addSession = [&](Session& session, const QByteArray& disk, bool isActive) {
            const auto path = canonical(session.filePath().toString());
            if (!confined(path, workspaceFile))
            {
                reject("Session transaction document escapes the workspace sessions root");
            }
            if (visited.contains(path))
            {
                return;
            }
            visited.insert(path);
            const auto result = ProcessorRefactoring::transform(current,
                                                                merged,
                                                                renames,
                                                                session.objects().values(),
                                                                error);
            if (!result)
            {
                reject(error ? *error : "Cannot refactor session processor overrides");
            }
            if (result->definitions != definitions)
            {
                reject("Inconsistent processing definition refactoring");
            }
            if (result->sessionObjects == session.objects().values())
            {
                return;
            }
            const auto replacement = session.prepareObjects(result->sessionObjects, error);
            if (!replacement)
            {
                reject(error ? *error : "Cannot prepare session document");
            }
            if (isActive)
            {
                const auto snapshot = session.prepareObjects(session.objects().values(), error);
                if (!snapshot)
                {
                    reject(error ? *error : "Cannot snapshot active session");
                }
                activeSnapshot    = *snapshot;
                activeReplacement = *replacement;
            }
            append({path, disk, *replacement});
        };
        if (active && !renames.isEmpty())
        {
            ++inspected;
            const auto disk = preflightRead(canonical(active->filePath().toString()));
            reserveRead(disk.size());
            // Validate the on-disk envelope too; the live owner's unrelated pending values win.
            Session persisted(active->directory(), workspace.settings());
            if (!persisted.load(error))
            {
                reject(error ? *error : "Cannot validate active session document");
            }
            if (preflightRead(active->filePath().toString()) != disk)
            {
                reject("Active session changed during preflight");
            }
            addSession(*active, disk, true);
        }
        const auto sessionIds = renames.isEmpty() ? QStringList {} : workspace.sessionIds();
        for (const auto& id : sessionIds)
        {
            const auto directory = workspace.sessionDirectory(id, error);
            if (!directory)
            {
                reject(error ? *error : "Cannot resolve session directory");
            }
            const auto path = canonical(directory->join("session.yaml").toString());
            if (visited.contains(path))
            {
                continue;
            }
            if (++inspected > 4096)
            {
                reject("Processing rename preflight supports at most 4096 inspected session documents");
            }
            const auto disk = preflightRead(path);
            reserveRead(disk.size());
            auto session = std::make_unique<Session>(*directory, workspace.settings());
            if (!session->load(error))
            {
                reject(error ? *error : "Cannot load registered session");
            }
            if (preflightRead(path) != disk)
            {
                reject("Registered session changed during preflight: " + path);
            }
            addSession(*session, disk, false);
        }
        if (!WorkspaceFileTransaction::commit(workspace.filePath(), changes, error))
        {
            return false;
        }
        journalCommitted = true;
        if (!activeReplacement.isEmpty())
        {
            if (!active->acceptPreparedDocument(activeReplacement, error))
            {
                reject(error ? *error : "Cannot adopt active session transaction");
            }
            activeAdopted = true;
        }
        if (!workspace.acceptPreparedDocument(*replacementWorkspace, error))
        {
            reject(error ? *error : "Cannot adopt workspace transaction");
        }
        workspaceAdopted = true;
        if (!workspace.acceptProcessorDefinitions(definitions, *replacementProcessors, error))
        {
            reject(error ? *error : "Cannot adopt processor documents");
        }
        if (!WorkspaceFileTransaction::finish(workspace.filePath(), error))
        {
            reject(error ? *error : "Cannot finish processor transaction");
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        QString message = QString::fromUtf8(exception.what());
        QString rollback;
        bool    diskRestored = true;
        if (journalCommitted && !recover(workspace.filePath(), &rollback))
        {
            diskRestored  = false;
            message      += "; rollback failed: " + rollback;
        }
        if (activeAdopted && !active->acceptPreparedDocument(activeSnapshot, &rollback, activeDirty))
        {
            message += "; active cache rollback failed: " + rollback;
        }
        if (workspaceAdopted && !workspace.acceptPreparedDocument(workspaceSnapshot, &rollback, workspaceDirty))
        {
            message += "; workspace cache rollback failed: " + rollback;
        }
        if (workspaceAdopted
            && !workspace.acceptProcessorDefinitions(processorSnapshot,
                                                     processorDiskSnapshot,
                                                     &rollback,
                                                     workspaceDirty))
        {
            message += "; processor cache rollback failed: " + rollback;
        }
        if (diskRestored && activeAdopted && !active->captureDiskBaseline(&rollback))
        {
            message += "; active disk baseline rollback failed: " + rollback;
        }
        if (diskRestored && workspaceAdopted && !workspace.captureDiskBaseline(&rollback))
        {
            message += "; workspace disk baseline rollback failed: " + rollback;
        }
        if (error)
        {
            *error = message;
        }
        return false;
    }
}
