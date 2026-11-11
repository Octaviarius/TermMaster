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
class DeferredFixtureProcessor final : public Processor
{
public:
    struct Probe
    {
        int  destroyed                      = 0;
        bool acknowledging                  = false;
        bool destroyedDuringAcknowledgement = false;
    };

    explicit DeferredFixtureProcessor(std::shared_ptr<Probe> probe) : _probe(std::move(probe))
    {
    }
    ~DeferredFixtureProcessor() override
    {
        ++_probe->destroyed;
        _probe->destroyedDuringAcknowledgement |= _probe->acknowledging;
    }

    QVariantMap configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk chunk, Callbacks callbacks) override
    {
        callbacks.output(std::move(chunk));
        callbacks.completed({});
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> done) override
    {
        QTimer::singleShot(2, this, [this, done = std::move(done)] {
            _probe->acknowledging = true;
            done();
            _probe->acknowledging = false;
        });
    }

private:
    std::shared_ptr<Probe> _probe;
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
QVariantMap savedLine(const QString& id, QVariantList nodes)
{
    return {
        {    "id",                             id},
        {  "name",                             id},
        {  "kind",                     "lineflow"},
        {"source", QVariantMap {{"nodes", nodes}}}
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
    void publicationDraftSharesOnlyImmutableSchemaState()
    {
        ProcessorRegistry registry;
        auto              first = savedLine("first", {});
        first.insert("settingsSchema", QString::fromUtf8(schema()));
        first.insert("defaults",
                     QVariantMap {
                         {"prefix", "one"}
        });
        auto second = savedLine("second", {});
        second.insert("settingsSchema", QString::fromUtf8(schema()));
        second.insert("defaults",
                      QVariantMap {
                          {"prefix", "two"}
        });
        QVariantMap draft {
            { "first",  first},
            {"second", second}
        };
        QString error;
        QVERIFY2(registry.validatePublicationDraft("workspace", draft, &error), qPrintable(error));
        const auto parsed = ProcessorRegistry::parseDefinitions(draft, &error);
        QVERIFY(parsed);
        QVERIFY(registry.publishDefinitions("workspace", *parsed, &error));
        QCOMPARE(registry.type("first")->defaults.value("prefix").toString(), QString("one"));
        QCOMPARE(registry.type("second")->defaults.value("prefix").toString(), QString("two"));

        second.insert("defaults",
                      QVariantMap {
                          {"prefix", 42}
        });
        draft.insert("second", second);
        QVERIFY(!registry.validatePublicationDraft("workspace", draft, &error));
        QVERIFY(error.contains("defaults"));
        auto numericSchema = schema();
        numericSchema.replace("type: string, default: default", "type: integer, default: 0");
        second.insert("settingsSchema", QString::fromUtf8(numericSchema));
        draft.insert("second", second);
        QVERIFY2(registry.validatePublicationDraft("workspace", draft, &error), qPrintable(error));

        second.insert("settingsSchema", "not: [valid");
        draft.insert("second", second);
        QVERIFY(!registry.validatePublicationDraft("workspace", draft, &error));
        second.insert("settingsSchema", QString::fromUtf8(numericSchema));
        draft.insert("second", second);
        QVERIFY2(registry.validatePublicationDraft("workspace", draft, &error), qPrintable(error));

        QVERIFY(registry.publishDefinitions("workspace", {line("empty", {})}));
        auto explicitDefault = savedLine("explicit", {});
        explicitDefault.insert("settingsSchema", QString::fromUtf8(registry.type("empty")->settingsSchema));
        QVERIFY2(registry.validatePublicationDraft("workspace",
                                                   {
                                                       {"empty", savedLine("empty", {})},
                                                       {"explicit", explicitDefault}
        },
                                                   &error),
                 qPrintable(error));
    }

    void publicationDraftMatchesTypedValidationContracts()
    {
        ProcessorRegistry registry;
        registry
            .registerType("native-owner",
                          {"configured", "Configured", schema(), [](const auto& configuration, const auto&, QString*) {
                               return std::make_unique<FixtureProcessor>(configuration);
                           }});
        const auto verify = [&](const QList<ProcessorRegistry::Definition>& definitions,
                                bool                                        expected,
                                const QString&                              owner = "workspace") {
            QVariantMap draft;
            for (const auto& definition : definitions)
            {
                auto value = savedLine(definition.id, definition.source.value("nodes").toList());
                value.insert("source", definition.source);
                value.insert("defaults", definition.defaults);
                draft.insert(definition.id, value);
            }
            QString typedError;
            QString draftError;
            QCOMPARE(registry.validatePublication(owner, definitions, &typedError), expected);
            QCOMPARE(registry.validatePublicationDraft(owner, draft, &draftError), expected);
            if (!expected)
            {
                QVERIFY(!typedError.isEmpty());
                QVERIFY(!draftError.isEmpty());
            }
        };
        verify({line("root", {leaf("configured")})}, true);
        verify({line("a", {leaf("b")}), line("b", {leaf("a")})}, false);
        QList<ProcessorRegistry::Definition> deep;
        for (int index = 0; index < 17; ++index)
        {
            deep.append(line(QString("depth%1").arg(index),
                             index == 0 ? QVariantList {} : QVariantList {leaf(QString("depth%1").arg(index - 1))}));
        }
        verify(deep, false);
        auto wrongConfiguration = leaf("configured");
        wrongConfiguration.insert("configuration",
                                  QVariantMap {
                                      {"prefix", 7}
        });
        verify({line("root", {wrongConfiguration})}, false);
        auto wrongTopology = leaf("configured");
        wrongTopology.insert("lane", "duplex");
        auto duplex = line("root", {wrongTopology});
        duplex.source.insert("channels", 2);
        verify({duplex}, false);
        verify({line("configured", {})}, false);
        QVERIFY(registry.publishDefinitions("other-owner", {line("inbound", {leaf("saved")})}));
        verify({line("saved", {})}, true);
        auto changedTarget = line("saved", {});
        changedTarget.source.insert("channels", 2);
        verify({changedTarget}, false);
        verify({line("saved", {leaf("inbound")})}, false);
        verify({line("root", {})}, false, "");
        auto oversized = line("large", {});
        oversized.defaults.insert("padding", QString(4 * 1024 * 1024, 'x'));
        verify({oversized}, false);
        QList<ProcessorRegistry::Definition> excessive;
        for (int index = 0; index < 1025; ++index)
        {
            excessive.append(line(QString("entry%1").arg(index), {}));
        }
        verify(excessive, false);
    }

    void publicationDraftRevalidatesProvidersWithoutPreparingOrPinning()
    {
        ProcessorRegistry          registry;
        int                        validations    = 0;
        int                        preparations   = 0;
        int                        factories      = 0;
        bool                       valid          = true;
        bool                       callbackPinned = false;
        ProcessorCatalog::Provider provider;
        provider.validate = [&](const auto&, QString*) {
            ++validations;
            callbackPinned = !registry.canRemoveOwner("provider-owner");
            return valid;
        };
        provider.prepare = [&](const auto&, const auto&, const auto&, const auto&, QString*) {
            ++preparations;
            return ProcessorCatalog::Type::Factory([&](const auto& configuration, const auto&, QString*) {
                ++factories;
                return std::make_unique<FixtureProcessor>(configuration);
            });
        };
        registry.registerProvider("provider-owner", "fixture", std::move(provider));
        auto value = savedLine("source", {});
        value.insert("kind", "fixture");
        const QVariantMap draft {
            {"source", value}
        };
        QSignalSpy changes(&registry, &ProcessorRegistry::changed);
        QVERIFY(registry.validatePublicationDraft("workspace", draft));
        QCOMPARE(validations, 1);
        QVERIFY(callbackPinned);
        valid = false;
        QVERIFY(!registry.validatePublicationDraft("workspace", draft));
        QCOMPARE(validations, 2);
        QCOMPARE(preparations, 0);
        QCOMPARE(factories, 0);
        QCOMPARE(changes.count(), 0);
        QVERIFY(!registry.type("source"));
        QVERIFY(registry.canRemoveOwner("provider-owner"));
        QVERIFY(registry.removeOwner("provider-owner"));
        QVERIFY(registry.validatePublicationDraft("workspace", draft));
        QCOMPARE(validations, 2);
    }

    void shutdownDrainsNestedScopedRetirementsAndRejectsPreparedFactories()
    {
        ProcessorRegistry      registry;
        auto                   probe = std::make_shared<DeferredFixtureProcessor::Probe>();
        ProcessorCatalog::Type type {"deferred", "Deferred", schema(), [probe](const auto&, const auto&, QString*) {
                                         return std::make_unique<DeferredFixtureProcessor>(probe);
                                     }};
        registry.registerType("fixture-owner", std::move(type));
        QVERIFY(registry.publishDefinitions("workspace",
                                            {line("inner", {leaf("deferred")}), line("outer", {leaf("inner")})}));
        auto scope = registry.scoped({
            {"local", savedLine("local", {leaf("outer")})}
        });
        QVERIFY(scope);
        auto prepared = scope->prepare("local", {}, {});
        QVERIFY(prepared);
        auto instance = prepared->instantiate();
        QVERIFY(instance);
        scope.reset();

        QTimer::singleShot(0, [holder = std::move(instance)]() mutable { holder.reset(); });
        QString error;
        QVERIFY2(registry.shutdown(1000, &error), qPrintable(error));
        QCOMPARE(probe->destroyed, 1);
        QVERIFY(!probe->destroyedDuringAcknowledgement);
        QVERIFY(!prepared->instantiate({}, &error));
        QVERIFY(error.contains("shutting down"));
        QVERIFY(!registry.prepare("deferred", {}, {}, &error));
        prepared.reset();
        QVERIFY2(registry.canRemoveOwner("fixture-owner", &error), qPrintable(error));
        QVERIFY(registry.shutdown(0, &error));
    }

    void unpreparedScopePinsCopiedFactoriesAndProviders()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native-owner");
        ProcessorCatalog::Provider provider;
        provider.prepare = [](const auto&, const auto&, const auto&, const auto&, QString*) {
            return ProcessorCatalog::Type::Factory([](const auto& configuration, const auto&, QString*) {
                return std::make_unique<FixtureProcessor>(configuration);
            });
        };
        registry.registerProvider("provider-owner", "fixture", std::move(provider));
        QVERIFY(registry.canRemoveOwner("native-owner"));
        QVERIFY(registry.canRemoveOwner("provider-owner"));
        QSignalSpy changed(&registry, &ProcessorRegistry::changed);
        QString    error;
        auto       local = registry.scoped({}, &error);
        QVERIFY2(local, qPrintable(error));
        QVERIFY(!registry.canRemoveOwner("native-owner"));
        QVERIFY(!registry.canRemoveOwner("provider-owner"));
        QVERIFY(!registry.removeOwner("native-owner", &error));
        QVERIFY(!registry.removeOwner("provider-owner", &error));
        QCOMPARE(changed.count(), 0);
        QVERIFY(registry.type("native.gate"));
        local.reset();
        QVERIFY(registry.canRemoveOwner("native-owner"));
        QVERIFY(registry.canRemoveOwner("provider-owner"));
        QCOMPARE(changed.count(), 0);
        QVERIFY(registry.removeOwner("provider-owner", &error));
        QVERIFY(registry.removeOwner("native-owner", &error));
    }

    void scopedDefinitionsKeepMetadataAndPreparationIndependent()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native-owner");
        QVERIFY(registry.publishDefinitions("workspace", {line("workspace.flow", {leaf("native.gate", "gate")})}));
        QSignalSpy changes(&registry, &ProcessorRegistry::changed);
        QString    error;
        auto       local = registry.scoped(
            {
                {"local.flow",
                 savedLine("local.flow", {leaf("workspace.flow", "saved"), leaf("native.gate", "direct")})}
        },
            &error);
        QVERIFY2(local, qPrintable(error));
        QCOMPARE(changes.count(), 0);
        QVERIFY(!registry.type("local.flow"));
        QVERIFY(local->type("local.flow"));
        QVERIFY(local->type("local.flow")->available);
        QCOMPARE(local->type("local.flow")->origin, QString("terminal.inplace"));
        auto properties = local->properties("local.flow", {}, {}, &error);
        QVERIFY2(properties, qPrintable(error));
        QCOMPARE(properties->size(), 2);
        QCOMPARE(properties->at(0).path, QString("saved/gate"));
        QCOMPARE(properties->at(1).path, QString("direct"));
        QVERIFY(!registry.canRemoveOwner("native-owner"));
        QVERIFY(!registry.canRemoveOwner("workspace"));
        auto prepared = local->prepare("local.flow", {}, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        QVERIFY(!registry.canRemoveOwner("native-owner"));
        QVERIFY(!registry.canRemoveOwner("workspace"));
        local.reset();
        QVERIFY(!registry.canRemoveOwner("native-owner"));
        auto instance = prepared->instantiate({}, &error);
        QVERIFY2(instance, qPrintable(error));
        prepared.reset();
        QVERIFY(!registry.canRemoveOwner("native-owner"));
        QList<QByteArray> output;
        bool              completed = false;
        instance->accept({QByteArray("scoped")},
                         {[&](auto chunk) { output.append(chunk.bytes); },
                          [&](auto message) {
                              QVERIFY(message.isEmpty());
                              completed = true;
                          }});
        QTRY_VERIFY(completed);
        QCOMPARE(output, QList<QByteArray> {QByteArray("scoped")});
        instance.reset();
        QTRY_VERIFY(registry.canRemoveOwner("native-owner"));
        QTRY_VERIFY(registry.canRemoveOwner("workspace"));
        QCOMPARE(changes.count(), 0);
        QVERIFY(registry.type("workspace.flow"));
    }

    void scopedDefinitionsPreserveProviderLeasesAfterScopeDestruction()
    {
        ProcessorRegistry          registry;
        ProcessorCatalog::Provider provider;
        provider.prepare = [](const auto&, const auto&, const auto&, const auto&, QString*) {
            return ProcessorCatalog::Type::Factory([](const auto& configuration, const auto&, QString*) {
                return std::make_unique<FixtureProcessor>(configuration);
            });
        };
        registry.registerProvider("provider-owner", "fixture", std::move(provider));
        const QVariantMap definition {
            {    "id",   "local.code"},
            {  "name",   "Local code"},
            {  "kind",      "fixture"},
            {"source", QVariantMap {}}
        };
        QString error;
        auto    local = registry.scoped(
            {
                {"local.code", definition}
        },
            &error);
        QVERIFY2(local, qPrintable(error));
        QVERIFY(!registry.canRemoveOwner("provider-owner"));
        auto prepared = local->prepare("local.code", {}, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        local.reset();
        QVERIFY(!registry.canRemoveOwner("provider-owner"));
        auto instance = prepared->instantiate({}, &error);
        QVERIFY2(instance, qPrintable(error));
        prepared.reset();
        QVERIFY(!registry.canRemoveOwner("provider-owner"));
        instance.reset();
        QTRY_VERIFY(registry.canRemoveOwner("provider-owner"));
        QVERIFY(registry.removeOwner("provider-owner"));
    }

    void scopedDefinitionsRejectCollisionsCyclesAndBounds()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native-owner");
        QVERIFY(registry.publishDefinitions("workspace", {line("workspace.flow", {leaf("local.flow")})}));
        // A scope must reject collisions even with an existing publication of its own owner name.
        QVERIFY(registry.publishDefinitions("terminal.inplace", {line("existing.local", {})}));
        QSignalSpy changes(&registry, &ProcessorRegistry::changed);
        QString    error;
        for (const auto& id : {QString("native.gate"), QString("workspace.flow"), QString("existing.local")})
        {
            QVERIFY(!registry.scoped(
                {
                    {id, savedLine(id, {})}
            },
                &error));
            QVERIFY(error.contains("Duplicate"));
        }
        QVERIFY(!registry.scoped(
            {
                {"local.flow", savedLine("local.flow", {leaf("workspace.flow")})}
        },
            &error));
        QVERIFY(error.contains("cycle"));
        QVERIFY(!registry.scoped(
            {
                {"local.flow", savedLine("local.flow", {leaf("local.flow")})}
        },
            &error));
        QVERIFY(error.contains("cycle"));
        QVariantMap oversized;
        for (int index = 0; index < 1025; ++index)
        {
            const auto id = QString("node%1").arg(index);
            oversized.insert(id, savedLine(id, {}));
        }
        QVERIFY(!registry.scoped(oversized, &error));
        QVERIFY(error.contains("1024"));
        QVariantList nodes;
        for (int index = 0; index < 257; ++index)
        {
            nodes.append(leaf("native.gate", QString("node%1").arg(index)));
        }
        QVERIFY(!registry.scoped(
            {
                {"local.flow", savedLine("local.flow", nodes)}
        },
            &error));
        QVERIFY(error.contains("256"));
        QVERIFY(!registry.scoped(
            {
                {"local.flow", QVariant::fromValue(static_cast<QObject*>(this))}
        },
            &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(changes.count(), 0);
        QVERIFY(!registry.type("local.flow"));
        QVERIFY(!registry.type("workspace.flow")->available);
        QVERIFY(registry.canRemoveOwner("native-owner"));
        QVERIFY(registry.canRemoveOwner("workspace"));
        QVERIFY(registry.canRemoveOwner("terminal.inplace"));
    }

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
        QString       error;
        QElapsedTimer deadline;
        deadline.start();
        QVERIFY(!registry.shutdown(1, &error));
        QVERIFY(error.contains("timed out"));
        QVERIFY(deadline.elapsed() < 1000);
        QVERIFY(!registry.canRemoveOwner("broken-owner"));
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
