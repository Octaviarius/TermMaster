#include "Processing/WorkspaceProcessingEdit.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/Session.h>
#include <Termaster/Core/Workspace.h>
#include <Termaster/Core/WorkspaceFileTransaction.h>

namespace
{
QByteArray read(const Path& path)
{
    QFile file(path.toString());
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray {};
}
bool write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
QVariantMap definition(const QString& id, const QVariantList& nodes)
{
    return {
        {    "id",                             id},
        {  "name",                             id},
        {  "kind",                     "lineflow"},
        {"source", QVariantMap {{"nodes", nodes}}}
    };
}
QVariantMap objects()
{
    return {
        {"terminal",
         QVariantMap {{"type", "terminal"},
                      {"module", "unavailable.module"},
                      {"opaque", "retained"},
                      {"settings",
                       QVariantMap {{"processing",
                                     QVariantMap {{"rx",
                                                   QVariantMap {{"type", "root"},
                                                                {"overrides",
                                                                 QVariantMap {{"branch/old",
                                                                               QVariantMap {{"value", 7}}}}}}}}}}}}}
    };
}
QVariantMap overrideMap(Session& session)
{
    return session.objects()
        .value("terminal")
        .toMap()
        .value("settings")
        .toMap()
        .value("processing")
        .toMap()
        .value("rx")
        .toMap()
        .value("overrides")
        .toMap();
}
struct Fixture
{
    QTemporaryDir     temporary;
    Settings          base;
    Workspace         workspace;
    Session           active;
    Session           closed;
    ProcessorRegistry registry;
    QVariantMap       baseline;
    QString           error;
    Fixture() :
        base(temporary.filePath("base.yaml")), workspace(Path(temporary.filePath("workspace.yaml")), base.node("")),
        active(Path(temporary.filePath("sessions/active")), SettingsOverlay(base.node(""), base.node(""))),
        closed(Path(temporary.filePath("sessions/closed")), SettingsOverlay(base.node(""), base.node("")))
    {
    }
    bool initialize()
    {
        if (!workspace.create("Original", &error) || !QDir().mkpath(temporary.filePath("sessions")))
        {
            return false;
        }
        baseline = {
            {"leaf", definition("leaf", {QVariantMap {{"name", "old"}, {"type", "unavailable.native"}}})},
            {"root", definition("root",            {QVariantMap {{"name", "branch"}, {"type", "leaf"}}})}
        };
        for (auto it = baseline.cbegin(); it != baseline.cend(); ++it)
        {
            if (!workspace.setProcessorDefinition(it.key(), it.value().toMap(), &error))
            {
                return false;
            }
        }
        if (!active.create("active", "Active", &error) || !closed.create("closed", "Closed", &error)
            || !active.objects().applyValues(objects(), {}, &error)
            || !closed.objects().applyValues(objects(), {}, &error) || !active.sync(&error) || !closed.sync(&error)
            || !workspace.registerSession("active", active.directory(), &error)
            || !workspace.registerSession("closed", closed.directory(), &error))
        {
            return false;
        }
        return workspace.sync(&error);
    }
    QVariantMap draft() const
    {
        auto result = baseline;
        result.insert("leaf",
                      definition("leaf",
                                 {
                                     QVariantMap {{"name", "new"}, {"type", "unavailable.native"}}
        }));
        return result;
    }
    QString journal() const
    {
        return workspace.filePath().toString() + ".processing-journal.json";
    }
};
QList<WorkspaceFileTransaction::Change> processorChanges(Workspace&                       workspace,
                                                         const Workspace::ProcessorFiles& replacement)
{
    const auto                              main = read(workspace.filePath());
    QList<WorkspaceFileTransaction::Change> result {
        {QFileInfo(workspace.filePath().toString()).canonicalFilePath(), main, main}
    };
    const auto    original = workspace.processorDiskFiles();
    QSet<QString> paths;
    for (const auto& path : original.keys())
    {
        paths.insert(path);
    }
    for (const auto& path : replacement.keys())
    {
        paths.insert(path);
    }
    for (const auto& path : paths)
    {
        result.append({path,
                       original.contains(path) ? std::optional<QByteArray>(original.value(path)) : std::nullopt,
                       replacement.contains(path) ? std::optional<QByteArray>(replacement.value(path)) : std::nullopt});
    }
    return result;
}
bool journal(Fixture&          fixture,
             const QByteArray& workspaceOriginal,
             const QByteArray& workspaceReplacement,
             const QByteArray& sessionOriginal,
             const QByteArray& sessionReplacement)
{
    QJsonArray entries;
    entries.append(QJsonObject {
        {             "path", QFileInfo(fixture.workspace.filePath().toString()).canonicalFilePath()},
        {   "originalExists",                                                                   true},
        {"replacementExists",                                                                   true},
        {         "original",                      QString::fromLatin1(workspaceOriginal.toBase64())},
        {      "replacement",                   QString::fromLatin1(workspaceReplacement.toBase64())}
    });
    entries.append(QJsonObject {
        {             "path", QFileInfo(fixture.closed.filePath().toString()).canonicalFilePath()},
        {   "originalExists",                                                                true},
        {"replacementExists",                                                                true},
        {         "original",                     QString::fromLatin1(sessionOriginal.toBase64())},
        {      "replacement",                  QString::fromLatin1(sessionReplacement.toBase64())}
    });
    const auto processorAfter = fixture.workspace.prepareProcessorFiles(fixture.draft());
    if (!processorAfter)
    {
        return false;
    }
    const auto    processorBefore = fixture.workspace.processorDiskFiles();
    QSet<QString> processorPaths;
    for (const auto& path : processorBefore.keys())
    {
        processorPaths.insert(path);
    }
    for (const auto& path : processorAfter->keys())
    {
        processorPaths.insert(path);
    }
    for (const auto& path : processorPaths)
    {
        entries.append(QJsonObject {
            {             "path",                                                        path},
            {   "originalExists",                              processorBefore.contains(path)},
            {"replacementExists",                              processorAfter->contains(path)},
            {         "original", QString::fromLatin1(processorBefore.value(path).toBase64())},
            {      "replacement", QString::fromLatin1(processorAfter->value(path).toBase64())}
        });
    }
    return write(fixture.journal(),
                 QJsonDocument(
                     QJsonObject {
                         {  "version",                                                                      2},
                         {"workspace", QFileInfo(fixture.workspace.filePath().toString()).canonicalFilePath()},
                         {    "files",                                                                entries}
    })
                     .toJson());
}
} // namespace

class LineflowPersistenceTests final : public QObject
{
    Q_OBJECT
private slots:
    void activeAndClosedSessionsPreservePendingValuesAndBorrowedViews()
    {
        Fixture fixture;
        QVERIFY2(fixture.initialize(), qPrintable(fixture.error));
        auto original = read(fixture.active.filePath());
        original.replace("content:", "envelopeUnknown: preserved\ncontent:");
        QVERIFY(write(fixture.active.filePath().toString(), original));
        QVERIFY(fixture.active.load());
        auto borrowed = fixture.active.objects();
        auto overlay  = fixture.active.settings();
        QVERIFY(overlay.setValue("pending", 42));
        QVERIFY(fixture.workspace.setName("Pending workspace"));
        auto concurrent = fixture.baseline.value("root").toMap();
        concurrent.insert("concurrentUnknown", 99);
        QVERIFY(fixture.workspace.setProcessorDefinition("root", concurrent));
        QSignalSpy changed(&fixture.workspace, &Workspace::processorsChanged);
        QVERIFY2(WorkspaceProcessingEdit::apply(fixture.workspace,
                                                &fixture.active,
                                                fixture.registry,
                                                fixture.baseline,
                                                fixture.draft(),
                                                {
                                                    {"leaf", "old", "new"}
        },
                                                &fixture.error),
                 qPrintable(fixture.error));
        QCOMPARE(changed.count(), 1);
        QVERIFY(borrowed.isValid());
        QVERIFY(overrideMap(fixture.active).contains("branch/new"));
        QVERIFY(borrowed.value("terminal").toMap().value("opaque").toString() == "retained");
        QCOMPARE(overlay.value("pending").toInt(), 42);
        QVERIFY(!fixture.active.isDirty());
        QVERIFY(!fixture.workspace.isDirty());
        QCOMPARE(fixture.workspace.name(), "Pending workspace");
        QCOMPARE(fixture.workspace.processorDefinitions().value("root").toMap().value("concurrentUnknown").toInt(), 99);
        QVERIFY(read(fixture.active.filePath()).contains("envelopeUnknown"));
        Session reopened(fixture.closed.directory(), fixture.workspace.settings());
        QVERIFY(reopened.load());
        QVERIFY(overrideMap(reopened).contains("branch/new"));
        QVERIFY(!QFileInfo::exists(fixture.journal()));
    }

    void concurrentEditedFieldRejectsBeforeWrites()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        auto draft      = fixture.draft();
        auto concurrent = fixture.baseline.value("leaf").toMap();
        concurrent.insert("source",
                          QVariantMap {
                              {"nodes", QVariantList {}}
        });
        QVERIFY(fixture.workspace.setProcessorDefinition("leaf", concurrent));
        const auto before        = read(fixture.workspace.filePath());
        const auto sessionBefore = read(fixture.active.filePath());
        QVERIFY(!WorkspaceProcessingEdit::apply(fixture.workspace,
                                                &fixture.active,
                                                fixture.registry,
                                                fixture.baseline,
                                                draft,
                                                {
                                                    {"leaf", "old", "new"}
        },
                                                &fixture.error));
        QVERIFY(fixture.error.contains("conflict"));
        QCOMPARE(read(fixture.workspace.filePath()), before);
        QCOMPARE(read(fixture.active.filePath()), sessionBefore);
        QCOMPARE(fixture.workspace.processorDefinitions().value("leaf").toMap(), concurrent);
        QVERIFY(fixture.workspace.isDirty());
    }

    void brokenClosedSessionRejectsBeforeWrites_data()
    {
        QTest::addColumn<bool>("missing");
        QTest::newRow("missing") << true;
        QTest::newRow("malformed") << false;
    }
    void brokenClosedSessionRejectsBeforeWrites()
    {
        QFETCH(bool, missing);
        Fixture fixture;
        QVERIFY(fixture.initialize());
        const auto before        = read(fixture.workspace.filePath());
        const auto sessionBefore = read(fixture.active.filePath());
        if (missing)
        {
            QVERIFY(QFile::remove(fixture.closed.filePath().toString()));
        }
        else
        {
            QVERIFY(write(fixture.closed.filePath().toString(), "malformed: ["));
        }
        QVERIFY(!WorkspaceProcessingEdit::apply(fixture.workspace,
                                                &fixture.active,
                                                fixture.registry,
                                                fixture.baseline,
                                                fixture.draft(),
                                                {
                                                    {"leaf", "old", "new"}
        },
                                                &fixture.error));
        QVERIFY(!fixture.error.isEmpty());
        QCOMPARE(read(fixture.workspace.filePath()), before);
        QCOMPARE(read(fixture.active.filePath()), sessionBefore);
        QCOMPARE(fixture.workspace.processorDefinitions().values(), fixture.baseline);
        QVERIFY(!QFileInfo::exists(fixture.journal()));
    }

    void interruptedJournalRollsBackOnlyRecognizedBytes()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        const auto workspaceBefore = read(fixture.workspace.filePath());
        const auto sessionBefore   = read(fixture.closed.filePath());
        const auto workspaceAfter  = fixture.workspace.prepareProcessorDefinitions(fixture.draft());
        const auto sessionAfter    = fixture.closed.prepareObjects(objects());
        QVERIFY(workspaceAfter);
        QVERIFY(sessionAfter);
        QVERIFY(journal(fixture, workspaceBefore, *workspaceAfter, sessionBefore, *sessionAfter));
        QVERIFY(write(fixture.workspace.filePath().toString(), *workspaceAfter));
        const bool recovered = WorkspaceProcessingEdit::recover(fixture.workspace.filePath(), &fixture.error);
        QVERIFY2(recovered, qPrintable(fixture.error));
        QCOMPARE(read(fixture.workspace.filePath()), workspaceBefore);
        QCOMPARE(read(fixture.closed.filePath()), sessionBefore);
        QVERIFY(!QFileInfo::exists(fixture.journal()));
        QVERIFY(journal(fixture, workspaceBefore, *workspaceAfter, sessionBefore, *sessionAfter));
        QVERIFY(write(fixture.workspace.filePath().toString(), *workspaceAfter));
        QVERIFY(write(fixture.closed.filePath().toString(), "external edit"));
        QVERIFY(!WorkspaceProcessingEdit::recover(fixture.workspace.filePath(), &fixture.error));
        QVERIFY(fixture.error.contains("externally changed"));
        QCOMPARE(read(fixture.workspace.filePath()), *workspaceAfter);
        QCOMPARE(read(fixture.closed.filePath()), QByteArray("external edit"));
        QVERIFY(QFileInfo::exists(fixture.journal()));
    }

    void externalEditBeforeApply_data()
    {
        QTest::addColumn<bool>("workspaceFile");
        QTest::newRow("workspace") << true;
        QTest::newRow("active-session") << false;
    }
    void externalEditBeforeApply()
    {
        QFETCH(bool, workspaceFile);
        Fixture fixture;
        QVERIFY(fixture.initialize());
        QVERIFY(fixture.active.settings().setValue("pending", 51));
        QVERIFY(fixture.workspace.setName("Pending workspace"));
        const auto selected  = workspaceFile ? fixture.workspace.filePath() : fixture.active.filePath();
        auto       external  = read(selected);
        external            += "\n# external edit before Apply\n";
        QVERIFY(write(selected.toString(), external));
        const auto workspaceBefore = read(fixture.workspace.filePath());
        const auto sessionBefore   = read(fixture.active.filePath());
        QVERIFY(!WorkspaceProcessingEdit::apply(fixture.workspace,
                                                &fixture.active,
                                                fixture.registry,
                                                fixture.baseline,
                                                fixture.draft(),
                                                {
                                                    {"leaf", "old", "new"}
        },
                                                &fixture.error));
        QVERIFY(fixture.error.contains("externally changed"));
        QCOMPARE(read(fixture.workspace.filePath()), workspaceBefore);
        QCOMPARE(read(fixture.active.filePath()), sessionBefore);
        QCOMPARE(fixture.workspace.name(), "Pending workspace");
        QCOMPARE(fixture.active.settings().value("pending").toInt(), 51);
        QVERIFY(fixture.workspace.isDirty());
        QVERIFY(fixture.active.isDirty());
        QVERIFY(!QFileInfo::exists(fixture.journal()));
    }

    void cacheAdoptionFailureRestoresDirtySnapshotAndDiskBaseline()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        QVERIFY(fixture.active.settings().setValue("pending", 61));
        const auto workspaceBefore = read(fixture.workspace.filePath());
        const auto activeBefore    = read(fixture.active.filePath());
        QString    processorPath;
        const auto preparedFiles = fixture.workspace.prepareProcessorFiles(fixture.draft());
        QVERIFY(preparedFiles);
        const auto diskFiles = fixture.workspace.processorDiskFiles();
        for (auto it = diskFiles.cbegin(); it != diskFiles.cend(); ++it)
        {
            if (preparedFiles->value(it.key()) != it.value())
            {
                processorPath = it.key();
            }
        }
        QVERIFY(!processorPath.isEmpty());
        const auto processorBefore = read(Path(processorPath));
        fixture.workspace.setProcessorValidator([&](const QVariantMap&, QString* error) {
            // Simulate a validator becoming unavailable after commit, before adoption.
            if (read(Path(processorPath)) != processorBefore)
            {
                if (error)
                {
                    *error = "Synthetic adoption failure";
                }
                return false;
            }
            return true;
        });
        auto borrowed = fixture.active.objects();
        QVERIFY(!WorkspaceProcessingEdit::apply(fixture.workspace,
                                                &fixture.active,
                                                fixture.registry,
                                                fixture.baseline,
                                                fixture.draft(),
                                                {
                                                    {"leaf", "old", "new"}
        },
                                                &fixture.error));
        QVERIFY2(fixture.error.contains("Synthetic adoption failure"), qPrintable(fixture.error));
        QVERIFY2(!fixture.error.contains("rollback failed"), qPrintable(fixture.error));
        QCOMPARE(read(fixture.workspace.filePath()), workspaceBefore);
        QCOMPARE(read(fixture.active.filePath()), activeBefore);
        QVERIFY(borrowed.isValid());
        QVERIFY(overrideMap(fixture.active).contains("branch/old"));
        QCOMPARE(fixture.active.settings().value("pending").toInt(), 61);
        QVERIFY(fixture.active.isDirty());
        QVERIFY(fixture.active.diskUnchanged());
        QVERIFY(fixture.workspace.diskUnchanged());
        QVERIFY(!QFileInfo::exists(fixture.journal()));
    }

    void excessiveSessionCatalogRejectsBeforeLoads()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        // Build a bounded YAML fixture directly: repeated QVariant/YAML map insertion is quadratic.
        QByteArray document = "version: 1\ntype: termaster.workspace\ndescription: Large catalog fixture\n"
                              "content:\n  name: Catalog budget\n  settings: {}\n  sessions:\n";
        document.reserve(300000);
        for (int index = 0; index < 4097; ++index)
        {
            document += "    entry" + QByteArray::number(index) + ": {directory: sessions/active}\n";
        }
        QVERIFY(write(fixture.workspace.filePath().toString(), document));
        QVERIFY2(fixture.workspace.load(&fixture.error), qPrintable(fixture.error));
        const auto workspaceBefore = read(fixture.workspace.filePath());
        const auto activeBefore    = read(fixture.active.filePath());
        QVERIFY(!WorkspaceProcessingEdit::apply(fixture.workspace,
                                                &fixture.active,
                                                fixture.registry,
                                                fixture.baseline,
                                                fixture.draft(),
                                                {
                                                    {"leaf", "old", "new"}
        },
                                                &fixture.error));
        QVERIFY2(fixture.error.contains("4096 inspected"), qPrintable(fixture.error));
        QCOMPARE(read(fixture.workspace.filePath()), workspaceBefore);
        QCOMPARE(read(fixture.active.filePath()), activeBefore);
        QVERIFY(!QFileInfo::exists(fixture.journal()));
    }

    void journalRejectsForeignAndDuplicateTargets()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        const auto workspaceBefore = read(fixture.workspace.filePath());
        const auto sessionBefore   = read(fixture.closed.filePath());
        const auto workspaceAfter  = fixture.workspace.prepareProcessorDefinitions(fixture.draft());
        const auto sessionAfter    = fixture.closed.prepareObjects(objects());
        QVERIFY(workspaceAfter && sessionAfter);
        QVERIFY(journal(fixture, workspaceBefore, *workspaceAfter, sessionBefore, *sessionAfter));
        auto       document      = QJsonDocument::fromJson(read(Path(fixture.journal()))).object();
        const auto originalFiles = document.value("files").toArray();
        const Path foreign(fixture.workspace.filePath().parent().join("outside.yaml"));
        QVERIFY(write(foreign.toString(), "untouched"));
        auto foreignRecord = originalFiles[1].toObject();
        foreignRecord.insert("path", QFileInfo(foreign.toString()).canonicalFilePath());
        foreignRecord.insert("original", QString::fromLatin1(QByteArray("untouched").toBase64()));
        foreignRecord.insert("replacement", QString::fromLatin1(QByteArray("changed").toBase64()));
        for (const bool duplicate : {false, true})
        {
            auto records = originalFiles;
            records[1]   = duplicate ? originalFiles[0] : QJsonValue(foreignRecord);
            document.insert("files", records);
            QVERIFY(write(fixture.journal(), QJsonDocument(document).toJson()));
            QVERIFY(!WorkspaceProcessingEdit::recover(fixture.workspace.filePath(), &fixture.error));
            QVERIFY2(fixture.error.contains("escapes") || fixture.error.contains("Duplicate"),
                     qPrintable(fixture.error));
            QCOMPARE(read(fixture.workspace.filePath()), workspaceBefore);
            QCOMPARE(read(fixture.closed.filePath()), sessionBefore);
            QCOMPARE(read(foreign), QByteArray("untouched"));
            QVERIFY(QFileInfo::exists(fixture.journal()));
        }
    }

    void externalDefinitionCreateDeleteAdoptsBorrowedCache()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        auto       borrowed      = fixture.workspace.processorDefinitions();
        const auto originalFiles = fixture.workspace.processorDiskFiles();
        auto       draft         = fixture.baseline;
        draft.remove("leaf");
        draft.insert("new", definition("new", {}));
        const auto sessionBefore = read(fixture.active.filePath());
        QVERIFY2(WorkspaceProcessingEdit::apply(fixture.workspace,
                                                &fixture.active,
                                                fixture.registry,
                                                fixture.baseline,
                                                draft,
                                                {},
                                                &fixture.error),
                 qPrintable(fixture.error));
        QVERIFY(borrowed.isValid());
        QCOMPARE(borrowed.values(), draft);
        QCOMPARE(read(fixture.active.filePath()), sessionBefore);
        const auto currentFiles = fixture.workspace.processorDiskFiles();
        QCOMPARE(currentFiles.size(), 2);
        int removed = 0;
        for (const auto& path : originalFiles.keys())
        {
            if (!currentFiles.contains(path))
            {
                ++removed;
                QVERIFY(!QFileInfo::exists(path));
            }
        }
        QCOMPARE(removed, 1);
        QVERIFY(!read(fixture.workspace.filePath()).contains("processorDefinitions"));
        Workspace reopened(fixture.workspace.filePath(), fixture.base.node(""));
        QVERIFY2(reopened.load(&fixture.error), qPrintable(fixture.error));
        QCOMPARE(reopened.processorDefinitions().values(), draft);
    }

    void interruptedCreateDeleteRestoresExactInventory()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        const auto originals = fixture.workspace.processorDiskFiles();
        auto       draft     = fixture.baseline;
        draft.remove("leaf");
        draft.insert("new", definition("new", {}));
        const auto replacements = fixture.workspace.prepareProcessorFiles(draft, &fixture.error);
        QVERIFY(replacements);
        const auto changes = processorChanges(fixture.workspace, *replacements);
        const auto note    = fixture.workspace.processorsDirectory().join("notes.txt");
        QVERIFY(write(note.toString(), "keep"));
        QVERIFY2(WorkspaceFileTransaction::commit(fixture.workspace.filePath(), changes, &fixture.error),
                 qPrintable(fixture.error));
        QVERIFY(QFileInfo::exists(fixture.journal()));
        for (const auto& path : originals.keys())
        {
            if (!replacements->contains(path))
            {
                QVERIFY(!QFileInfo::exists(path));
            }
        }
        // Core load recovers the durable journal before publishing any definitions.
        QVERIFY2(fixture.workspace.load(&fixture.error), qPrintable(fixture.error));
        QCOMPARE(fixture.workspace.processorDefinitions().values(), fixture.baseline);
        QCOMPARE(fixture.workspace.processorDiskFiles(), originals);
        for (auto it = originals.cbegin(); it != originals.cend(); ++it)
        {
            QCOMPARE(read(Path(it.key())), it.value());
        }
        for (const auto& path : replacements->keys())
        {
            if (!originals.contains(path))
            {
                QVERIFY(!QFileInfo::exists(path));
            }
        }
        QCOMPARE(read(note), QByteArray("keep"));
        QVERIFY(!QFileInfo::exists(fixture.journal()));
    }

    void recoveryRejectsExtraProcessorFilesWithoutMutatingAnyTarget()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        const auto replacements = fixture.workspace.prepareProcessorFiles(fixture.draft());
        QVERIFY(replacements);
        QVERIFY(WorkspaceFileTransaction::commit(fixture.workspace.filePath(),
                                                 processorChanges(fixture.workspace, *replacements),
                                                 &fixture.error));
        const auto extra = fixture.workspace.processorsDirectory().join(
            QString::fromLatin1(QCryptographicHash::hash(QByteArray("external"), QCryptographicHash::Sha256).toHex())
            + ".yaml");
        QVERIFY(write(extra.toString(), "external document"));
        QVERIFY(!WorkspaceProcessingEdit::recover(fixture.workspace.filePath(), &fixture.error));
        QVERIFY2(fixture.error.contains("inventory"), qPrintable(fixture.error));
        QCOMPARE(read(extra), QByteArray("external document"));
        for (auto it = replacements->cbegin(); it != replacements->cend(); ++it)
        {
            QCOMPARE(read(Path(it.key())), it.value());
        }
        QVERIFY(QFileInfo::exists(fixture.journal()));
        QVERIFY(QFile::remove(extra.toString()));
        QVERIFY2(WorkspaceProcessingEdit::recover(fixture.workspace.filePath(), &fixture.error),
                 qPrintable(fixture.error));
    }

    void emptyFileStateIsDistinctFromAbsentFile()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        const auto path = QDir::fromNativeSeparators(
            fixture.workspace.processorsDirectory()
                .join(QString::fromLatin1(
                          QCryptographicHash::hash(QByteArray("empty"), QCryptographicHash::Sha256).toHex())
                      + ".yaml")
                .toString());
        QVERIFY(write(path, {}));
        auto changes = processorChanges(fixture.workspace, fixture.workspace.processorDiskFiles());
        changes.append({path, QByteArray {}, QByteArray("replacement")});
        QVERIFY2(WorkspaceFileTransaction::commit(fixture.workspace.filePath(), changes, &fixture.error),
                 qPrintable(fixture.error));
        QCOMPARE(read(Path(path)), QByteArray("replacement"));
        QVERIFY(WorkspaceProcessingEdit::recover(fixture.workspace.filePath(), &fixture.error));
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(QFileInfo(path).size(), qint64(0));
    }

    void journalWriteFailureRetainsOriginalData()
    {
        Fixture fixture;
        QVERIFY(fixture.initialize());
        const auto before        = read(fixture.workspace.filePath());
        const auto sessionBefore = read(fixture.active.filePath());
        // An occupied journal destination is never replaced or ignored.
        QVERIFY(QDir().mkdir(fixture.journal()));
        QVERIFY(!WorkspaceProcessingEdit::apply(fixture.workspace,
                                                &fixture.active,
                                                fixture.registry,
                                                fixture.baseline,
                                                fixture.draft(),
                                                {
                                                    {"leaf", "old", "new"}
        },
                                                &fixture.error));
        QCOMPARE(read(fixture.workspace.filePath()), before);
        QCOMPARE(read(fixture.active.filePath()), sessionBefore);
        QCOMPARE(fixture.workspace.processorDefinitions().values(), fixture.baseline);
    }
};
QTEST_GUILESS_MAIN(LineflowPersistenceTests)
#include "LineflowPersistenceTests.moc"
