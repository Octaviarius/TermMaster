#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <Termaster/Core/WorkspaceFileTransaction.h>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
constexpr qint64 MaximumDocument = 4 * 1024 * 1024;
constexpr qint64 MaximumJournal  = 32 * 1024 * 1024;
void             reject(const QString& message)
{
    throw std::runtime_error(message.toStdString());
}
QByteArray read(const QString& path, qint64 limit = MaximumDocument)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        reject("Cannot read " + path + ": " + file.errorString());
    }
    const auto bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > limit)
    {
        reject("Cannot read bounded transaction document: " + path);
    }
    return bytes;
}
QString workspaceFile(const Path& workspace)
{
    const QFileInfo file(workspace.toString());
    if (!file.isFile() || file.isSymLink() || file.canonicalFilePath().isEmpty())
    {
        reject("Transaction workspace is not a regular canonical file");
    }
    return file.canonicalFilePath();
}
void safeDirectory(QString directory)
{
    while (true)
    {
        const QFileInfo info(directory);
        if (info.isSymLink())
        {
            reject("Transaction directory must not be a symbolic link: " + directory);
        }
        if (info.exists() && (!info.isDir() || info.canonicalFilePath() != info.absoluteFilePath()))
        {
            reject("Transaction parent is not a canonical directory: " + directory);
        }
        const auto parent = info.dir().absolutePath();
        if (parent == directory)
        {
            break;
        }
        directory = parent;
    }
}
void confined(const QString& path, const QString& workspace)
{
    const QFileInfo file(path);
    if (path.isEmpty() || QDir::cleanPath(file.absoluteFilePath()) != path || file.isSymLink())
    {
        reject("Transaction target must be an absolute non-link path");
    }
    if (path == workspace)
    {
        return;
    }
    const auto                      processorRoot = workspace + ".d/processors";
    static const QRegularExpression filename("\\A[0-9a-f]{64}\\.yaml\\z");
    if (file.dir().absolutePath() == processorRoot && filename.match(file.fileName()).hasMatch())
    {
        safeDirectory(file.dir().absolutePath());
        return;
    }
    const auto root     = QFileInfo(workspace).dir().filePath("sessions");
    const auto relative = QDir(root).relativeFilePath(path);
    if (file.fileName() != "session.yaml" || relative == ".." || relative.startsWith("../")
        || QDir::isAbsolutePath(relative))
    {
        reject("Transaction document escapes workspace-owned directories: " + path);
    }
    safeDirectory(file.dir().absolutePath());
}
QSet<QString> processorInventory(const QString& workspace)
{
    const auto root = workspace + ".d/processors";
    safeDirectory(root);
    QSet<QString> result;
    if (!QFileInfo::exists(root))
    {
        return result;
    }
    QDirIterator iterator(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    int          inspected = 0;
    while (iterator.hasNext())
    {
        if (++inspected > 4096)
        {
            reject("Processor directory contains too many entries");
        }
        iterator.next();
        const auto entry = iterator.fileInfo();
        if (entry.suffix().compare("yaml", Qt::CaseInsensitive) == 0)
        {
            if (result.size() >= 1024)
            {
                reject("Processor directory contains too many definitions");
            }
            confined(entry.absoluteFilePath(), workspace);
            result.insert(entry.absoluteFilePath());
        }
    }
    return result;
}
void inventory(const QString& workspace, const QList<WorkspaceFileTransaction::Change>& changes, bool recovering)
{
    QSet<QString> before;
    QSet<QString> after;
    for (const auto& change : changes)
    {
        if (QFileInfo(change.path).dir().absolutePath() == workspace + ".d/processors")
        {
            if (change.original)
            {
                before.insert(change.path);
            }
            if (change.replacement)
            {
                after.insert(change.path);
            }
        }
    }
    const auto actual = processorInventory(workspace);
    if (!recovering)
    {
        if (actual != before)
        {
            reject("Processor file inventory was externally changed during transaction");
        }
    }
    else if (!(actual - (before | after)).isEmpty() || !((before & after) - actual).isEmpty())
    {
        reject("Processor recovery inventory was externally changed");
    }
}
void existingAnchor(const WorkspaceFileTransaction::Change& change, const QString& workspace)
{
    if (QFileInfo(change.path).dir().absolutePath() != workspace + ".d/processors"
        && (!change.original || !change.replacement))
    {
        reject("Only processor documents may be created or deleted in this transaction");
    }
}
std::optional<QByteArray> state(const QString& path)
{
    const QFileInfo file(path);
    if (file.isSymLink())
    {
        reject("Transaction target is a symbolic link: " + path);
    }
    if (!file.exists())
    {
        return std::nullopt;
    }
    if (!file.isFile() || file.canonicalFilePath() != path)
    {
        reject("Transaction target is not a canonical regular file: " + path);
    }
    return read(path);
}
std::unique_ptr<QSaveFile> stage(const QString& path, const QByteArray& bytes, bool journal = false)
{
    auto file = std::make_unique<QSaveFile>(path);
    file->setDirectWriteFallback(false);
    if (!file->open(QIODevice::WriteOnly))
    {
        reject("Cannot stage " + path + ": " + file->errorString());
    }
    if (journal && !file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    {
        reject("Cannot restrict processing transaction journal permissions");
    }
    if (file->write(bytes) != bytes.size())
    {
        reject("Cannot stage " + path + ": " + file->errorString());
    }
    return file;
}
QByteArray encode(const QString& workspace, const QList<WorkspaceFileTransaction::Change>& changes)
{
    QJsonArray records;
    qint64     payload = 0;
    for (const auto& change : changes)
    {
        payload += change.original ? change.original->size() : 0;
        payload += change.replacement ? change.replacement->size() : 0;
        if (payload > 20 * 1024 * 1024)
        {
            reject("Processing transaction exceeds bounded journal capacity");
        }
        records.append(QJsonObject {
            {             "path",                                                                           change.path},
            {   "originalExists",                                                                 bool(change.original)},
            {"replacementExists",                                                              bool(change.replacement)},
            {         "original",       change.original ? QString::fromLatin1(change.original->toBase64()) : QString {}},
            {      "replacement", change.replacement ? QString::fromLatin1(change.replacement->toBase64()) : QString {}}
        });
    }
    const auto bytes = QJsonDocument(QJsonObject {
                                         {  "version",         2},
                                         {"workspace", workspace},
                                         {    "files",   records}
    })
                           .toJson(QJsonDocument::Compact);
    if (changes.isEmpty() || changes.size() > 4096 || bytes.size() > MaximumJournal)
    {
        reject("Processing transaction journal exceeds 32 MiB or 4096 files");
    }
    return bytes;
}
std::optional<QByteArray> decode(const QJsonObject& record, const QString& key)
{
    const auto exists = record.value(key + "Exists");
    const auto value  = record.value(key);
    if (!exists.isBool() || !value.isString() || value.toString().size() > MaximumDocument * 2)
    {
        reject("Invalid processing transaction journal state");
    }
    if (!exists.toBool())
    {
        if (!value.toString().isEmpty())
        {
            reject("Absent transaction state has a payload");
        }
        return std::nullopt;
    }
    const auto result = QByteArray::fromBase64Encoding(value.toString().toLatin1(),
                                                       QByteArray::AbortOnBase64DecodingErrors);
    if (!result || result.decoded.size() > MaximumDocument)
    {
        reject("Invalid bounded processing transaction journal payload");
    }
    return result.decoded;
}
bool failure(QString* error, const std::exception& exception)
{
    if (error)
    {
        *error = QString::fromUtf8(exception.what());
    }
    return false;
}
} // namespace
QString WorkspaceFileTransaction::journalPath(const Path& workspace)
{
    const QFileInfo file(workspace.absolute().toString());
    const auto      canonical = file.canonicalFilePath();
    return (canonical.isEmpty() ? file.absoluteFilePath() : canonical) + ".processing-journal.json";
}
bool WorkspaceFileTransaction::commit(const Path& workspace, const QList<Change>& changes, QString* error)
{
    if (error)
    {
        error->clear();
    }
    bool journalWritten  = false;
    bool journalReserved = false;
    try
    {
        const auto root    = workspaceFile(workspace);
        const auto journal = journalPath(workspace);
        if (QFileInfo::exists(journal) || QFileInfo(journal).isSymLink())
        {
            reject("An interrupted processing transaction must be recovered before editing");
        }
        QSet<QString> targets;
        qint64        readBudget = 32 * 1024 * 1024;
        for (const auto& change : changes)
        {
            confined(change.path, root);
            existingAnchor(change, root);
            if (targets.contains(change.path))
            {
                reject("Duplicate transaction document");
            }
            targets.insert(change.path);
            if ((change.original && change.original->size() > MaximumDocument)
                || (change.replacement && change.replacement->size() > MaximumDocument))
            {
                reject("Transaction document exceeds 4 MiB");
            }
            const auto size = QFileInfo(change.path).exists() ? QFileInfo(change.path).size() : 0;
            if (size < 0 || size > readBudget)
            {
                reject("Transaction preflight exceeds 32 MiB read budget");
            }
            readBudget -= size;
            if (state(change.path) != change.original)
            {
                reject("Document changed during transaction preflight: " + change.path);
            }
        }
        inventory(root, changes, false);
        const auto                              bytes = encode(root, changes);
        std::vector<std::unique_ptr<QSaveFile>> staged;
        for (const auto& change : changes)
        {
            if (change.replacement && change.replacement != change.original)
            {
                if (!QDir().mkpath(QFileInfo(change.path).absolutePath()))
                {
                    reject("Cannot create processor directory");
                }
                confined(change.path, root);
                staged.push_back(stage(change.path, *change.replacement));
            }
            else
            {
                staged.push_back(nullptr);
            }
        }
        for (const auto& change : changes)
        {
            confined(change.path, root);
            if (state(change.path) != change.original)
            {
                reject("Document changed during transaction staging: " + change.path);
            }
        }
        inventory(root, changes, false);
        QFile reservation(journal);
        if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        {
            reject("Cannot reserve processing transaction journal: " + reservation.errorString());
        }
        journalReserved = true;
        if (!reservation.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        {
            reject("Cannot restrict processing journal reservation permissions");
        }
        reservation.close();
        auto log = stage(journal, bytes, true);
        if (!log->commit())
        {
            reject("Cannot persist processing transaction journal: " + log->errorString());
        }
        journalWritten = true;
        for (qsizetype index = 0; index < changes.size(); ++index)
        {
            const auto& change = changes[index];
            confined(change.path, root);
            if (state(change.path) != change.original)
            {
                reject("Document changed during processing commit: " + change.path);
            }
            if (change.replacement == change.original)
            {
                continue;
            }
            if (change.replacement)
            {
                if (!staged[size_t(index)]->commit())
                {
                    reject("Cannot commit " + change.path + ": " + staged[size_t(index)]->errorString());
                }
            }
            else if (change.original && !QFile::remove(change.path))
            {
                reject("Cannot remove processor document: " + change.path);
            }
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        QString message = QString::fromUtf8(exception.what());
        if (journalReserved && !journalWritten)
        {
            const auto      path = journalPath(workspace);
            const QFileInfo file(path);
            if (!file.isSymLink() && file.isFile() && file.size() == 0)
            {
                if (!QFile::remove(path))
                {
                    message += "; cannot remove empty journal reservation";
                }
            }
        }
        if (journalWritten)
        {
            QString rollback;
            if (!recover(workspace, &rollback))
            {
                message += "; rollback failed: " + rollback;
            }
        }
        if (error)
        {
            *error = message;
        }
        return false;
    }
}
bool WorkspaceFileTransaction::finish(const Path& workspace, QString* error)
{
    if (error)
    {
        error->clear();
    }
    const auto path = journalPath(workspace);
    if (QFileInfo(path).isSymLink() || !QFile::remove(path))
    {
        if (error)
        {
            *error = "Cannot remove completed processing journal: " + path;
        }
        return false;
    }
    return true;
}
bool WorkspaceFileTransaction::recover(const Path& workspace, QString* error)
{
    if (error)
    {
        error->clear();
    }
    try
    {
        const auto journal = journalPath(workspace);
        if (!QFileInfo::exists(journal) && !QFileInfo(journal).isSymLink())
        {
            return true;
        }
        if (QFileInfo(journal).isSymLink())
        {
            reject("Processing journal must not be a symbolic link");
        }
        const auto      root = workspaceFile(workspace);
        QJsonParseError parsing;
        const auto      document = QJsonDocument::fromJson(read(journal, MaximumJournal), &parsing);
        const auto      object   = document.object();
        const auto      records  = object.value("files").toArray();
        if (parsing.error != QJsonParseError::NoError || !document.isObject() || object.value("version").toInt() != 2
            || object.value("workspace").toString() != root || records.isEmpty() || records.size() > 4096)
        {
            reject("Invalid processing transaction journal");
        }
        QList<Change> changes;
        QSet<QString> paths;
        qint64        payload = 0;
        for (const auto& value : records)
        {
            if (!value.isObject())
            {
                reject("Invalid processing transaction record");
            }
            const auto record = value.toObject();
            const auto path   = record.value("path").toString();
            confined(path, root);
            if (paths.contains(path))
            {
                reject("Duplicate processing recovery document");
            }
            paths.insert(path);
            Change change {path, decode(record, "original"), decode(record, "replacement")};
            existingAnchor(change, root);
            payload += change.original ? change.original->size() : 0;
            payload += change.replacement ? change.replacement->size() : 0;
            if (payload > 20 * 1024 * 1024)
            {
                reject("Processing recovery payload exceeds bounds");
            }
            const auto current = state(path);
            if (current != change.original && current != change.replacement)
            {
                reject("Processing recovery stopped because a document was externally changed: " + path);
            }
            changes.append(std::move(change));
        }
        inventory(root, changes, true);
        std::vector<std::unique_ptr<QSaveFile>> staged;
        for (const auto& change : changes)
        {
            if (change.original)
            {
                if (!QDir().mkpath(QFileInfo(change.path).absolutePath()))
                {
                    reject("Cannot restore processor directory");
                }
                confined(change.path, root);
                staged.push_back(stage(change.path, *change.original));
            }
            else
            {
                staged.push_back(nullptr);
            }
        }
        for (qsizetype index = 0; index < changes.size(); ++index)
        {
            const auto& change = changes[index];
            confined(change.path, root);
            const auto current = state(change.path);
            if (current != change.original && current != change.replacement)
            {
                reject("Processing recovery encountered a concurrent document change: " + change.path);
            }
            if (change.original)
            {
                if (!staged[size_t(index)]->commit())
                {
                    reject("Cannot restore document: " + change.path);
                }
            }
            else if (current && !QFile::remove(change.path))
            {
                reject("Cannot remove newly created processor document during recovery: " + change.path);
            }
        }
        return finish(workspace, error);
    }
    catch (const std::exception& exception)
    {
        return failure(error, exception);
    }
}
