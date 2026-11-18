#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/Session.h>
#include <Termaster/Core/Workspace.h>
#include <filesystem>
#include <limits>

namespace
{
QString layoutId(int index)
{
    return QString("00000000-0000-4000-8000-%1").arg(index, 12, 16, QLatin1Char('0'));
}
QVariantMap group(int index, const QVariantList& tabs = {})
{
    return {
        {"type",         "group"},
        {  "id", layoutId(index)},
        {"tabs",            tabs}
    };
}
QVariantMap split(const QVariantList& children)
{
    QVariantList sizes;
    for (qsizetype index = 0; index < children.size(); ++index)
    {
        sizes.append(100);
    }
    return {
        {       "type",      "split"},
        {"orientation", "horizontal"},
        {      "sizes",        sizes},
        {   "children",     children}
    };
}
QVariantMap terminalLayout(const QVariantMap& main)
{
    return {
        {   "main",            main},
        {"windows", QVariantList {}}
    };
}
} // namespace

class SessionTests : public QObject
{
    Q_OBJECT
private slots:
    void connectionSnapshotRequiresBoolean()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Session       session(Path(temporary.filePath("session")), SettingsOverlay(base.node(""), base.node("")));
        QString       error;
        QVERIFY(session.create("snapshot", "Snapshot", &error));
        const QVariantMap object {
            {     "type",      "fixture"},
            {   "module",    "Synthetic"},
            { "settings", QVariantMap {}},
            {"connected",           true}
        };
        QVERIFY(session.objects().setValue("terminal", object));
        QVERIFY(session.sync(&error));
        QVERIFY(session.load(&error));
        QCOMPARE(session.objects().value("terminal/connected").typeId(), QMetaType::Bool);
        for (const auto& invalid : QVariantList {QString("true"), 1, QVariantMap {}, QVariantList {}})
        {
            QVERIFY(session.objects().setValue("terminal/connected", invalid));
            QVERIFY(!session.sync(&error));
            QVERIFY(error.contains("boolean"));
        }
    }

    void terminalProcessingOverridesRemainPrivate()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Workspace     workspace(Path(temporary.filePath("workspace.yaml")), base.node(""));
        QVERIFY(workspace.create("Shared blocks"));
        const QVariantMap leaf {
            {    "id",                                                      "leaf-block"                      },
            {  "name",                                                                                  "Leaf"},
            {  "kind",                                                                              "lineflow"},
            {"source",
             QVariantMap {{"nodes",
             QVariantList {QVariantMap {{"name", "leaf"},
             {"type", "native.gate"},
             {"configuration", QVariantMap {{"label", "shared"}}}}}}}                                         }
        };
        const QVariantMap root {
            {    "id",                                                            "pipeline"                      },
            {  "name",                                                                                  "Pipeline"},
            {  "kind",                                                                                  "lineflow"},
            {"source",
             QVariantMap {{"channels", 2},
             {"nodes", QVariantList {QVariantMap {{"name", "branch"}, {"reference", "leaf-block"}}}}}             }
        };
        QVERIFY(workspace.setProcessorDefinition("leaf-block", leaf));
        QVERIFY(workspace.setProcessorDefinition("pipeline", root));
        QVERIFY(workspace.sync());
        const auto shared = workspace.processorDefinitions().values();
        Session    session(Path(temporary.filePath("session")), workspace.settings());
        QVERIFY(session.create("private-settings", "Private processor settings"));
        const QVariantMap firstOverrides {
            {"branch/leaf", QVariantMap {{"label", "first"}}}
        };
        const QVariantMap secondOverrides {
            {"branch/leaf", QVariantMap {{"label", "second"}}}
        };
        const auto terminal = [](const QVariantMap& overrides) {
            return QVariantMap {
                {    "type",                                              "terminal"                            },
                {  "module",                                                             "unknown.future.module"},
                {"settings",
                 QVariantMap {
                 {"processing",
                 QVariantMap {{"mode", "lineflow"},
                 {"lineflow", QVariantMap {{"type", "pipeline"}, {"overrides", overrides}}}}}}                  }
            };
        };
        // Write the whole map: occurrence paths contain '/' and are not Settings paths.
        QVERIFY(session.objects().setValue("first", terminal(firstOverrides)));
        QVERIFY(session.objects().setValue("second", terminal(secondOverrides)));
        QVERIFY(session.sync());
        Session loaded(session.directory(), workspace.settings());
        QVERIFY(loaded.load());
        const auto processing = [&loaded](const QString& object) {
            return loaded.objects().value(object).toMap().value("settings").toMap().value("processing").toMap();
        };
        const auto firstSelection  = processing("first").value("lineflow").toMap();
        const auto secondSelection = processing("second").value("lineflow").toMap();
        QCOMPARE(firstSelection.value("overrides").toMap(), firstOverrides);
        QCOMPARE(secondSelection.value("overrides").toMap(), secondOverrides);
        QVERIFY(firstSelection.value("overrides").toMap().contains("branch/leaf"));
        QVERIFY(!firstSelection.value("overrides").toMap().contains("branch"));

        ProcessorRegistry registry;
        QString           error;
        registerNativeProcessors(registry, "native");
        const auto definitions = ProcessorRegistry::parseDefinitions(shared, &error);
        QVERIFY2(definitions, qPrintable(error));
        QVERIFY2(registry.publishDefinitions("workspace", *definitions, &error), qPrintable(error));
        auto prepared = registry.prepare("pipeline", {}, Processor::Limits {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        auto first  = prepared->instantiate(firstSelection.value("overrides").toMap(), &error);
        auto second = prepared->instantiate(secondSelection.value("overrides").toMap(), &error);
        QVERIFY2(first, qPrintable(error));
        QVERIFY2(second, qPrintable(error));
        QCOMPARE(first->configuration().value("overrides").toMap(), firstOverrides);
        QCOMPARE(second->configuration().value("overrides").toMap(), secondOverrides);
        QCOMPARE(workspace.processorDefinitions().values(), shared);
        first.reset();
        second.reset();
        prepared.reset();
        QTRY_VERIFY(registry.canRemoveOwner("native"));
    }

    void layeredSettings()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Settings      workspace(temporary.filePath("workspace.yaml"));
        QVERIFY(base.setValue("font/size", 12));
        QVERIFY(base.setValue("font/family", "mono"));
        QVERIFY(workspace.setValue("font/size", 14));
        SettingsOverlay inherited(base.node(""), workspace.node(""));
        Session         session(Path(temporary.filePath("session")), inherited);
        QVERIFY(session.create("test-1", "Test"));
        auto font = session.settings().node("font");
        QCOMPARE(font.value("size").toInt(), 14);
        QVERIFY(font.setValue("size", 16));
        QCOMPARE(font.value("size").toInt(), 16);
        QVERIFY(font.remove("size"));
        QVERIFY(workspace.setValue("font/size", 18));
        QCOMPARE(font.value("size").toInt(), 18);
        QCOMPARE(font.value("family").toString(), QString("mono"));
        Settings        overrides(temporary.filePath("fourth.yaml"));
        SettingsOverlay fourth(inherited.node("font"), overrides.node(""));
        QCOMPARE(fourth.value("size").toInt(), 18);
        QVERIFY(fourth.setValue("size", 20));
        QCOMPARE(fourth.value("size").toInt(), 20);
    }

    void roundTripAndFailedValidation()
    {
        QTemporaryDir   temporary;
        Settings        base(temporary.filePath("base.yaml"));
        Settings        workspace(temporary.filePath("workspace.yaml"));
        SettingsOverlay inherited(base.node(""), workspace.node(""));
        const Path      directory(temporary.filePath("session"));
        Session         session(directory, inherited);
        QString         error;
        QVERIFY2(session.create("stable-id", "Saved session", &error), qPrintable(error));
        const QVariantMap object {
            {     "type",                   "terminal"},
            {   "module",      "unknown.future.module"},
            { "settings", QVariantMap {{"future", 42}}},
            {"extension",                         true}
        };
        QVERIFY(session.objects().setValue("terminal-1", object));
        const QVariantMap endpoint {
            {"object", "terminal-1"},
            {  "port",      "bytes"}
        };
        QVERIFY(session.links().setValue("loop",
                                         QVariantMap {
                                             {"source", endpoint},
                                             {"target", endpoint}
        }));
        QVERIFY(session.resources().setValue("asset", "assets/missing.bin"));
        QVERIFY(session.presentation().setValue("future",
                                                QVariantMap {
                                                    {"layout", 7}
        }));
        QVERIFY2(session.sync(&error), qPrintable(error));
        Session loaded(directory, inherited);
        QVERIFY2(loaded.load(&error), qPrintable(error));
        QCOMPARE(loaded.id(), QString("stable-id"));
        QCOMPARE(loaded.objects().value("terminal-1").toMap(), object);
        QVERIFY(loaded.resourcePath("asset", &error));
        auto borrowed = loaded.objects();
        QVERIFY(loaded.links().setValue("loop/source/object", "missing"));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(loaded.isDirty());
        QVERIFY(loaded.load(&error));
        QVERIFY(!borrowed.isValid());
        QVERIFY(loaded.resources().setValue("escape", "../outside"));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(!loaded.resourcePath("escape", &error));
        QVERIFY(loaded.load(&error));
        QVERIFY(loaded.resources().setValue("absolute", temporary.path()));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(loaded.load(&error));
        QVERIFY(loaded.resources().setValue("nul", QString("bad") + QChar::Null + "path"));
        QVERIFY(!loaded.resourcePath("nul", &error));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(loaded.load(&error));
        QVERIFY(loaded.links().setValue("loop/source/port", "  "));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(loaded.load(&error));
        QVERIFY(loaded.objects().setValue("bad id", object));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(loaded.load(&error));
        QVERIFY(loaded.setName("Pending"));
        QFile damaged(loaded.filePath().toString());
        QVERIFY(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(damaged.write("invalid: document") > 0);
        damaged.close();
        QVERIFY(!loaded.load(&error));
        QCOMPARE(loaded.name(), QString("Pending"));
        QVERIFY(loaded.isDirty());
    }

    void tabOrderRoundTripPreservesUnavailableObjectsAndPresentation()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Session       session(Path(temporary.filePath("session")), SettingsOverlay(base.node(""), base.node("")));
        QString       error;
        QVERIFY2(session.create("ordered", "Ordered", &error), qPrintable(error));
        const QVariantMap object {
            {    "type",           "terminal"},
            {  "module", "unavailable.plugin"},
            {"settings",       QVariantMap {}}
        };
        QVERIFY(session.objects().setValue("hidden-terminal", object));
        const QVariantMap extension {
            {"nested", QVariantList {1, "future"}}
        };
        QVERIFY(session.presentation().setValue("extension", extension));
        const QVariantList order {"hidden-terminal", "stale-terminal", "another-terminal"};
        QVERIFY(session.presentation().setValue("tabOrder", order));
        QVERIFY2(session.sync(&error), qPrintable(error));
        Session loaded(session.directory(), SettingsOverlay(base.node(""), base.node("")));
        QVERIFY2(loaded.load(&error), qPrintable(error));
        QCOMPARE(loaded.presentation().value("tabOrder").toList(), order);
        QCOMPARE(loaded.presentation().value("extension").toMap(), extension);
        QCOMPARE(loaded.objects().value("hidden-terminal").toMap(), object);
        QVERIFY(loaded.presentation().setValue("tabOrder", QStringList {}));
        QVERIFY2(loaded.sync(&error), qPrintable(error));
        QVERIFY2(loaded.load(&error), qPrintable(error));
        QVERIFY(loaded.presentation().value("tabOrder").toList().isEmpty());
        QCOMPARE(loaded.presentation().value("extension").toMap(), extension);
    }

    void invalidTabOrderRejectsCandidatesWithoutReplacingOwner()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Session       session(Path(temporary.filePath("session")), SettingsOverlay(base.node(""), base.node("")));
        QString       error;
        QVERIFY2(session.create("ordered", "Ordered", &error), qPrintable(error));
        const QVariantList order {"first", "unavailable"};
        QVERIFY(session.presentation().setValue("tabOrder", order));
        QVERIFY(session.presentation().setValue("extension", "preserved"));
        QVERIFY2(session.sync(&error), qPrintable(error));
        QFile disk(session.filePath().toString());
        QVERIFY(disk.open(QIODevice::ReadOnly));
        const auto baseline = disk.readAll();
        disk.close();
        const QVariantList invalidOrders {
            "first",
            QVariantMap {},
            QVariantList {"first", "first"},
            QVariantList {"bad id"},
            QVariantList {"../escape"},
            QVariantList {""},
            QVariantList {42},
            QVariantList {true},
            QVariantList {QString(129, 'a')},
            QVariantList {QString("bad") + QChar::Null}
        };
        for (const auto& invalid : invalidOrders)
        {
            Settings candidate(session.filePath(), Settings::Info {"termaster.session", "Candidate"});
            QVERIFY(candidate.replaceSerialized(baseline, &error));
            QVERIFY(candidate.setValue("presentation/tabOrder", invalid, &error));
            const auto bytes = candidate.serializedPatch({}, {}, &error);
            QVERIFY2(bytes, qPrintable(error));
            const auto borrowed = session.presentation();
            QVERIFY(!session.acceptPreparedDocument(*bytes, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(borrowed.isValid());
            QCOMPARE(session.presentation().value("tabOrder").toList(), order);
            QCOMPARE(session.presentation().value("extension").toString(), QString("preserved"));
            QVERIFY(!session.isDirty());

            QVERIFY(session.presentation().setValue("tabOrder", invalid));
            QVERIFY(!session.sync(&error));
            QVERIFY(session.isDirty());
            QVERIFY(disk.open(QIODevice::ReadOnly));
            QCOMPARE(disk.readAll(), baseline);
            disk.close();
            QVERIFY2(session.load(&error), qPrintable(error));
            QCOMPARE(session.presentation().value("tabOrder").toList(), order);

            QVERIFY(disk.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(disk.write(*bytes), bytes->size());
            disk.close();
            const auto live = session.presentation();
            QVERIFY(!session.load(&error));
            QVERIFY(live.isValid());
            QCOMPARE(session.presentation().value("tabOrder").toList(), order);
            QVERIFY(disk.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(disk.write(baseline), baseline.size());
            disk.close();
        }
    }

    void terminalLayoutRoundTripPreservesUnavailableObjectsAndExtensions()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Session       session(Path(temporary.filePath("session")), SettingsOverlay(base.node(""), base.node("")));
        QString       error;
        QVERIFY2(session.create("groups", "Groups", &error), qPrintable(error));
        const QVariantMap object {
            {    "type",           "terminal"},
            {  "module", "unavailable.plugin"},
            {"settings",       QVariantMap {}}
        };
        QVERIFY(session.objects().setValue("unavailable", object));
        auto first = group(1, {"unavailable", "stale-object"});
        first.insert("active", "stale-object");
        first.insert("toolbarVisible", false);
        first.insert("statusBarVisible", true);
        first.insert("futureGroup",
                     QVariantMap {
                         {"position", 7}
        });
        auto empty = group(2);
        empty.insert("active", "");
        auto root = split({first, empty});
        root.insert("orientation", "vertical");
        root.insert("sizes", QVariantList {0, std::numeric_limits<int>::max()});
        root.insert("futureSplit", "preserved");
        const QVariantMap window {
            {"id", layoutId(3)},
            {"geometry", QString::fromLatin1(QByteArray(16384, 'x').toBase64())},
            {"tree", group(4, {"secondary"})},
            {"futureWindow", true},
            {"number", 5}
        };
        auto layout = terminalLayout(root);
        layout.insert("windows", QVariantList {window});
        layout.insert("activeGroup", layoutId(4));
        layout.insert("futureLayout", QVariantList {"preserved"});
        const QVariantList order {"secondary", "unavailable", "dormant", "stale-object"};
        QVERIFY(session.presentation().setValue("terminalLayout", layout));
        QVERIFY(session.presentation().setValue("tabOrder", order));
        QVERIFY(session.presentation().setValue("future", "preserved"));
        QVERIFY2(session.sync(&error), qPrintable(error));

        Session loaded(session.directory(), SettingsOverlay(base.node(""), base.node("")));
        QVERIFY2(loaded.load(&error), qPrintable(error));
        QCOMPARE(loaded.presentation().value("terminalLayout").toMap(), layout);
        QCOMPARE(loaded.presentation().value("tabOrder").toList(), order);
        QCOMPARE(loaded.presentation().value("future").toString(), QString("preserved"));
        QCOMPARE(loaded.objects().value("unavailable").toMap(), object);

        const auto emptyLayout = terminalLayout(group(1));
        QVERIFY(loaded.presentation().setValue("terminalLayout", emptyLayout));
        QVERIFY2(loaded.sync(&error), qPrintable(error));
        QVERIFY2(loaded.load(&error), qPrintable(error));
        QCOMPARE(loaded.presentation().value("terminalLayout").toMap(), emptyLayout);
    }

    void invalidTerminalLayoutsRejectCandidatesWithoutReplacingOwner()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Session       session(Path(temporary.filePath("session")), SettingsOverlay(base.node(""), base.node("")));
        QString       error;
        QVERIFY2(session.create("groups", "Groups", &error), qPrintable(error));
        const auto layout = terminalLayout(group(1, {"unavailable"}));
        QVERIFY(session.presentation().setValue("terminalLayout", layout));
        QVERIFY(session.presentation().setValue("future", "preserved"));
        QVERIFY2(session.sync(&error), qPrintable(error));
        const auto presentation = session.presentation().values();
        QFile      disk(session.filePath().toString());
        QVERIFY(disk.open(QIODevice::ReadOnly));
        const auto baseline = disk.readAll();
        disk.close();

        QVariantList       invalidLayouts {"layout",
                                           QVariantList {},
                                           QVariantMap {},
                                           QVariantMap {{"main", group(1)}},
                                           QVariantMap {{"windows", QVariantList {}}}};
        const QVariantList invalidGroups {
            "group",
            QVariantList {},
            QVariantMap {},
            QVariantMap {{"type", 42}, {"id", layoutId(1)}, {"tabs", QVariantList {}}},
            QVariantMap {{"type", "leaf"}, {"id", layoutId(1)}, {"tabs", QVariantList {}}}
        };
        for (const auto& invalid : invalidGroups)
        {
            auto candidate = layout;
            candidate.insert("main", invalid);
            invalidLayouts.append(candidate);
        }
        for (const auto& id : QVariantList {42,
                                            "",
                                            "bad id",
                                            "../escape",
                                            "group-1",
                                            "00000000-0000-0000-0000-000000000000",
                                            "{00000000-0000-4000-8000-000000000001}"})
        {
            auto invalid = group(1);
            invalid.insert("id", id);
            invalidLayouts.append(terminalLayout(invalid));
        }
        for (const auto& tabs : QVariantList {
                 "terminal",
                 QVariantMap {},
                 QVariantList {"first", "first"},
                 QVariantList {"bad id"},
                 QVariantList {"../escape"},
                 QVariantList {""},
                 QVariantList {42},
                 QVariantList {true},
                 QVariantList {QString(129, 'a')},
                 QVariantList {QString("bad") + QChar::Null}
        })
        {
            auto invalid = group(1);
            invalid.insert("tabs", tabs);
            invalidLayouts.append(terminalLayout(invalid));
        }
        for (const auto& active : QVariantList {42, true, "absent", layoutId(1)})
        {
            auto invalid = group(1, {"first"});
            invalid.insert("active", active);
            invalidLayouts.append(terminalLayout(invalid));
        }
        for (const auto& active : QVariantList {42, true, "", "group-1", layoutId(2)})
        {
            auto invalid = layout;
            invalid.insert("activeGroup", active);
            invalidLayouts.append(invalid);
        }
        const auto twoGroups  = QVariantList {group(1, {"first"}), group(2, {"second"})};
        const auto validSplit = split(twoGroups);
        for (const auto& orientation : QVariantList {42, true, "", "diagonal"})
        {
            auto invalid = validSplit;
            invalid.insert("orientation", orientation);
            invalidLayouts.append(terminalLayout(invalid));
        }
        for (const auto& children : QVariantList {
                 "children",
                 QVariantMap {},
                 QVariantList {},
                 QVariantList {group(1)},
                 QVariantList {group(1), "group"}
        })
        {
            auto invalid = validSplit;
            invalid.insert("children", children);
            invalidLayouts.append(terminalLayout(invalid));
        }
        for (const auto& sizes : QVariantList {
                 "sizes",
                 QVariantMap {},
                 QVariantList {},
                 QVariantList {1},
                 QVariantList {1, 2, 3},
                 QVariantList {-1, 1},
                 QVariantList {true, 1},
                 QVariantList {"1", 1},
                 QVariantList {1.0, 1},
                 QVariantList {0.5, 1},
                 QVariantList {QVariant::fromValue(qlonglong(std::numeric_limits<int>::max()) + 1), 1},
                 QVariantList {QVariant::fromValue(std::numeric_limits<qulonglong>::max()), 1}
        })
        {
            auto invalid = validSplit;
            invalid.insert("sizes", sizes);
            invalidLayouts.append(terminalLayout(invalid));
        }
        for (const auto& field : {"toolbarVisible", "statusBarVisible"})
        {
            for (const auto& value : QVariantList {1, "true", QVariantMap {}})
            {
                auto invalid = group(1);
                invalid.insert(field, value);
                invalidLayouts.append(terminalLayout(invalid));
            }
        }
        invalidLayouts.append(terminalLayout(split({group(1), group(1)})));
        invalidLayouts.append(terminalLayout(split({group(1, {"first"}), split({group(2), group(3, {"first"})})})));
        auto deep = group(1);
        for (int depth = 0; depth < 16; ++depth)
        {
            deep = split({deep, group(depth + 2)});
        }
        invalidLayouts.append(terminalLayout(deep));
        QVariantList tooManyGroups;
        for (int index = 1; index <= 256; ++index)
        {
            tooManyGroups.append(group(index));
        }
        invalidLayouts.append(terminalLayout(split({split(tooManyGroups), group(257)})));

        const QVariantMap validWindow {
            {"id", layoutId(2)},
            {"geometry", "YQ=="},
            {"tree", group(3, {"second"})}
        };
        for (const auto& windows : QVariantList {
                 "windows",
                 QVariantMap {},
                 QVariantList {"window"},
                 QVariantList {QVariantMap {}},
                 QVariantList {validWindow, validWindow}
        })
        {
            auto invalid = layout;
            invalid.insert("windows", windows);
            invalidLayouts.append(invalid);
        }
        QVariantList invalidWindows;
        for (const auto& number : QVariantList {1,
                                                0,
                                                -1,
                                                true,
                                                "2",
                                                2.0,
                                                QVariant::fromValue(qlonglong(std::numeric_limits<int>::max()) + 1)})
        {
            auto window = validWindow;
            window.insert("number", number);
            invalidWindows.append(window);
        }
        auto firstNumbered = validWindow;
        firstNumbered.insert("number", 5);
        auto secondNumbered = validWindow;
        secondNumbered.insert("id", layoutId(4));
        secondNumbered.insert("tree", group(5));
        secondNumbered.insert("number", 5);
        auto repeatedNumber = layout;
        repeatedNumber.insert("windows", QVariantList {firstNumbered, secondNumbered});
        invalidLayouts.append(repeatedNumber);
        for (const auto& id : QVariantList {42, "", "window-1", layoutId(1), layoutId(3)})
        {
            auto window = validWindow;
            window.insert("id", id);
            invalidWindows.append(window);
        }
        for (const auto& geometry : QVariantList {42,
                                                  true,
                                                  QVariantMap {},
                                                  "",
                                                  "%%% =",
                                                  "YQ",
                                                  "YR==",
                                                  "YQ== ",
                                                  QString::fromLatin1(QByteArray(16385, 'x').toBase64())})
        {
            auto window = validWindow;
            window.insert("geometry", geometry);
            invalidWindows.append(window);
        }
        for (const auto& tree : QVariantList {"tree", group(1), group(3, {"unavailable"})})
        {
            auto window = validWindow;
            window.insert("tree", tree);
            invalidWindows.append(window);
        }
        for (const auto& window : invalidWindows)
        {
            auto invalid = layout;
            invalid.insert("windows", QVariantList {window});
            invalidLayouts.append(invalid);
        }
        QVariantList tooManyWindows;
        for (int index = 0; index < 65; ++index)
        {
            tooManyWindows.append(QVariantMap {
                {      "id", layoutId(2 + index * 2)},
                {"geometry",                  "YQ=="},
                {    "tree",    group(3 + index * 2)}
            });
        }
        auto excessiveWindows = layout;
        excessiveWindows.insert("windows", tooManyWindows);
        invalidLayouts.append(excessiveWindows);

        for (qsizetype index = 0; index < invalidLayouts.size(); ++index)
        {
            const auto& invalid = invalidLayouts[index];
            Settings    candidate(session.filePath(), Settings::Info {"termaster.session", "Candidate"});
            QVERIFY2(candidate.replaceSerialized(baseline, &error), qPrintable(error));
            QVERIFY2(candidate.setValue("presentation/terminalLayout", invalid, &error), qPrintable(error));
            const auto bytes = candidate.serializedPatch({}, {}, &error);
            QVERIFY2(bytes, qPrintable(QString("Case %1: %2").arg(index).arg(error)));
            const auto borrowed = session.presentation();
            QVERIFY2(!session.acceptPreparedDocument(*bytes, &error), qPrintable(QString("Case %1").arg(index)));
            QVERIFY(!error.isEmpty());
            QVERIFY(borrowed.isValid());
            QCOMPARE(session.presentation().values(), presentation);
            QVERIFY(!session.isDirty());

            QVERIFY2(session.presentation().setValue("terminalLayout", invalid, &error), qPrintable(error));
            QVERIFY(!session.sync(&error));
            QVERIFY(session.isDirty());
            QVERIFY(disk.open(QIODevice::ReadOnly));
            QCOMPARE(disk.readAll(), baseline);
            disk.close();
            QVERIFY2(session.load(&error), qPrintable(error));

            QVERIFY(disk.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(disk.write(*bytes), bytes->size());
            disk.close();
            const auto live = session.presentation();
            QVERIFY(!session.load(&error));
            QVERIFY(live.isValid());
            QCOMPARE(session.presentation().values(), presentation);
            QVERIFY(disk.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(disk.write(baseline), baseline.size());
            disk.close();
        }

        QVariantList excessiveIds;
        excessiveIds.reserve(100001);
        for (int index = 0; index < 100001; ++index)
        {
            excessiveIds.append(QString("terminal-%1").arg(index));
        }
        QVERIFY(!session.presentation().setValue("terminalLayout", terminalLayout(group(1, excessiveIds)), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(session.presentation().values(), presentation);
        QVERIFY(!session.isDirty());
        QVERIFY(disk.open(QIODevice::ReadOnly));
        QCOMPARE(disk.readAll(), baseline);
    }

    void terminalLayoutAcceptsBoundaryDepthGroupsAndWindows()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Session       session(Path(temporary.filePath("session")), SettingsOverlay(base.node(""), base.node("")));
        QString       error;
        QVERIFY2(session.create("limits", "Limits", &error), qPrintable(error));
        auto deep = group(1);
        for (int depth = 0; depth < 15; ++depth)
        {
            deep = split({deep, group(depth + 2)});
        }
        QVERIFY(session.presentation().setValue("terminalLayout", terminalLayout(deep)));
        QVERIFY2(session.sync(&error), qPrintable(error));
        QVERIFY2(session.load(&error), qPrintable(error));

        QVariantList groups;
        for (int index = 1; index <= 256; ++index)
        {
            groups.append(group(index));
        }
        QVERIFY(session.presentation().setValue("terminalLayout", terminalLayout(split(groups))));
        QVERIFY2(session.sync(&error), qPrintable(error));
        QVERIFY2(session.load(&error), qPrintable(error));

        auto         layout = terminalLayout(group(1));
        QVariantList windows;
        for (int index = 0; index < 64; ++index)
        {
            windows.append(QVariantMap {
                {      "id", layoutId(2 + index * 2)},
                {"geometry",                  "YQ=="},
                {    "tree",    group(3 + index * 2)}
            });
        }
        layout.insert("windows", windows);
        QVERIFY(session.presentation().setValue("terminalLayout", layout));
        QVERIFY2(session.sync(&error), qPrintable(error));
        QVERIFY2(session.load(&error), qPrintable(error));
        QCOMPARE(session.presentation().value("terminalLayout").toMap(), layout);
    }

    void noClobberAndWriteFailure()
    {
        QTemporaryDir   temporary;
        Settings        base(temporary.filePath("base.yaml"));
        Settings        workspace(temporary.filePath("workspace.yaml"));
        SettingsOverlay inherited(base.node(""), workspace.node(""));
        const Path      directory(temporary.filePath("session"));
        Session         first(directory, inherited);
        QVERIFY(first.create("first", "First"));
        Session other(directory, inherited);
        QVERIFY(!other.create("second", "Second"));
        QVERIFY(first.load());
        QCOMPARE(first.id(), QString("first"));
        QVERIFY(first.setName("Pending"));
        QVERIFY(QFile::remove(first.filePath().toString()));
        QVERIFY(QDir().mkdir(first.filePath().toString()));
        QVERIFY(!first.sync());
        QVERIFY(first.isDirty());
        QCOMPARE(first.name(), QString("Pending"));
        Session absent(Path(temporary.filePath("missing/child")), inherited);
        QVERIFY(!absent.create("absent", "Absent"));
    }

    void symlinkResourceConfinement()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Settings      workspace(temporary.filePath("workspace.yaml"));
        Session       session(Path(temporary.filePath("session")), SettingsOverlay(base.node(""), workspace.node("")));
        QVERIFY(session.create("links", "Links"));
        QVERIFY(session.resources().setValue("asset", "linked/missing.bin"));
        QVERIFY(session.resourcePath("asset"));
        QVERIFY(QDir().mkdir(temporary.filePath("outside")));
        std::error_code error;
        std::filesystem::create_directory_symlink(std::filesystem::path(temporary.filePath("outside").toStdU16String()),
                                                  std::filesystem::path(
                                                      session.directory().join("linked").toString().toStdU16String()),
                                                  error);
        if (error)
        {
            QSKIP("Directory symlink creation is unavailable for this user");
        }
        QVERIFY(!session.resourcePath("asset"));
        QVERIFY(!session.sync());
        QVERIFY(session.isDirty());
        QVERIFY(QFile::remove(session.directory().join("linked").toString()));
    }

    void manifestSymlinkConfinement()
    {
        QTemporaryDir   temporary;
        Settings        base(temporary.filePath("base.yaml"));
        Settings        workspace(temporary.filePath("workspace.yaml"));
        SettingsOverlay inherited(base.node(""), workspace.node(""));
        Session         session(Path(temporary.filePath("session")), inherited);
        Session         foreign(Path(temporary.filePath("foreign")), inherited);
        QVERIFY(session.create("session", "Session"));
        QVERIFY(foreign.create("foreign", "Foreign"));
        QVERIFY(session.setName("Pending"));
        QVERIFY(QFile::remove(session.filePath().toString()));
        std::error_code error;
        std::filesystem::create_symlink(std::filesystem::path(foreign.filePath().toString().toStdU16String()),
                                        std::filesystem::path(session.filePath().toString().toStdU16String()),
                                        error);
        if (error)
        {
            QSKIP("File symlink creation is unavailable for this user");
        }
        QVERIFY(!session.load());
        QCOMPARE(session.name(), QString("Pending"));
        QVERIFY(!session.sync());
        QVERIFY(session.isDirty());
        QVERIFY(foreign.load());
        QCOMPARE(foreign.name(), QString("Foreign"));
        QVERIFY(QFile::remove(session.filePath().toString()));
    }

    void expiredInheritance()
    {
        QTemporaryDir   temporary;
        Settings        workspace(temporary.filePath("workspace.yaml"));
        auto            base = std::make_unique<Settings>(temporary.filePath("base.yaml"));
        SettingsOverlay inherited(base->node(""), workspace.node(""));
        Session         session(Path(temporary.filePath("session")), inherited);
        QVERIFY(session.create("test", "Test"));
        auto settings = session.settings();
        base.reset();
        QVERIFY(!settings.isValid());
        QVERIFY(!settings.setValue("x", 1));
    }
};
QTEST_GUILESS_MAIN(SessionTests)
#include "SessionTests.moc"
