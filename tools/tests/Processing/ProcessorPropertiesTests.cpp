#include <QTest>
#include <Termaster/Core/Processing/ProcessorRegistry.h>

namespace
{
QByteArray schema()
{
    return R"(version: 1
type: termaster.settings-schema
description: Property fixture
content:
  type: object
  properties:
    value: {type: integer, minimum: 0, maximum: 20, default: 1}
    label: {type: string, default: schema}
    nested:
      type: object
      properties:
        a: {type: integer, default: 1}
        b: {type: integer, default: 2}
)";
}
class PropertyFixture final : public Processor
{
public:
    explicit PropertyFixture(QVariantMap configuration) : _configuration(std::move(configuration))
    {
    }
    QVariantMap configuration() const override
    {
        return _configuration;
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        if (callbacks.output)
        {
            callbacks.output(std::move(input));
        }
        if (callbacks.completed)
        {
            callbacks.completed({});
        }
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> done) override
    {
        if (done)
        {
            done();
        }
    }

private:
    QVariantMap _configuration;
};
ProcessorRegistry::Definition line(const QString& id, const QVariantList& nodes, const QVariantMap& defaults = {})
{
    return {id, id, "lineflow", {{"nodes", nodes}}, {}, defaults, {}, {}};
}
QVariantMap leaf(const QString& name, const QString& type, const QVariantMap& configuration = {})
{
    return {
        {         "name",          name},
        {         "type",          type},
        {"configuration", configuration}
    };
}
void registerFixture(ProcessorRegistry& registry, QList<QVariantMap>& created)
{
    ProcessorCatalog::Type type {"fixture",
                                 "Fixture title",
                                 schema(),
                                 [&created](const QVariantMap& configuration, const auto&, QString*) {
                                     created.append(configuration);
                                     return std::make_unique<PropertyFixture>(configuration);
                                 }};
    type.defaults = {
        {"value", 5}
    };
    registry.registerType("native", std::move(type));
}
} // namespace

class ProcessorPropertiesTests final : public QObject
{
    Q_OBJECT
private slots:
    void nativeRootDefaultsAndOverridesAreMetadataOnly()
    {
        ProcessorRegistry  registry;
        QList<QVariantMap> created;
        registerFixture(registry, created);
        QString    error;
        const auto nodes = registry.properties("fixture",
                                               {
                                                   {"label", "stored"}
        },
                                               {{"value", 9}},
                                               &error);
        QVERIFY2(nodes, qPrintable(error));
        QCOMPARE(nodes->size(), 1);
        const auto snapshot = nodes->first();
        QVERIFY(snapshot.path.isEmpty());
        QCOMPARE(snapshot.type, QString("fixture"));
        QCOMPARE(snapshot.title, QString("Fixture title"));
        QCOMPARE(snapshot.settingsSchema, schema());
        QCOMPARE(snapshot.inheritedConfiguration.value("value").toInt(), 5);
        QCOMPARE(snapshot.inheritedConfiguration.value("label").toString(), "stored");
        QCOMPARE(snapshot.configuration.value("value").toInt(), 9);
        QVERIFY(created.isEmpty());
        QVERIFY(registry.canRemoveOwner("native"));
        QVERIFY(registry.removeOwner("native"));
        QCOMPARE(snapshot.title, QString("Fixture title"));
        QCOMPARE(snapshot.configuration.value("value").toInt(), 9);
        QCOMPARE(snapshot.settingsSchema, schema());
    }

    void nestedRepeatedOccurrencesMatchRuntimePrecedence()
    {
        ProcessorRegistry  registry;
        QList<QVariantMap> created;
        registerFixture(registry, created);
        const auto branch = line("branch",
                                 {
                                     leaf("item", "fixture", {{"value", 3}, {"opaque", "kept"}}
                                       )
        },
                                 {{"overrides",
                                   QVariantMap {{"item", QVariantMap {{"value", 2}, {"label", "branch-default"}}}}}});
        const auto root   = line("root",
                                 {
                                     leaf("left", "branch"),
                                     leaf("right", "branch"),
                                     leaf("last", "fixture")
        },
                                 {{"overrides",
                                   QVariantMap {{"left/item", QVariantMap {{"value", 4}}},
                                                {"right/item", QVariantMap {{"label", "root-default"}}}}}});
        QString    error;
        QVERIFY2(registry.publishDefinitions("workspace", {branch, root}, &error), qPrintable(error));
        const QVariantMap stored {
            {"overrides", QVariantMap {{"left/item", QVariantMap {{"label", "stored"}}}}}
        };
        const QVariantMap overrides {
            { "left/item",                       QVariantMap {{"value", 9}}},
            {"right/item", QVariantMap {{"nested", QVariantMap {{"b", 8}}}}}
        };
        const auto nodes = registry.properties("root", stored, overrides, &error);
        QVERIFY2(nodes, qPrintable(error));
        QCOMPARE(nodes->size(), 3);
        QCOMPARE(nodes->at(0).path, QString("left/item"));
        QCOMPARE(nodes->at(1).path, QString("right/item"));
        QCOMPARE(nodes->at(2).path, QString("last"));
        QCOMPARE(nodes->at(0).inheritedConfiguration.value("value").toInt(), 4);
        QCOMPARE(nodes->at(0).inheritedConfiguration.value("label").toString(), "stored");
        QCOMPARE(nodes->at(0).configuration.value("value").toInt(), 9);
        QCOMPARE(nodes->at(0).configuration.value("opaque").toString(), "kept");
        QCOMPARE(nodes->at(1).inheritedConfiguration.value("value").toInt(), 2);
        QCOMPARE(nodes->at(1).inheritedConfiguration.value("label").toString(), "root-default");
        QCOMPARE(nodes->at(1).inheritedConfiguration.value("nested").toMap().value("b").toInt(), 2);
        QCOMPARE(nodes->at(1).configuration.value("nested").toMap().value("a").toInt(), 1);
        QCOMPARE(nodes->at(1).configuration.value("nested").toMap().value("b").toInt(), 8);
        QCOMPARE(nodes->at(2).configuration.value("value").toInt(), 5);
        QVERIFY(created.isEmpty());
        QVERIFY(registry.canRemoveOwner("native"));
        auto prepared = registry.prepare("root", stored, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        auto runtime = prepared->instantiate(overrides, &error);
        QVERIFY2(runtime, qPrintable(error));
        QCOMPARE(created.size(), nodes->size());
        for (qsizetype index = 0; index < created.size(); ++index)
        {
            QCOMPARE(created[index], nodes->at(index).configuration);
        }
        runtime.reset();
        prepared.reset();
        QTRY_VERIFY(registry.canRemoveOwner("native"));
    }

    void providerIsOneEditableLeafWithoutExecutingProviderCode()
    {
        ProcessorRegistry          registry;
        int                        validations  = 0;
        int                        preparations = 0;
        ProcessorCatalog::Provider provider;
        provider.validate = [&](const auto&, QString*) {
            ++validations;
            return true;
        };
        provider.prepare = [&](const auto&, const auto&, const auto&, const auto&, QString*) {
            ++preparations;
            return ProcessorCatalog::Type::Factory {};
        };
        registry.registerProvider("provider", "code-kind", std::move(provider));
        ProcessorRegistry::Definition code {"saved-code",
                                            "Saved code",
                                            "code-kind",
                                            {{"code", "opaque"}},
                                            schema(),
                                            {{"value", 6}},
                                            {},
                                            {"missing.reference"}};
        QVERIFY(registry.publishDefinitions("workspace", {code}));
        validations = 0;
        QString error;
        auto    properties = registry.properties("saved-code",
                                                 {
        },
                                                 {{"value", 8}},
                                                 &error);
        QVERIFY2(properties, qPrintable(error));
        QCOMPARE(properties->size(), 1);
        QVERIFY(properties->first().path.isEmpty());
        QCOMPARE(properties->first().inheritedConfiguration.value("value").toInt(), 6);
        QCOMPARE(properties->first().configuration.value("value").toInt(), 8);
        QCOMPARE(validations, 0);
        QCOMPARE(preparations, 0);
        QVERIFY(registry.removeOwner("provider"));
        // Unavailable providers remain configurable from their saved schema and defaults.
        QVERIFY(registry.properties("saved-code", {}, {}, &error));
        QCOMPARE(properties->first().title, QString("Saved code"));
    }

    void missingAddressesInvalidOverridesAndCyclesReject()
    {
        ProcessorRegistry  registry;
        QList<QVariantMap> created;
        registerFixture(registry, created);
        QString error;
        QVERIFY(!registry.properties("missing", {}, {}, &error));
        QVERIFY(error.contains("Unavailable"));
        auto root = line("root", {leaf("item", "fixture")});
        QVERIFY(registry.publishDefinitions("workspace", {root}));
        QVERIFY(!registry.properties("root",
                                     {
        },
                                     {{"missing", QVariantMap {}}},
                                     &error));
        QVERIFY(error.contains("occurrence"));
        QVERIFY(!registry.properties("root",
                                     {
        },
                                     {{"item", 3}},
                                     &error));
        QVERIFY(!registry.properties("root",
                                     {
        },
                                     {{"item", QVariantMap {{"value", 30}}}},
                                     &error));
        QVERIFY(error.contains("configuration"));
        QVERIFY(!registry.properties("fixture",
                                     {
        },
                                     {{"value", "wrong-type"}},
                                     &error));
        auto missing = line("missing-root", {leaf("unknown", "absent")});
        QVERIFY(registry.publishDefinitions("workspace", {root, missing}));
        QVERIFY(!registry.properties("missing-root", {}, {}, &error));
        QVERIFY(error.contains("absent"));
        auto cycle = line("cycle", {leaf("self", "cycle")});
        QVERIFY(!registry.publishDefinitions("workspace", {cycle}, &error));
        QVERIFY(error.contains("cycle"));
        QVERIFY(registry.properties("root", {}, {}, &error));
        QVERIFY(created.isEmpty());
    }

    void repeatedExpansionAndPayloadBounds()
    {
        ProcessorRegistry  registry;
        QList<QVariantMap> created;
        registerFixture(registry, created);
        QList<ProcessorRegistry::Definition> definitions;
        definitions.append(line("level0", {leaf("item", "fixture")}));
        for (int index = 1; index < 8; ++index)
        {
            const auto previous = "level" + QString::number(index - 1);
            definitions.append(
                line("level" + QString::number(index), {leaf("left", previous), leaf("right", previous)}));
        }
        QString error;
        QVERIFY(registry.publishDefinitions("workspace", definitions, &error));
        QVERIFY(registry.properties("level6", {}, {}, &error));
        QVERIFY(!registry.properties("level7", {}, {}, &error));
        QVERIFY(error.contains("256"));
        QVariantMap deep;
        for (int index = 0; index < 66; ++index)
        {
            deep = {
                {"child", deep}
            };
        }
        QVERIFY(!registry.properties("fixture", deep, {}, &error));
        QVERIFY(error.contains("bounds"));
        QVERIFY(!registry.properties("fixture",
                                     {
                                         {"opaque", QString(4 * 1024 * 1024, 'x')}
        },
                                     {},
                                     &error));
        QVERIFY(error.contains("bounds"));
        QVERIFY(created.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ProcessorPropertiesTests)
#include "ProcessorPropertiesTests.moc"
