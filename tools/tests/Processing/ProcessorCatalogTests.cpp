#include <QtTest>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <stdexcept>

namespace
{
class FixtureProcessor final : public Processor
{
public:
    explicit FixtureProcessor(QVariantMap configuration, bool acknowledge = true) :
        _configuration(std::move(configuration)), _acknowledge(acknowledge)
    {
    }
    QVariantMap configuration() const override
    {
        return _configuration;
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        input.bytes.prepend(_configuration.value("prefix").toString().toUtf8());
        callbacks.output(std::move(input));
        callbacks.completed({});
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> done) override
    {
        if (_acknowledge)
        {
            done();
        }
    }

private:
    QVariantMap _configuration;
    bool        _acknowledge;
};
ProcessorRegistry::Definition line(const QString& id, QVariantList nodes)
{
    return {id, id, "lineflow", {{"nodes", nodes}}, {}, {}, {}, {}};
}
QVariantMap leaf(const QString& id, const QString& name = "leaf")
{
    return {
        {"name", name},
        {"type",   id}
    };
}
QByteArray schema()
{
    return QByteArray(
        "version: 1\ntype: termaster.settings-schema\ndescription: Fixture\ncontent:\n  type: object\n  properties:\n    prefix: {type: string, default: default}\n");
}
} // namespace
class ProcessorCatalogTests final : public QObject
{
    Q_OBJECT
private slots:
    void nativeGroupMetadataOwnsSnapshotsAndStripsFactories()
    {
        ProcessorRegistry      registry;
        QChar                  rawGroup[] {'B', 'y', 't', 'e', 's'};
        ProcessorCatalog::Type type {"grouped",
                                     "Grouped fixture",
                                     schema(),
                                     [](const auto& config, const auto&, QString*) {
                                         return std::make_unique<FixtureProcessor>(config);
                                     }};
        type.group = QString::fromRawData(rawGroup, 5);
        registry.registerType("native-owner", type);
        rawGroup[0]         = 'X';
        const auto snapshot = registry.type("grouped");
        QVERIFY(snapshot);
        QCOMPARE(snapshot->group, QString("Bytes"));
        QVERIFY(!snapshot->create);
        const auto all = registry.types();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].group, QString("Bytes"));
        QVERIFY(!all[0].create);
        QVERIFY(registry.removeOwner("native-owner"));
        QCOMPARE(snapshot->group, QString("Bytes"));
        QCOMPARE(all[0].group, QString("Bytes"));
        QVERIFY(!registry.type("grouped"));
    }

    void savedGroupsParsePublishUpdateAndPreserveSnapshots()
    {
        QVariantMap value {
            {     "id",                                "grouped"},
            {   "name",                                "Grouped"},
            {   "kind",                               "lineflow"},
            { "source", QVariantMap {{"nodes", QVariantList {}}}},
            {  "group",         QString::fromUtf8("Protocol 雪")},
            {"unknown",                "retained by persistence"}
        };
        QString error;
        auto    parsed = ProcessorRegistry::parseDefinitions(
            {
                {"grouped", value}
        },
            &error);
        QVERIFY2(parsed, qPrintable(error));
        QCOMPARE(parsed->first().group, QString::fromUtf8("Protocol 雪"));
        ProcessorRegistry registry;
        QSignalSpy        changed(&registry, &ProcessorRegistry::changed);
        QVERIFY(registry.publishDefinitions("workspace", *parsed, &error));
        QCOMPARE(changed.count(), 1);
        const auto snapshot = registry.type("grouped");
        QVERIFY(snapshot);
        QCOMPARE(snapshot->group, QString::fromUtf8("Protocol 雪"));
        QVERIFY(!snapshot->create);
        auto definition  = parsed->first();
        definition.group = "Updated group";
        QVERIFY(registry.publishDefinitions("workspace", {definition}, &error));
        QCOMPARE(changed.count(), 2);
        QCOMPARE(registry.type("grouped")->group, QString("Updated group"));
        QCOMPARE(registry.types().first().group, QString("Updated group"));
        QCOMPARE(snapshot->group, QString::fromUtf8("Protocol 雪"));
        QVERIFY(registry.publishDefinitions("workspace", {}, &error));
        QCOMPARE(snapshot->group, QString::fromUtf8("Protocol 雪"));
        value.remove("group");
        parsed = ProcessorRegistry::parseDefinitions(
            {
                {"grouped", value}
        },
            &error);
        QVERIFY(parsed);
        QVERIFY(parsed->first().group.isEmpty());
        value.insert("group", QString {});
        QVERIFY(ProcessorRegistry::parseDefinitions(
            {
                {"grouped", value}
        },
            &error));
    }

    void invalidGroupsRejectAtomicallyAcrossRegistrationPaths()
    {
        ProcessorRegistry registry;
        auto              original = line("root", {});
        original.group             = "Original group";
        QVERIFY(registry.publishDefinitions("workspace", {original}));
        QSignalSpy            changed(&registry, &ProcessorRegistry::changed);
        const QList<QVariant> invalid {7,
                                       true,
                                       QVariantMap {},
                                       QString(" leading"),
                                       QString("trailing "),
                                       QString(129, 'x'),
                                       QString("nul") + QChar::Null + QString("inside")};
        QString               error;
        for (const auto& group : invalid)
        {
            const QVariantMap definition {
                {    "id",                                   "root"},
                {  "name",                                   "Root"},
                {  "kind",                               "lineflow"},
                {"source", QVariantMap {{"nodes", QVariantList {}}}},
                { "group",                                    group}
            };
            QVERIFY(!ProcessorRegistry::parseDefinitions(
                {
                    {"root", definition}
            },
                &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(registry.type("root")->group, QString("Original group"));
            QCOMPARE(changed.count(), 0);
            if (group.typeId() != QMetaType::QString)
            {
                continue;
            }
            auto replacement  = original;
            replacement.group = group.toString();
            QVERIFY(!registry.publishDefinitions("workspace", {replacement}, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(registry.type("root")->group, QString("Original group"));
            QCOMPARE(changed.count(), 0);
            ProcessorCatalog::Type native {"invalid",
                                           "Invalid group",
                                           schema(),
                                           [](const auto& config, const auto&, QString*) {
                                               return std::make_unique<FixtureProcessor>(config);
                                           }};
            native.group = group.toString();
            QVERIFY_EXCEPTION_THROWN(registry.registerType("native-owner", native), std::invalid_argument);
            QVERIFY(!registry.type("invalid"));
            QCOMPARE(changed.count(), 0);
        }
        auto valid  = original;
        valid.group = QString(128, 'x');
        QVERIFY(registry.publishDefinitions("workspace", {valid}, &error));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(registry.type("root")->group.size(), 128);
    }

    void atomicPublicationAvailabilityAndCrossKindCycles()
    {
        ProcessorRegistry registry;
        QSignalSpy        changes(&registry, &ProcessorRegistry::changed);
        auto              root = line("root", {leaf("missing")});
        QString           error;
        QVERIFY(registry.publishDefinitions("workspace", {root}, &error));
        QVERIFY(!registry.type("root")->available);
        QVERIFY(registry.type("root")->unavailableReason.contains("missing"));
        registerNativeProcessors(registry, "native-owner");
        root.source.insert("nodes", QVariantList {leaf("native.gate")});
        QVERIFY(registry.publishDefinitions("workspace", {root}, &error));
        QVERIFY(registry.type("root")->available);
        const auto count = changes.size();
        auto       cycle = line("child", {leaf("root")});
        root.source.insert("nodes", QVariantList {leaf("child")});
        QVERIFY(!registry.publishDefinitions("workspace", {root, cycle}, &error));
        QCOMPARE(changes.size(), count);
        QVERIFY(registry.type("root")->available);
        ProcessorRegistry::Definition code {"code", "Code", "python", {}, {}, {}, {}, {"root"}};
        root.source.insert("nodes", QVariantList {leaf("code")});
        QVERIFY(!registry.publishDefinitions("workspace", {root, code}, &error));
        QVERIFY(error.contains("cycle"));
        QVERIFY(registry.publishDefinitions("workspace", {}, &error));
        QVERIFY(!registry.type("root"));
    }

    void independentSnapshotsSurviveUpdatesAndRemoval()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native-owner");
        auto    root = line("root", {leaf("native.framer")});
        QString error;
        QVERIFY(registry.publishDefinitions("workspace", {root}, &error));
        auto prepared = registry.prepare("root", {}, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        QVERIFY(!registry.canRemoveOwner("native-owner", &error));
        auto first  = prepared->instantiate({}, &error);
        auto second = prepared->instantiate({}, &error);
        QVERIFY(first && second && first != second);
        QList<QByteArray> firstOutput;
        QList<QByteArray> secondOutput;
        bool              completed = false;
        QCOMPARE(first->accept({QByteArray("a")},
                               {[&](auto chunk) { firstOutput.append(chunk.bytes); },
                                [&](auto message) {
                                    QVERIFY(message.isEmpty());
                                    completed = true;
                                }}),
                 Processor::AcceptResult::Accepted);
        QTRY_VERIFY(completed);
        QVERIFY(firstOutput.isEmpty());
        root.source.insert("nodes", QVariantList {leaf("native.gate")});
        QVERIFY(registry.publishDefinitions("workspace", {root}, &error));
        QVERIFY(registry.publishDefinitions("workspace", {}, &error));
        completed = false;
        first->accept({QByteArray("b\r")},
                      {[&](auto chunk) { firstOutput.append(chunk.bytes); }, [&](auto) { completed = true; }});
        QTRY_VERIFY(completed);
        QCOMPARE(firstOutput, QList<QByteArray> {QByteArray("ab")});
        completed = false;
        second->accept({QByteArray("c\r")},
                       {[&](auto chunk) { secondOutput.append(chunk.bytes); }, [&](auto) { completed = true; }});
        QTRY_VERIFY(completed);
        QCOMPARE(secondOutput, QList<QByteArray> {QByteArray("c")});
        auto third = prepared->instantiate({}, &error);
        QVERIFY2(third, qPrintable(error));
        first.reset();
        second.reset();
        third.reset();
        prepared.reset();
        QTRY_VERIFY(registry.canRemoveOwner("native-owner", &error));
        QTRY_VERIFY(registry.canRemoveOwner("workspace", &error));
    }

    void lazyProviderAndPreparedFactoryLeases()
    {
        ProcessorRegistry          registry;
        int                        preparations  = 0;
        int                        constructions = 0;
        ProcessorCatalog::Provider provider;
        provider.validate = [](const auto& source, QString* error) {
            if (source.value("text").typeId() == QMetaType::QString)
            {
                return true;
            }
            if (error)
            {
                *error = "Missing text";
            }
            return false;
        };
        provider.prepare = [&](const auto& source, const auto&, const auto&, const auto&, QString*) {
            ++preparations;
            const auto text = source.value("text").toString();
            return ProcessorCatalog::Type::Factory([&, text](const QVariantMap& config, const auto&, QString*) {
                ++constructions;
                auto effective = config;
                effective.insert("source", text);
                return std::make_unique<FixtureProcessor>(effective);
            });
        };
        ProcessorRegistry::Definition code {"code",
                                            "Code",
                                            "python",
                                            {{"text", "old"}},
                                            schema(),
                                            {{"prefix", "saved"}},
                                            {{"language", "python"}},
                                            {}};
        QVERIFY(registry.publishDefinitions("workspace", {code}));
        QVERIFY(!registry.type("code")->available);
        registry.registerProvider("provider", "python", std::move(provider));
        QVERIFY(registry.type("code")->available);
        QCOMPARE(preparations, 0);
        QCOMPARE(constructions, 0);
        auto prepared = registry.prepare("code", {}, {});
        QVERIFY(prepared);
        QCOMPARE(preparations, 1);
        QCOMPARE(constructions, 0);
        code.source.insert("text", "new");
        QVERIFY(registry.publishDefinitions("workspace", {code}));
        QVERIFY(registry.unregisterProvider("provider", "python"));
        QVERIFY(!registry.type("code")->available);
        QVERIFY(!registry.canRemoveOwner("provider"));
        auto first  = prepared->instantiate({
            {"prefix", "private"}
        });
        auto second = prepared->instantiate();
        QVERIFY(first && second);
        QCOMPARE(first->configuration().value("source").toString(), QString("old"));
        QCOMPARE(first->configuration().value("prefix").toString(), QString("private"));
        QCOMPARE(second->configuration().value("prefix").toString(), QString("saved"));
        first.reset();
        second.reset();
        prepared.reset();
        QTRY_VERIFY(registry.canRemoveOwner("provider"));
    }

    void discardedCancellationAcknowledgementPinsFactory()
    {
        ProcessorRegistry      registry;
        ProcessorCatalog::Type type {"broken", "Broken", schema(), [](const auto& config, const auto&, QString*) {
                                         return std::make_unique<FixtureProcessor>(config, false);
                                     }};
        registry.registerType("broken-owner", std::move(type));
        auto instance = registry.create("broken", {}, {});
        QVERIFY(instance);
        instance.reset();
        QCoreApplication::processEvents();
        QVERIFY(!registry.canRemoveOwner("broken-owner"));
        QVERIFY(!registry.unregisterType("broken-owner", "broken"));
    }

    void sharedGraphDepthAndAvailabilityAreBounded()
    {
        ProcessorRegistry                    registry;
        QList<ProcessorRegistry::Definition> definitions;
        definitions.append(line("a", {}));
        for (int index = 1; index <= 16; ++index)
        {
            const auto previous = index == 1 ? QString("a") : QString("node%1").arg(index - 1);
            definitions.append(line(QString("node%1").arg(index), {leaf(previous, "left"), leaf(previous, "right")}));
        }
        QString error;
        QVERIFY(!registry.publishDefinitions("workspace", definitions, &error));
        QVERIFY(error.contains("depth"));
        definitions.removeLast();
        QVERIFY2(registry.publishDefinitions("workspace", definitions, &error), qPrintable(error));
        QVERIFY(registry.type("node15")->available);
        ProcessorRegistry::Definition code {"code", "Code", "validator", {}, {}, {}, {}, {}};
        QVERIFY(registry.publishDefinitions("workspace", {code}, &error));
        QVERIFY(!registry.type("code")->available);
        ProcessorCatalog::Provider provider;
        provider.validate = [](const auto&, QString* error) {
            if (error)
            {
                *error = QStringLiteral("Invalid source fixture");
            }
            return false;
        };
        provider.prepare = [](const auto&, const auto&, const auto&, const auto&, QString*) {
            return ProcessorCatalog::Type::Factory {};
        };
        registry.registerProvider("validator-owner", "validator", std::move(provider));
        const auto metadata = registry.type("code");
        QVERIFY(!metadata->available);
        QCOMPARE(metadata->unavailableReason, QString("Invalid source fixture"));
        QVERIFY(registry.removeOwner("validator-owner"));
        QCOMPARE(metadata->unavailableReason, QString("Invalid source fixture"));
    }

    void knownNodeConfigurationValidationAndLateAvailability()
    {
        ProcessorRegistry registry;
        auto              node = leaf("configured");
        node.insert("configuration",
                    QVariantMap {
                        {"prefix", 7}
        });
        auto    root = line("root", {node});
        QString error;
        QVERIFY(registry.publishDefinitions("workspace", {root}, &error));
        ProcessorCatalog::Type type {"configured",
                                     "Configured",
                                     schema(),
                                     [](const auto& config, const auto&, QString*) {
                                         return std::make_unique<FixtureProcessor>(config);
                                     }};
        registry.registerType("native-owner", std::move(type));
        QVERIFY(!registry.type("root")->available);
        QVERIFY(registry.type("root")->unavailableReason.contains("configuration"));
        QVERIFY(!registry.publishDefinitions("workspace", {root}, &error));
        QVERIFY(error.contains("configuration"));
        node.insert("configuration",
                    QVariantMap {
                        {"prefix", "valid"}
        });
        root.source.insert("nodes", QVariantList {node});
        QVERIFY(registry.publishDefinitions("workspace", {root}, &error));
        QVERIFY(registry.type("root")->available);
    }

    void providerExceptionsAndCompositePrivateValidation()
    {
        ProcessorRegistry          registry;
        ProcessorCatalog::Provider provider;
        provider.validate = [](const auto&, QString*) -> bool { throw 7; };
        provider.prepare  = [](const auto&, const auto&, const auto&, const auto&, QString*) {
            return ProcessorCatalog::Type::Factory {};
        };
        registry.registerProvider("throws", "throwing", std::move(provider));
        ProcessorRegistry::Definition code {"code", "Code", "throwing", {}, {}, {}, {}, {}};
        QString                       error;
        QVERIFY(!registry.publishDefinitions("workspace", {code}, &error));
        QVERIFY(!registry.type("code"));
        QVERIFY(!error.isEmpty());
        registerNativeProcessors(registry, "native-owner");
        auto root           = line("root", {leaf("native.gate")});
        root.settingsSchema = QByteArray(
            "version: 1\ntype: termaster.settings-schema\ndescription: Composite constraint\ncontent:\n  type: object\n  properties:\n    overrides:\n      type: object\n      properties:\n        leaf:\n          type: object\n          properties:\n            prefix: {type: string, enum: [valid]}\n");
        QVERIFY(registry.publishDefinitions("workspace", {root}, &error));
        auto prepared = registry.prepare("root", {}, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        const QVariantMap overrides {
            {"leaf", QVariantMap {{"prefix", "invalid"}}}
        };
        QVERIFY(!prepared->instantiate(overrides, &error));
        QVERIFY(error.contains("composite"));
    }

    void parsingMissingReferencesAndBounds()
    {
        QString     error;
        QVariantMap root {
            {    "id",                                                      "root"},
            {  "name",                                                      "Root"},
            {  "kind",                                                  "lineflow"},
            {"source", QVariantMap {{"nodes", QVariantList {leaf("unavailable")}}}}
        };
        QVariantMap definitions {
            {"root", root}
        };
        auto parsed = ProcessorRegistry::parseDefinitions(definitions, &error);
        QVERIFY2(parsed.has_value(), qPrintable(error));
        QCOMPARE(parsed->size(), 1);
        root.insert("source",
                    QVariantMap {
                        {"nodes", QVariantList {leaf("root")}}
        });
        definitions.insert("root", root);
        QVERIFY(!ProcessorRegistry::parseDefinitions(definitions, &error));
        QVERIFY(error.contains("cycle"));
        root.insert("source",
                    QVariantMap {
                        {"nodes", QVariantList {leaf("other", "bad/name")}}
        });
        definitions.insert("root", root);
        QVERIFY(!ProcessorRegistry::parseDefinitions(definitions, &error));
        definitions.insert("root", QVariant::fromValue(static_cast<QObject*>(this)));
        QVERIFY(!ProcessorRegistry::parseDefinitions(definitions, &error));
    }
};
QTEST_GUILESS_MAIN(ProcessorCatalogTests)
#include "ProcessorCatalogTests.moc"
