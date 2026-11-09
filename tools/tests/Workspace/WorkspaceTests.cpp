#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <Termaster/Core/SettingsSchema.h>
#include <Termaster/Core/Workspace.h>
#include <filesystem>
#include <memory>

namespace
{
bool writeDocument(const QString&    path,
                   const QByteArray& content,
                   const QByteArray& type    = "termaster.workspace",
                   int               version = 1)
{
    QByteArray bytes = "version: " + QByteArray::number(version) + "\ntype: " + type
                     + "\ndescription: Test document\ncontent:\n";
    for (const auto& line : content.split('\n'))
    {
        bytes += "  " + line + '\n';
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return file.readAll();
}
} // namespace

class WorkspaceTests : public QObject
{
    Q_OBJECT
private slots:
    void layeredSettings()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Settings      overrides(temporary.filePath("overrides.yaml"));
        QVERIFY(base.setValue("nested",
                              QVariantMap {
                                  {"a", 1},
                                  {"b", 2}
        }));
        QVERIFY(base.setValue("list", QVariantList {1, 2}));
        QVERIFY(base.setValue("null", "inherited"));
        QVERIFY(overrides.setValue("nested/a", 3));
        QVERIFY(overrides.setValue("list", QVariantList {4}));
        QVERIFY(overrides.setValue("null", {}));
        SettingsOverlay overlay(base.node(""), overrides.node(""));
        QCOMPARE(overlay.value("nested/a").toInt(), 3);
        QCOMPARE(overlay.value("nested/b").toInt(), 2);
        QCOMPARE(overlay.value("list").toList(), QVariantList {4});
        QVERIFY(overlay.contains("null"));
        QVERIFY(!overlay.value("null", 123).isValid());
        QCOMPARE(overlay.value("absent", 123).toInt(), 123);
        QVERIFY(overlay.remove("nested/a"));
        QCOMPARE(overlay.value("nested/a").toInt(), 1);
        auto scoped = overlay.node("nested");
        QVERIFY(scoped.setValue("a", 9));
        QCOMPARE(base.value("nested/a").toInt(), 1);
        QCOMPARE(overlay.values().value("nested").toMap().value("a").toInt(), 9);
        QVERIFY(!scoped.setValue("../escape", 1));
        QVERIFY(!overlay.node(QStringList(65, "x").join('/')).isValid());
        QVERIFY(base.setValue("scalar", 5));
        QVERIFY(overrides.setValue("scalar",
                                   QVariantMap {
                                       {"child", 8}
        }));
        QVERIFY(overlay.node("scalar").isValid());
        QCOMPARE(overlay.node("scalar").value("child").toInt(), 8);
        QVERIFY(overrides.setValue("nested", 42));
        QVERIFY(!overlay.contains("nested/b"));
        QVERIFY(!overlay.node("nested").isValid());
        QVERIFY(!overlay.node("nested/child").isValid());
    }

    void expiredViews()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        QVERIFY(base.setValue("inherited", 7));
        auto            overrides = std::make_unique<Settings>(temporary.filePath("overrides.yaml"));
        SettingsOverlay overlay(base.node(""), overrides->node(""));
        overrides.reset();
        QVERIFY(!overlay.isValid());
        QVERIFY(!overlay.contains("inherited"));
        QCOMPARE(overlay.value("inherited", 99).toInt(), 99);
        QVERIFY(!overlay.setValue("x", 1));
    }

    void createLoadAndSessions()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        QVERIFY(base.setValue("font/size", 12));
        const Path path(temporary.filePath(QString::fromUtf8("workspace-雪.yaml")));
        Workspace  workspace(path, base.node(""));
        QString    error;
        QVERIFY2(workspace.create(QString::fromUtf8("Рабочая область 雪"), &error), qPrintable(error));
        QVERIFY(!workspace.isDirty());
        QVERIFY(workspace.filePath().isAbsolute());
        QCOMPARE(workspace.settings().value("font/size").toInt(), 12);
        QVERIFY(workspace.settings().setValue("font/size", 14));
        QVERIFY(workspace.sessions().setValue("example",
                                              QVariantMap {
                                                  {"directory", "sessions/example"}
        }));
        QVERIFY(workspace.isDirty());
        QVERIFY(workspace.sync(&error));
        Workspace opened(path, base.node(""));
        QVERIFY2(opened.load(&error), qPrintable(error));
        QCOMPARE(opened.name(), workspace.name());
        QCOMPARE(opened.settings().value("font/size").toInt(), 14);
        QVERIFY(opened.sessions().contains("example"));
        QCOMPARE(base.value("font/size").toInt(), 12);
        QVERIFY(!opened.setName("   ", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!opened.setName(QString(257, 'x')));
    }

    void recentSessionsRoundTrip()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const Path    path(temporary.filePath("workspace.yaml"));
        Workspace     workspace(path, base.node(""));
        QVERIFY(workspace.create("Recent"));
        QVERIFY(workspace.recentSessions().isEmpty());
        QStringList expected;
        for (int index = 0; index < 12; ++index)
        {
            const auto id = QString("session-%1").arg(index);
            QVERIFY(workspace.sessions().setValue(id,
                                                  QVariantMap {
                                                      {"directory", "sessions/" + id}
            }));
            QVERIFY(workspace.rememberSession(id));
            expected.prepend(id);
        }
        expected = expected.first(10);
        QCOMPARE(workspace.recentSessions(), expected);
        QVERIFY(workspace.rememberSession("session-4"));
        expected.removeAll("session-4");
        expected.prepend("session-4");
        QCOMPARE(workspace.recentSessions(), expected);
        QVERIFY(workspace.sync());
        Workspace opened(path, base.node(""));
        QVERIFY(opened.load());
        QCOMPARE(opened.recentSessions(), expected);
        QVERIFY(!opened.isDirty());
        QVERIFY(!opened.rememberSession("absent"));
        QVERIFY(!opened.rememberSession(""));
        QVERIFY(!opened.rememberSession(QString(257, 'x')));
        QVERIFY(!opened.rememberSession(QString("a") + QChar::Null));
        QVERIFY(opened.sessions().setValue("scalar", 42));
        QVERIFY(!opened.rememberSession("scalar"));
        QVERIFY(opened.sessions().setValue("nested/child", QVariantMap {}));
        QVERIFY(!opened.rememberSession("nested/child"));
        QCOMPARE(opened.recentSessions(), expected);
    }

    void staleRecentSessionsAreReadOnly()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const auto    path = temporary.filePath("workspace.yaml");
        QVERIFY(writeDocument(
            path,
            "name: Recent\nsettings: {}\nsessions: {live: {directory: sessions/live}}\nstate: {recentSessions: [missing, live], unknown: keep}"));
        Workspace workspace(Path(path), base.node(""));
        QVERIFY(workspace.load());
        QCOMPARE(workspace.recentSessions(), QStringList {"live"});
        QVERIFY(!workspace.isDirty());
        QVERIFY(workspace.setName("Saved"));
        QVERIFY(workspace.sync());
        Settings saved(path, Settings::Info {"termaster.workspace", "Termaster workspace"});
        QVERIFY(saved.load());
        QCOMPARE(saved.value("state/recentSessions").toList(), QVariantList({"missing", "live"}));
        QVERIFY(workspace.rememberSession("live"));
        QVERIFY(workspace.sync());
        QVERIFY(saved.load());
        QCOMPARE(saved.value("state/unknown").toString(), "keep");
    }

    void invalidRecentSessionsRetainOwner()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const auto    path = temporary.filePath("workspace.yaml");
        Workspace     workspace(Path(path), base.node(""));
        QVERIFY(workspace.create("Original"));
        QVERIFY(workspace.sessions().setValue("live",
                                              QVariantMap {
                                                  {"directory", "sessions/live"}
        }));
        QVERIFY(workspace.rememberSession("live"));
        const QList<QByteArray> invalid {"state: []",
                                         "state: {recentSessions: {}}",
                                         "state: {recentSessions: [live, live]}",
                                         "state: {recentSessions: [42]}",
                                         "state: {recentSessions: ['']}",
                                         "state: {recentSessions: ['   ']}",
                                         "state: {recentSessions: [a, b, c, d, e, f, g, h, i, j, k]}",
                                         "state: {recentSessions: ['" + QByteArray(257, 'x') + "']}",
                                         "state: {recentSessions: [\"a\\0b\"]}"};
        for (const auto& state : invalid)
        {
            QVERIFY(writeDocument(path, "name: Replacement\nsettings: {}\nsessions: {}\n" + state));
            QString error;
            QVERIFY2(!workspace.load(&error), state.constData());
            QVERIFY(!error.isEmpty());
            QCOMPARE(workspace.name(), "Original");
            QCOMPARE(workspace.recentSessions(), QStringList {"live"});
            QVERIFY(workspace.isDirty());
        }
    }

    void catalogRegistrationAndConfinement()
    {
        QTemporaryDir temporary;
        QTemporaryDir outside;
        Settings      base(temporary.filePath("base.yaml"));
        const Path    file(temporary.filePath("workspace.yaml"));
        Workspace     workspace(file, base.node(""));
        QVERIFY(workspace.create("Catalog"));
        QCOMPARE(workspace.sessionsDirectory().toString(), file.parent().join("sessions").toString());
        const auto directory = workspace.sessionsDirectory().join("custom-folder");
        QVERIFY(directory.createDirectories());
        QVERIFY(workspace.registerSession("valid-ID_1", directory));
        QVERIFY(workspace.sessions().setValue("valid-ID_1/unknown", "preserved"));
        QVERIFY(workspace.registerSession("valid-ID_1", directory));
        QCOMPARE(workspace.sessions().value("valid-ID_1/unknown").toString(), "preserved");
        QCOMPARE(workspace.sessionIds(), QStringList {"valid-ID_1"});
        QVERIFY(workspace.rememberSession("valid-ID_1"));
        QVERIFY(!workspace.registerSession("../escape", directory));
        QVERIFY(!workspace.registerSession(".hidden", directory));
        QVERIFY(!workspace.registerSession("outside", Path(outside.path())));
        QVERIFY(!workspace.registerSession("root", workspace.sessionsDirectory()));
        QVERIFY(workspace.sync());
        Workspace opened(file, base.node(""));
        QVERIFY(opened.load());
        QCOMPARE(opened.sessionDirectory("valid-ID_1")->toString(), directory.toString());
        QVERIFY(opened.unregisterSession("valid-ID_1"));
        QVERIFY(opened.recentSessions().isEmpty());
        QVERIFY(opened.sessionIds().isEmpty());
        QVERIFY(directory.isDirectory());
        QVERIFY(opened.sync());
        QVERIFY(workspace.load());
        QVERIFY(workspace.sessionIds().isEmpty());
    }

    void catalogRejectsSymlinkEscape()
    {
        QTemporaryDir temporary;
        QTemporaryDir outside;
        Settings      base(temporary.filePath("base.yaml"));
        Workspace     workspace(Path(temporary.filePath("workspace.yaml")), base.node(""));
        QVERIFY(workspace.create("Confinement"));
        QVERIFY(workspace.sessionsDirectory().createDirectories());
        const auto      link = workspace.sessionsDirectory().join("escape");
        std::error_code code;
        std::filesystem::create_directory_symlink(std::filesystem::path(outside.path().toStdWString()),
                                                  std::filesystem::path(link.toString().toStdWString()),
                                                  code);
        if (code)
        {
            QSKIP("Directory symlink creation is unavailable on this host");
        }
        QVERIFY(!workspace.registerSession("escape", link));
        QVERIFY(workspace.sessions().setValue("escape",
                                              QVariantMap {
                                                  {"directory", "sessions/escape/missing"}
        }));
        QString error;
        QVERIFY(!workspace.sessionDirectory("escape", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!workspace.sync());
    }

    void invalidCatalogRetainsOwner()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const auto    path = temporary.filePath("workspace.yaml");
        Workspace     workspace(Path(path), base.node(""));
        QVERIFY(workspace.create("Original"));
        QVERIFY(workspace.setName("Pending"));
        const QList<QByteArray> invalid {"{old: {name: Embedded}}",
                                         "{bad: 42}",
                                         "{bad: {directory: 42}}",
                                         "{bad: {directory: ../outside}}",
                                         "{bad: {directory: sessions/../outside}}",
                                         "{bad: {directory: /absolute}}",
                                         "{bad: {directory: sessions}}",
                                         "{'.bad': {directory: sessions/good}}"};
        for (const auto& catalog : invalid)
        {
            QVERIFY(writeDocument(path, "name: Replacement\nsettings: {}\nsessions: " + catalog));
            QString error;
            QVERIFY2(!workspace.load(&error), catalog.constData());
            QVERIFY(!error.isEmpty());
            QCOMPARE(workspace.name(), "Pending");
            QVERIFY(workspace.isDirty());
        }
        QVERIFY(
            writeDocument(path,
                          "name: Missing allowed\nsettings: {}\nsessions: {missing: {directory: sessions/missing}}"));
        QVERIFY(workspace.load());
        QVERIFY(workspace.sessionDirectory("missing").has_value());
        QVERIFY(!workspace.sessionDirectory("missing")->exists());
    }

    void preserveExistingFile()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const auto    path = temporary.filePath("workspace.yaml");
        QVERIFY(writeDocument(path, "name: Original\nsettings: {}\nsessions: {}\nunknown: keep"));
        const auto original = readFile(path);
        Workspace  workspace(Path(path), base.node(""));
        QString    error;
        QVERIFY(!workspace.create("Replacement", &error));
        QCOMPARE(readFile(path), original);
        QVERIFY(workspace.load(&error));
        QVERIFY(workspace.setName("Changed"));
        QVERIFY(workspace.sync(&error));
        QVERIFY(readFile(path).contains("unknown"));
        QVERIFY(readFile(path).contains("keep"));
    }

    void invalidDocuments()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const auto    path = temporary.filePath("workspace.yaml");
        Workspace     workspace(Path(path), base.node(""));
        QString       error;
        QVERIFY(!workspace.load(&error));
        QVERIFY(!error.isEmpty());
        const QList<QByteArray> invalid {"name: Name\nsettings: []\nsessions: {}",
                                         "name: 42\nsettings: {}\nsessions: {}",
                                         "name: Name\nsettings: {}\nsessions: []",
                                         "name: Name\nsettings: {}\nsessions: {}\ndirectories: {plugins: false}",
                                         "name: Name\nsettings: {}\nsessions: {}\ndirectories: {scripts: ''}"};
        for (const auto& content : invalid)
        {
            QVERIFY(writeDocument(path, content));
            QVERIFY2(!workspace.load(&error), content.constData());
            QVERIFY(!error.isEmpty());
        }
        const QByteArray valid = "name: Name\nsettings: {}\nsessions: {}";
        QVERIFY(writeDocument(path, valid, "termaster.settings"));
        QVERIFY(!workspace.load(&error));
        QVERIFY(writeDocument(path, valid, "termaster.workspace", 2));
        QVERIFY(!workspace.load(&error));
        QVERIFY(writeDocument(path, valid));
        QVERIFY(workspace.load(&error));
        QVERIFY(workspace.setName("Unsaved"));
        QVERIFY(writeDocument(path, invalid.first()));
        QVERIFY(!workspace.load(&error));
        QCOMPARE(workspace.name(), "Unsaved");
        QVERIFY(workspace.isDirty());
    }

    void directoriesAreMetadata()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const auto    path = temporary.filePath("workspace.yaml");
        QVERIFY(writeDocument(
            path,
            "name: Metadata\nsettings: {}\nsessions: {}\ndirectories:\n  plugins: future/plugins\n  scripts: future/scripts"));
        Workspace workspace(Path(path), base.node(""));
        QVERIFY(workspace.load());
        QVERIFY(workspace.pluginDirectory().has_value());
        QVERIFY(workspace.scriptDirectory().has_value());
        QCOMPARE(workspace.pluginDirectory()->toString(),
                 Path(temporary.filePath("future/plugins")).absolute().toString());
        QVERIFY(!workspace.pluginDirectory()->exists());
        QVERIFY(!workspace.scriptDirectory()->exists());
        QVERIFY(workspace.setName("Renamed"));
        QVERIFY(workspace.sync());
        QVERIFY(!Path(temporary.filePath("future")).exists());
    }

    void processorDefinitionsRoundTripAndNotifications()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const Path    path(temporary.filePath("workspace.yaml"));
        Workspace     workspace(path, base.node(""));
        QSignalSpy    changed(&workspace, &Workspace::processorsChanged);
        QVERIFY(workspace.create("Processing"));
        QCOMPARE(changed.count(), 1);
        const QVariantMap node {
            {              "name",                      "processor"},
            {              "type",   "unavailable.vendor.processor"},
            {     "configuration", QVariantMap {{"mode", "future"}}},
            {"futureNodeMetadata",  QVariantMap {{"color", "blue"}}}
        };
        const QVariantMap definition {
            {                      "id",                                     "shared"},
            {                    "name",               QString::fromUtf8("Shared 雪")},
            {                    "kind",                                   "lineflow"},
            {                  "source", QVariantMap {{"nodes", QVariantList {node}}}},
            {"futureDefinitionMetadata",          QVariantMap {{"owner", "external"}}}
        };
        const QVariantMap code {
            {            "id",                                    "script"                              },
            {          "name",                                                              "Saved code"},
            {          "kind",                                                      "unavailable.python"},
            {        "source",               QVariantMap {{"code", "return input"}, {"futureSource", 7}}},
            {      "defaults",                                               QVariantMap {{"limit", 12}}},
            {"settingsSchema",
             "version: 1\ntype: termaster.settings-schema\ndescription: Code\ncontent: {type: object}\n"},
            {        "editor",                                      QVariantMap {{"language", "python"}}},
            {    "references",                                       QVariantList {"missing.definition"}},
            {       "unknown",                                             QVariantList {"retained", 42}}
        };
        QString error;
        QVERIFY2(workspace.setProcessorDefinition("shared", definition, &error), qPrintable(error));
        QCOMPARE(changed.count(), 2);
        QVERIFY(workspace.setProcessorDefinition("shared", definition));
        QCOMPARE(changed.count(), 2);
        // Unknown providers and missing dependencies are persisted without execution.
        QVERIFY2(workspace.setProcessorDefinition("script", code, &error), qPrintable(error));
        QCOMPARE(changed.count(), 3);
        QVERIFY(workspace.sync());
        QCOMPARE(changed.count(), 3);
        Workspace  opened(path, base.node(""));
        QSignalSpy loaded(&opened, &Workspace::processorsChanged);
        QVERIFY2(opened.load(&error), qPrintable(error));
        QCOMPARE(loaded.count(), 1);
        QCOMPARE(opened.processorDefinitions().value("shared").toMap(), definition);
        QCOMPARE(opened.processorDefinitions().value("script").toMap(), code);
        QVERIFY(opened.load());
        QCOMPARE(loaded.count(), 1);
        auto modified = code;
        modified.insert("name", "Edited code");
        QVERIFY(opened.processorDefinitions().setValue("script", modified));
        QCOMPARE(loaded.count(), 1);
        QVERIFY(opened.sync());
        QCOMPARE(loaded.count(), 2);
        QVERIFY(workspace.load());
        QCOMPARE(changed.count(), 4);
        QCOMPARE(workspace.processorDefinitions().value("script").toMap(), modified);
    }

    void invalidProcessorDefinitionEditsAreAtomic()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const Path    path(temporary.filePath("workspace.yaml"));
        Workspace     workspace(path, base.node(""));
        QVERIFY(workspace.create("Processing"));
        const QVariantMap leaf {
            {    "id",                                                             "leaf"                      },
            {  "name",                                                                                   "Leaf"},
            {  "kind",                                                                               "lineflow"},
            {"source",
             QVariantMap {
             {"nodes", QVariantList {QVariantMap {{"name", "processor"}, {"type", "unavailable.native"}}}}}    }
        };
        const QVariantMap parent {
            {    "id",                                                        "parent"                      },
            {  "name",                                                                              "Parent"},
            {  "kind",                                                                            "lineflow"},
            {"source",
             QVariantMap {{"nodes", QVariantList {QVariantMap {{"name", "nested"}, {"reference", "leaf"}}}}}}
        };
        QVERIFY(workspace.setProcessorDefinition("leaf", leaf));
        QVERIFY(workspace.setProcessorDefinition("parent", parent));
        QVERIFY(workspace.sync());
        const auto original  = workspace.processorDefinitions().values();
        const auto persisted = readFile(path.toString());
        QSignalSpy changed(&workspace, &Workspace::processorsChanged);
        auto       invalid = leaf;
        invalid.insert("source",
                       QVariantMap {
                           {"nodes",
                            QVariantList {
                                QVariantMap {{"name", "ambiguous"}, {"type", "native"}, {"reference", "parent"}}}}
        });
        QString error;
        QVERIFY(!workspace.setProcessorDefinition("leaf", invalid, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(workspace.processorDefinitions().values(), original);
        QVERIFY(!workspace.isDirty());
        invalid.insert("source",
                       QVariantMap {
                           {"nodes", QVariantList {QVariantMap {{"name", "cycle"}, {"reference", "parent"}}}}
        });
        QVERIFY(!workspace.setProcessorDefinition("leaf", invalid, &error));
        QVERIFY(error.contains("cycle"));
        QCOMPARE(workspace.processorDefinitions().values(), original);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(readFile(path.toString()), persisted);
        // Removing a dependency preserves the parent's unavailable reference.
        QVERIFY(workspace.removeProcessorDefinition("leaf", &error));
        QCOMPARE(workspace.processorDefinitions().value("parent").toMap(), parent);
        QCOMPARE(changed.count(), 1);
        QVERIFY(workspace.sync());
        Workspace opened(path, base.node(""));
        QVERIFY(opened.load());
        QCOMPARE(opened.processorDefinitions().value("parent").toMap(), parent);
        QVERIFY(!workspace.removeProcessorDefinition("leaf", &error));
        QCOMPARE(changed.count(), 1);
        QVERIFY(workspace.removeProcessorDefinition("parent"));
        QVERIFY(workspace.processorDefinitions().values().isEmpty());
    }

    void invalidProcessorDefinitionsRetainLoadedOwnerAndFile()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const Path    path(temporary.filePath("workspace.yaml"));
        Workspace     workspace(path, base.node(""));
        QVERIFY(workspace.create("Original"));
        const QVariantMap definition {
            {    "id",                                   "good"},
            {  "name",                                   "Good"},
            {  "kind",                               "lineflow"},
            {"source", QVariantMap {{"nodes", QVariantList {}}}}
        };
        QVERIFY(workspace.setProcessorDefinition("good", definition));
        QVERIFY(workspace.sync());
        const auto original  = workspace.processorDefinitions().values();
        const auto persisted = readFile(path.toString());
        QSignalSpy changed(&workspace, &Workspace::processorsChanged);
        QVERIFY(workspace.processorDefinitions().setValue("good/source/nodes", "invalid"));
        QString error;
        QVERIFY(!workspace.sync(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(changed.count(), 0);
        QCOMPARE(readFile(path.toString()), persisted);
        QVERIFY(workspace.isDirty());
        QVERIFY(workspace.load());
        QCOMPARE(workspace.processorDefinitions().values(), original);
        QCOMPARE(changed.count(), 0);
        QVERIFY(workspace.setName("Pending"));
        QVERIFY(writeDocument(path.toString(),
                              "name: Replacement\nsettings: {}\nsessions: {}\nprocessorDefinitions:\n"
                              "  bad: {id: bad, name: Bad, kind: lineflow, source: {nodes: invalid}}"));
        QVERIFY(!workspace.load(&error));
        QCOMPARE(workspace.name(), "Pending");
        QCOMPARE(workspace.processorDefinitions().values(), original);
        QCOMPARE(changed.count(), 0);
        QVERIFY(workspace.isDirty());
    }

    void processorPublicationValidatorRejectsAtomically()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const Path    path(temporary.filePath("workspace.yaml"));
        Workspace     workspace(path, base.node(""));
        QVERIFY(workspace.create("Original"));
        const auto persisted = readFile(path.toString());
        workspace.setProcessorValidator([](const QVariantMap& definitions, QString* error) {
            if (definitions.contains("reserved"))
            {
                if (error)
                {
                    *error = "Reserved catalog identity";
                }
                return false;
            }
            return true;
        });
        QSignalSpy        changed(&workspace, &Workspace::processorsChanged);
        const QVariantMap definition {
            {    "id",        "reserved"},
            {  "name",        "Reserved"},
            {  "kind", "future.provider"},
            {"source",    QVariantMap {}}
        };
        QString error;
        QVERIFY(!workspace.setProcessorDefinition("reserved", definition, &error));
        QCOMPARE(error, "Reserved catalog identity");
        QVERIFY(workspace.processorDefinitions().values().isEmpty());
        QVERIFY(workspace.processorDefinitions().setValue("reserved", definition));
        QVERIFY(!workspace.sync(&error));
        QCOMPARE(readFile(path.toString()), persisted);
        QCOMPARE(changed.count(), 0);
        QVERIFY(workspace.load());
        QVERIFY(workspace.processorDefinitions().values().isEmpty());
    }

    void externalProcessorFilesPreserveLiteralKeysAndEnvelope()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Workspace     workspace(Path(temporary.filePath("workspace.yaml")), base.node(""));
        QVERIFY(workspace.create("External processors"));
        QString error;
        for (const auto& id : QStringList {"CON", "con", "A", "a"})
        {
            const QVariantMap definition {
                {          "id",                                                           id},
                {        "name",                                                "Saved " + id},
                {        "kind",                                            "future.provider"},
                {      "source", QVariantMap {{"code", "return input; unique-source-marker"}}},
                {"unknown/path",                          QVariantMap {{"literal/value", 23}}}
            };
            QVERIFY2(workspace.setProcessorDefinition(id, definition, &error), qPrintable(error));
        }
        QVERIFY2(workspace.sync(&error), qPrintable(error));
        QCOMPARE(workspace.processorDiskFiles().size(), 4);
        const auto main = readFile(workspace.filePath().toString());
        QVERIFY(!main.contains("processorDefinitions"));
        QVERIFY(!main.contains("unique-source-marker"));
        const auto files = workspace.processorDiskFiles();
        QString    path;
        const auto filename = QString::fromLatin1(
                                  QCryptographicHash::hash(QByteArray("CON"), QCryptographicHash::Sha256).toHex())
                            + ".yaml";
        for (auto it = files.cbegin(); it != files.cend(); ++it)
        {
            if (QFileInfo(it.key()).fileName() == filename)
            {
                path = it.key();
            }
        }
        QVERIFY(!path.isEmpty());
        QFile envelope(path);
        QVERIFY(envelope.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(envelope.write("futureEnvelope: retained\n" + files.value(path)) > 0);
        envelope.close();
        QVERIFY2(workspace.load(&error), qPrintable(error));
        auto definition = workspace.processorDefinitions().value("CON").toMap();
        definition.insert("name", "Renamed display title");
        QVERIFY(workspace.setProcessorDefinition("CON", definition));
        QVERIFY2(workspace.sync(&error), qPrintable(error));
        Settings stored(path, Settings::Info {"termaster.processor-definition", "Fixture"});
        QVERIFY(stored.load(&error));
        QCOMPARE(stored.values().value("unknown/path").toMap().value("literal/value").toInt(), 23);
        QVERIFY(!stored.values().contains("unknown"));
        QCOMPARE(SettingsSchema::parseValue(readFile(path)).toMap().value("futureEnvelope").toString(),
                 QString("retained"));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
        {
            if (it.key() != path)
            {
                QCOMPARE(readFile(it.key()), it.value());
            }
        }
        const auto note = workspace.processorsDirectory().join("notes.txt");
        QFile      unrelated(note.toString());
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        unrelated.write("keep");
        unrelated.close();
        QVERIFY(workspace.removeProcessorDefinition("CON"));
        QVERIFY(workspace.sync(&error));
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(readFile(note.toString()), QByteArray("keep"));
        Workspace opened(workspace.filePath(), base.node(""));
        QVERIFY2(opened.load(&error), qPrintable(error));
        QCOMPARE(opened.processorDefinitions().values(), workspace.processorDefinitions().values());
        QVERIFY(!opened.processorDefinitions().contains("CON"));
        QVERIFY(opened.processorDefinitions().contains("con"));
    }

    void externalProcessorCorruptionAndInventoryChangesRetainOwner()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Workspace     workspace(Path(temporary.filePath("workspace.yaml")), base.node(""));
        QVERIFY(workspace.create("Original"));
        const QVariantMap definition {
            {    "id",                                   "root"},
            {  "name",                                   "Root"},
            {  "kind",                               "lineflow"},
            {"source", QVariantMap {{"nodes", QVariantList {}}}}
        };
        QVERIFY(workspace.setProcessorDefinition("root", definition));
        QString error;
        QVERIFY2(workspace.sync(&error), qPrintable(error));
        const auto files    = workspace.processorDiskFiles();
        const auto path     = files.firstKey();
        const auto original = workspace.processorDefinitions().values();
        QVERIFY(workspace.setName("Pending"));
        QSignalSpy changed(&workspace, &Workspace::processorsChanged);
        QVERIFY(writeDocument(path,
                              "id: root\nname: Broken\nkind: lineflow\nsource: {nodes: invalid}",
                              "termaster.processor-definition"));
        QVERIFY(!workspace.load(&error));
        QCOMPARE(workspace.name(), QString("Pending"));
        QCOMPARE(workspace.processorDefinitions().values(), original);
        QCOMPARE(changed.count(), 0);
        QVERIFY(workspace.isDirty());
        QFile restore(path);
        QVERIFY(restore.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(restore.write(files.value(path)), files.value(path).size());
        restore.close();
        const auto extra = workspace.processorsDirectory().join(
            QString::fromLatin1(QCryptographicHash::hash(QByteArray("extra"), QCryptographicHash::Sha256).toHex())
            + ".yaml");
        QVERIFY(writeDocument(extra.toString(),
                              "id: extra\nname: Extra\nkind: future.provider\nsource: {}",
                              "termaster.processor-definition"));
        const auto before = readFile(workspace.filePath().toString());
        QVERIFY(!workspace.sync(&error));
        QVERIFY2(error.contains("externally changed"), qPrintable(error));
        QCOMPARE(readFile(workspace.filePath().toString()), before);
        QVERIFY(QFileInfo::exists(extra.toString()));
        QCOMPARE(workspace.processorDefinitions().values(), original);
        QVERIFY(workspace.load(&error));
        QVERIFY(workspace.processorDefinitions().contains("extra"));
        const auto wrong = workspace.processorsDirectory().join("not-a-hash.yaml");
        QVERIFY(writeDocument(wrong.toString(),
                              "id: wrong\nname: Wrong\nkind: future.provider\nsource: {}",
                              "termaster.processor-definition"));
        QVERIFY(!workspace.load(&error));
        QVERIFY(error.contains("filename"));
    }

    void obsoleteInlineProcessorDefinitionsAreRejected()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const Path    path(temporary.filePath("workspace.yaml"));
        QVERIFY(writeDocument(path.toString(), "name: Obsolete\nsettings: {}\nsessions: {}\nprocessorDefinitions: {}"));
        Workspace workspace(path, base.node(""));
        QString   error;
        QVERIFY(!workspace.load(&error));
        QVERIFY(error.contains("Inline processorDefinitions"));
        QVERIFY(!QFileInfo::exists(path.toString() + ".d"));
    }

    void workspaceViewLifetime()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        QVERIFY(base.setValue("inherited", 7));
        auto workspace = std::make_unique<Workspace>(Path(temporary.filePath("workspace.yaml")), base.node(""));
        QVERIFY(workspace->create("Initial"));
        auto overlay  = workspace->settings();
        auto sessions = workspace->sessions();
        QVERIFY(workspace->load());
        QVERIFY(!overlay.isValid());
        QVERIFY(!sessions.isValid());
        auto current = workspace->settings();
        QCOMPARE(current.value("inherited").toInt(), 7);
        workspace.reset();
        QVERIFY(!current.isValid());
        QVERIFY(!current.contains("inherited"));
        QVERIFY(!current.setValue("inherited", 9));
    }

    void failedSyncRetainsDirtyState()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        const auto    path = temporary.filePath("workspace.yaml");
        Workspace     workspace(Path(path), base.node(""));
        QVERIFY(workspace.create("Initial"));
        QVERIFY(workspace.setName("Pending"));
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path));
        QString error;
        QVERIFY(!workspace.sync(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(workspace.isDirty());
        QCOMPARE(workspace.name(), "Pending");
    }
};

QTEST_GUILESS_MAIN(WorkspaceTests)
#include "WorkspaceTests.moc"
