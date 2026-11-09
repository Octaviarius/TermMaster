#include <QtTest>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <stdexcept>

namespace
{
QByteArray descriptor(const QByteArray& content)
{
    QByteArray result("version: 1\ntype: termaster.settings-schema\ndescription: Defaults fixture\ncontent:\n");
    for (const auto& line : content.split('\n'))
    {
        result += "  " + line + '\n';
    }
    return result;
}

class SnapshotProcessor final : public Processor
{
public:
    explicit SnapshotProcessor(QVariantMap configuration) : _configuration(std::move(configuration))
    {
    }

    QVariantMap configuration() const override
    {
        return _configuration;
    }

    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        callbacks.output(std::move(input));
        callbacks.completed({});
        return AcceptResult::Accepted;
    }

    void cancel(std::function<void()> quiescent) override
    {
        quiescent();
    }

private:
    QVariantMap _configuration;
};

ProcessorCatalog::Type fixtureType(const QByteArray& schema)
{
    return {"defaults.fixture",
            "Defaults fixture",
            descriptor(schema),
            [](const QVariantMap& configuration, const Processor::Limits&, QString*) {
                return std::make_unique<SnapshotProcessor>(configuration);
            }};
}
} // namespace

class ProcessorDefaultsTests : public QObject
{
    Q_OBJECT
private slots:
    void requiredDefaultsReachNativeInstance()
    {
        ProcessorRegistry registry;
        QString           error;
        registry.registerType("fixture", fixtureType(R"(type: object
required: [size]
properties:
  size: {type: integer, minimum: 1, default: 32}
)"));

        const QVariantMap stored;
        QVERIFY2(registry.validateConfiguration("defaults.fixture", stored, &error), qPrintable(error));
        auto instance = registry.create("defaults.fixture", stored, {}, &error);
        QVERIFY2(instance, qPrintable(error));
        QCOMPARE(instance->configuration().value("size").toInt(), 32);
        QVERIFY(stored.isEmpty());

        instance.reset();
        QTRY_VERIFY(registry.canRemoveOwner("fixture"));
    }

    void recursiveDefaultsStoredValuesAndPrivateOverrides()
    {
        ProcessorRegistry registry;
        QString           error;
        registry.registerType("fixture", fixtureType(R"(type: object
required: [nested, labels]
properties:
  nested:
    type: object
    required: [first, second, third]
    properties:
      first: {type: integer, default: 1}
      second: {type: integer, default: 2}
      third:
        type: object
        properties:
          flag: {type: boolean, default: true}
  labels: {type: array, items: {type: string}, default: [default]}
)"));

        const QVariantMap stored {
            {     "nested", QVariantMap {{"first", 10}, {"unknownStored", "kept"}}},
            {     "labels",                      QVariantList {"stored", "second"}},
            {"unknownRoot",                QVariantMap {{"inherited", "retained"}}}
        };
        const QVariantMap overrides {
            {     "nested",       QVariantMap {{"second", 20}}},
            {     "labels",           QVariantList {"private"}},
            {"unknownRoot", QVariantMap {{"private", "added"}}}
        };
        const auto storedSnapshot    = stored;
        const auto overridesSnapshot = overrides;

        const auto effective = registry.resolveConfiguration("defaults.fixture", stored, overrides, &error);
        QVERIFY2(effective.has_value(), qPrintable(error));
        const auto nested = effective->value("nested").toMap();
        QCOMPARE(nested.value("first").toInt(), 10);
        QCOMPARE(nested.value("second").toInt(), 20);
        QCOMPARE(nested.value("third").toMap().value("flag").toBool(), true);
        QCOMPARE(nested.value("unknownStored").toString(), "kept");
        QCOMPARE(effective->value("labels").toList(), QVariantList {"private"});
        QCOMPARE(effective->value("unknownRoot").toMap(),
                 (QVariantMap {
                     {"inherited", "retained"},
                     {  "private",    "added"}
        }));
        QCOMPARE(stored, storedSnapshot);
        QCOMPARE(overrides, overridesSnapshot);

        const auto baseline = registry.resolveConfiguration("defaults.fixture", {}, {}, &error);
        QVERIFY2(baseline.has_value(), qPrintable(error));
        QCOMPARE(baseline->value("nested").toMap().value("first").toInt(), 1);
        QCOMPARE(baseline->value("labels").toList(), QVariantList {"default"});
    }

    void invalidRegistrationThrowsWithoutReplacingExistingType()
    {
        ProcessorRegistry registry;
        const auto        valid = fixtureType("type: object\nproperties: {}");
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("", valid));

        auto invalid = valid;
        invalid.id   = "../invalid";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("fixture", invalid));
        invalid = valid;
        invalid.title.clear();
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("fixture", invalid));
        invalid        = valid;
        invalid.create = {};
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("fixture", invalid));
        invalid                = valid;
        invalid.settingsSchema = "invalid schema";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("fixture", invalid));
        QVERIFY(registry.types().isEmpty());

        registry.registerType("fixture", valid);
        invalid       = valid;
        invalid.title = "Replacement";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("another", invalid));
        QCOMPARE(registry.type(valid.id)->title, valid.title);
    }

    void unusableBaselineRegistrationThrowsWithoutInsertion()
    {
        ProcessorRegistry registry;
        const auto        missing = fixtureType(R"(type: object
required: [requiredValue]
properties:
  requiredValue: {type: string, minLength: 1}
)");
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("fixture", missing));
        QVERIFY(!registry.type("defaults.fixture"));

        const auto invalid = fixtureType(R"(type: object
properties:
  count: {type: integer, minimum: 1, default: 0}
)");
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, registry.registerType("fixture", invalid));
        QVERIFY(registry.types().isEmpty());
    }

    void invalidOverridesReportErrors()
    {
        ProcessorRegistry registry;
        registry.registerType("fixture", fixtureType(R"(type: object
required: [requiredValue]
properties:
  requiredValue: {type: string, minLength: 1, default: baseline}
  count: {type: integer, minimum: 1, default: 4}
)"));
        QString           error;
        const QVariantMap stored {
            {"requiredValue", "configured"}
        };
        QVERIFY2(registry.resolveConfiguration("defaults.fixture", stored, {}, &error).has_value(), qPrintable(error));

        QVERIFY(!registry.resolveConfiguration("defaults.fixture",
                                               stored,
                                               {
                                                   {"count", 0}
        },
                                               &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!registry.resolveConfiguration("defaults.fixture",
                                               stored,
                                               {
                                                   {"requiredValue", QVariant()}
        },
                                               &error));
        QVERIFY(!error.isEmpty());
    }

    void objectDefaultCombinesWithDescendantDefaults()
    {
        ProcessorRegistry registry;
        QString           error;
        registry.registerType("fixture", fixtureType(R"(type: object
properties:
  nested:
    type: object
    default: {first: 9}
    properties:
      first: {type: integer, default: 1}
      second: {type: integer, default: 2}
)"));

        const auto effective = registry.resolveConfiguration("defaults.fixture", {}, {}, &error);
        QVERIFY2(effective.has_value(), qPrintable(error));
        QCOMPARE(effective->value("nested").toMap().value("first").toInt(), 9);
        QCOMPARE(effective->value("nested").toMap().value("second").toInt(), 2);
    }
};

QTEST_GUILESS_MAIN(ProcessorDefaultsTests)
#include "ProcessorDefaultsTests.moc"
