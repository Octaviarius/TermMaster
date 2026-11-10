#include <QTest>
#include <Termaster/Core/Processing/ProcessorRefactoring.h>

namespace
{
QVariantMap node(const QString& name, const QString& type)
{
    return {
        {"name", name},
        {"type", type}
    };
}
QVariantMap definition(const QString& id, const QVariantList& nodes)
{
    return {
        {     "id",                             id},
        {   "name",                             id},
        {   "kind",                     "lineflow"},
        { "source", QVariantMap {{"nodes", nodes}}},
        {"unknown",                             42}
    };
}
QVariantMap object(const QString& root, const QVariantMap& overrides)
{
    return {
        {  "module",                                                       "unavailable.module"                    },
        { "unknown",                                                                                     "retained"},
        {"settings",
         QVariantMap {{"processing", QVariantMap {{"rx", QVariantMap {{"type", root}, {"overrides", overrides}}}}}}}
    };
}
QVariantMap overrides(const QVariantMap& objects, const QString& id)
{
    return objects.value(id)
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
} // namespace

class ProcessorRefactoringTests final : public QObject
{
    Q_OBJECT
private slots:
    void nestedRepeatedOccurrencesAndSequentialRenames()
    {
        const QVariantMap old {
            {"leaf", definition("leaf",            {node("decode", "missing.native")})},
            {"root", definition("root", {node("left", "leaf"), node("right", "leaf")})}
        };
        auto final = old;
        final.insert("leaf", definition("leaf", {node(QString::fromUtf8("解析"), "missing.native")}));
        final.insert("root",
                     definition("root",
                                {node("right", "leaf"), node("newLeft", "leaf"), node("inserted", "missing.native")}));
        const QVariantMap first {
            { "left/decode", QVariantMap {{"value", 1}}},
            {"right/decode", QVariantMap {{"value", 2}}}
        };
        const QVariantMap objects {
            {"closed-session-terminal", object("root",                                    first)},
            {         "other-terminal", object("leaf", {{"decode", QVariantMap {{"value", 3}}}})}
        };
        const auto result = ProcessorRefactoring::transform(old,
                                                            final,
                                                            {
                                                                {"leaf",       "decode",            "intermediate"},
                                                                {"leaf", "intermediate", QString::fromUtf8("解析")},
                                                                {"root",         "left",                 "newLeft"}
        },
                                                            objects);
        QVERIFY(result);
        const auto changed = overrides(result->sessionObjects, "closed-session-terminal");
        QCOMPARE(changed.value(QString::fromUtf8("newLeft/解析")), first.value("left/decode"));
        QCOMPARE(changed.value(QString::fromUtf8("right/解析")), first.value("right/decode"));
        QVERIFY(overrides(result->sessionObjects, "other-terminal").contains(QString::fromUtf8("解析")));
        QCOMPARE(objects.value("closed-session-terminal").toMap().value("unknown").toString(), "retained");
        QCOMPARE(result->definitions, final);
        QCOMPARE(overrides(objects, "closed-session-terminal"), first);
    }

    void repeatedValidRenameHistory()
    {
        const QVariantMap before {
            {"root", definition("root", {node("a", "missing.native")})}
        };
        const QVariantMap after {
            {"root", definition("root", {node("b", "missing.native")})}
        };
        const QVariantMap objects {
            {"terminal", object("root", {{"a", QVariantMap {{"value", 1}}}})}
        };
        const auto result = ProcessorRefactoring::transform(before,
                                                            after,
                                                            {
                                                                {"root", "a", "b"},
                                                                {"root", "b", "a"},
                                                                {"root", "a", "b"}
        },
                                                            objects);
        QVERIFY(result);
        QVERIFY(overrides(result->sessionObjects, "terminal").contains("b"));
    }

    void defaultsAndNestedNodeConfiguration()
    {
        auto root = definition("root", {node("branch", "leaf")});
        root.insert("defaults",
                    QVariantMap {
                        {"overrides", QVariantMap {{"branch/old", QVariantMap {{"value", 7}}}}},
                        {  "unknown",                                                     true}
        });
        auto parentNode = node("nested", "root");
        parentNode.insert("configuration",
                          QVariantMap {
                              {"overrides", QVariantMap {{"branch/old", QVariantMap {{"value", 9}}}}},
                              {   "opaque",                                                   "kept"}
        });
        const QVariantMap old {
            {"leaf", definition("leaf", {node("old", "missing.native")})},
            {"root", root},
            {"parent", definition("parent", {parentNode})}
        };
        auto final = old;
        final.insert("leaf", definition("leaf", {node("new", "missing.native")}));
        const auto result = ProcessorRefactoring::transform(old,
                                                            final,
                                                            {
                                                                {"leaf", "old", "new"}
        },
                                                            {});
        QVERIFY(result);
        const auto defaults = result->definitions.value("root").toMap().value("defaults").toMap();
        QVERIFY(defaults.value("overrides").toMap().contains("branch/new"));
        QCOMPARE(defaults.value("unknown").toBool(), true);
        const auto config = result->definitions.value("parent")
                                .toMap()
                                .value("source")
                                .toMap()
                                .value("nodes")
                                .toList()[0]
                                .toMap()
                                .value("configuration")
                                .toMap();
        QVERIFY(config.value("overrides").toMap().contains("branch/new"));
        QCOMPARE(config.value("opaque").toString(), "kept");
    }

    void embeddedDiagramsRefactorDormantProfilesInIsolatedTerminalScopes()
    {
        const QVariantMap before {
            {"shared", definition("shared", {node("old", "missing.native")})}
        };
        const QVariantMap after {
            {"shared", definition("shared", {node("new", "missing.native")})}
        };
        const auto terminal = [](const QString& prefix, bool selected) {
            QVariantMap diagrams;
            QVariantMap processing {
                {   "mode",              "bypass"},
                {"unknown", "processing metadata"}
            };
            for (const auto& direction : {QStringLiteral("rx"), QStringLiteral("tx"), QStringLiteral("duplex")})
            {
                const auto id     = "terminal.inplace." + direction;
                auto       nested = node(prefix, "shared");
                nested.insert("configuration",
                              QVariantMap {
                                  {"overrides", QVariantMap {{"old", QVariantMap {{"value", 7}}}}},
                                  {  "unknown",                                   "node metadata"}
                });
                auto root   = definition(id, {nested, node("old", "missing.native")});
                auto source = root.value("source").toMap();
                source.insert("channels", direction == "duplex" ? 2 : 1);
                root.insert("source", source);
                root.insert("defaults",
                            QVariantMap {
                                {"overrides", QVariantMap {{prefix + "/old", QVariantMap {{"value", 8}}}}}
                });
                diagrams.insert(direction,
                                QVariantMap {
                                    {id, root}
                });
                processing.insert(direction,
                                  QVariantMap {
                                      {         "type",selected ? "@inplace" : "missing.other"                                                       },
                                      {    "overrides",
                                       QVariantMap {
                                       {selected ? prefix + "/old" : "opaque/path", QVariantMap {{"value", 9}}}}    },
                                      {"configuration",
                                       QVariantMap {{"overrides",
                                       QVariantMap {{selected ? prefix + "/old" : "opaque/path",
                                       QVariantMap {{"value", 10}}}}}}                                              },
                                      {      "unknown",                                         "selection metadata"}
                });
            }
            processing.insert("inplace", diagrams);
            return QVariantMap {
                {"settings", QVariantMap {{"processing", processing}}},
                { "unknown",                        "object metadata"}
            };
        };
        const QVariantMap objects {
            {    "first",   terminal("one",  true)},
            {   "second",   terminal("two",  true)},
            {  "dormant", terminal("three", false)},
            {"unrelated", object("missing",    {})}
        };
        QString    error;
        const auto result = ProcessorRefactoring::transform(before,
                                                            after,
                                                            {
                                                                {"shared", "old", "new"}
        },
                                                            objects,
                                                            &error);
        QVERIFY2(result, qPrintable(error));
        QCOMPARE(result->definitions, after);
        QCOMPARE(result->sessionObjects.value("unrelated"), objects.value("unrelated"));
        for (const auto& terminalId : {QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("dormant")})
        {
            const auto prefix  = terminalId == "first"  ? QStringLiteral("one") :
                                 terminalId == "second" ? QStringLiteral("two") :
                                                          QStringLiteral("three");
            const auto changed = result->sessionObjects.value(terminalId)
                                     .toMap()
                                     .value("settings")
                                     .toMap()
                                     .value("processing")
                                     .toMap();
            QCOMPARE(changed.value("mode").toString(), QString("bypass"));
            QCOMPARE(changed.value("unknown").toString(), QString("processing metadata"));
            for (const auto& direction : {QStringLiteral("rx"), QStringLiteral("tx"), QStringLiteral("duplex")})
            {
                const auto id   = "terminal.inplace." + direction;
                const auto root = changed.value("inplace").toMap().value(direction).toMap().value(id).toMap();
                QVERIFY(root.value("defaults").toMap().value("overrides").toMap().contains(prefix + "/new"));
                const auto children = root.value("source").toMap().value("nodes").toList();
                QCOMPARE(children[0].toMap().value("name").toString(), prefix);
                QCOMPARE(children[1].toMap().value("name").toString(), QString("old"));
                const auto configuration = children[0].toMap().value("configuration").toMap();
                QVERIFY(configuration.value("overrides").toMap().contains("new"));
                QCOMPARE(configuration.value("unknown").toString(), QString("node metadata"));
                const auto selection = changed.value(direction).toMap();
                QCOMPARE(selection.value("unknown").toString(), QString("selection metadata"));
                if (terminalId == "dormant")
                {
                    const auto original = objects.value(terminalId)
                                              .toMap()
                                              .value("settings")
                                              .toMap()
                                              .value("processing")
                                              .toMap();
                    QCOMPARE(selection, original.value(direction).toMap());
                }
                else
                {
                    QCOMPARE(selection.value("type").toString(), QString("@inplace"));
                    QVERIFY(selection.value("overrides").toMap().contains(prefix + "/new"));
                    QVERIFY(
                        selection.value("configuration").toMap().value("overrides").toMap().contains(prefix + "/new"));
                    QVERIFY(!selection.value("overrides").toMap().contains(prefix + "/old"));
                }
            }
        }
        QCOMPARE(objects.value("first"), terminal("one", true));
    }

    void embeddedDiagramRefactoringRejectsCollisionsAndStaleAddresses()
    {
        const auto        id = QStringLiteral("terminal.inplace.rx");
        const QVariantMap shared {
            {"shared", definition("shared", {node("old", "missing.native")})}
        };
        auto              terminal   = object("@inplace",
                                              {
                                                  {"branch/stale", QVariantMap {}}
        });
        auto              settings   = terminal.value("settings").toMap();
        auto              processing = settings.value("processing").toMap();
        const QVariantMap local {
            {id, definition(id, {node("branch", "shared")})}
        };
        processing.insert("inplace",
                          QVariantMap {
                              {"rx", local}
        });
        settings.insert("processing", processing);
        terminal.insert("settings", settings);
        QString error;
        QVERIFY(!ProcessorRefactoring::transform(shared,
                                                 shared,
                                                 {
        },
                                                 {{"terminal", terminal}},
                                                 &error));
        QVERIFY(error.contains("Stale"));
        auto collision = shared;
        collision.insert(id, local.value(id));
        QVERIFY(!ProcessorRefactoring::transform(collision,
                                                 collision,
                                                 {
        },
                                                 {{"terminal", terminal}},
                                                 &error));
        QVERIFY(error.contains("Duplicate"));
    }

    void collisionsStalePathsAndAtomicInputs()
    {
        const QVariantMap old {
            {"root", definition("root", {node("a", "missing.native"), node("b", "missing.native")})}
        };
        const QVariantMap objects {
            {"terminal", object("root", {{"a", QVariantMap {{"unknown", 12}}}})}
        };
        QString error;
        QVERIFY(!ProcessorRefactoring::transform(old,
                                                 old,
                                                 {
                                                     {"root", "a", "b"}
        },
                                                 objects,
                                                 &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!ProcessorRefactoring::transform(old,
                                                 old,
                                                 {
                                                     {"root", "a", "c"},
                                                     {"root", "a", "c"}
        },
                                                 objects,
                                                 &error));
        QVERIFY(!ProcessorRefactoring::transform(old,
                                                 old,
                                                 {
                                                     {"root", "a", "slash/name"}
        },
                                                 objects,
                                                 &error));
        auto removed = old;
        removed.insert("root", definition("root", {node("b", "missing.native")}));
        QVERIFY(!ProcessorRefactoring::transform(old, removed, {}, objects, &error));
        QVERIFY(error.contains("removed"));
        QCOMPARE(overrides(objects, "terminal").value("a").toMap().value("unknown").toInt(), 12);
        QVERIFY(ProcessorRefactoring::transform(old, removed, {}, {}));
        const QVariantMap stale {
            {"terminal", object("root", {{"stale", QVariantMap {}}})}
        };
        QVERIFY(!ProcessorRefactoring::transform(old, old, {}, stale, &error));
    }

    void unavailableRootsAndBounds()
    {
        const QVariantMap objects {
            {"terminal", object("missing.root", {{"opaque/path", QVariantMap {{"unknown", 1}}}})}
        };
        const auto result = ProcessorRefactoring::transform({}, {}, {}, objects);
        QVERIFY(result);
        QCOMPARE(result->sessionObjects, objects);
        const QVariantMap old {
            {"root", definition("root", {node("unknown", "missing.provider")})}
        };
        const QVariantMap nested {
            {"terminal", object("root", {{"unknown/opaque", QVariantMap {}}})}
        };
        QVERIFY(ProcessorRefactoring::transform(old, old, {}, nested));
        QList<ProcessorRefactoring::Rename> excessive;
        for (int index = 0; index < 4097; ++index)
        {
            excessive.append({"root", "a", "b"});
        }
        QVERIFY(!ProcessorRefactoring::transform({}, {}, excessive, {}));
        QVariantMap deep;
        for (int index = 0; index < 66; ++index)
        {
            deep = {
                {"nested", deep}
            };
        }
        QVERIFY(!ProcessorRefactoring::transform({}, {}, {}, deep));
        QVERIFY(!ProcessorRefactoring::transform(
            {
        },
            {},
            {},
            {{"large", QString(4 * 1024 * 1024, 'x')}}));
    }
};
QTEST_GUILESS_MAIN(ProcessorRefactoringTests)
#include "ProcessorRefactoringTests.moc"
