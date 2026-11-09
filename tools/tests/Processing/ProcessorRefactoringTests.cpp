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
