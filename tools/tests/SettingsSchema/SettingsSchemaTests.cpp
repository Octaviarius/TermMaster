#include <QtTest>
#include <Termaster/Core/SettingsSchema.h>
#include <limits>
#include <stdexcept>

namespace
{
QByteArray descriptor(const QByteArray& content)
{
    QByteArray bytes("version: 1\ntype: termaster.settings-schema\ndescription: Test form\ncontent:\n");
    for (const auto& line : content.split('\n'))
    {
        bytes += "  " + line + '\n';
    }
    return bytes;
}
} // namespace

class SettingsSchemaTests : public QObject
{
    Q_OBJECT
private slots:
    void typedValues()
    {
        auto value = SettingsSchema::parseValue("{number: 17, text: '17', flag: true, nullValue: null, real: 1.5}")
                         .toMap();
        QCOMPARE(value.value("number").typeId(), QMetaType::LongLong);
        QCOMPARE(value.value("text").typeId(), QMetaType::QString);
        QCOMPARE(value.value("flag").typeId(), QMetaType::Bool);
        QVERIFY(!value.value("nullValue").isValid());
        QCOMPARE(value.value("real").typeId(), QMetaType::Double);
        QCOMPARE(SettingsSchema::parseValue("18446744073709551615").toULongLong(), std::numeric_limits<quint64>::max());
        QCOMPARE(SettingsSchema::parseValue("9007199254740993").toLongLong(), qint64(9007199254740993LL));
        QCOMPARE(SettingsSchema::parseValue(SettingsSchema::serializeValue(value)).toMap(), value);
        QVERIFY_EXCEPTION_THROWN(SettingsSchema::parseValue("18446744073709551616"), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(SettingsSchema::parseValue("{a: 1, a: 2}"), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(SettingsSchema::parseValue("---\n1\n---\n2"), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(SettingsSchema::parseValue("&self [*self]"), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(SettingsSchema::parseValue(QByteArray(70, '[') + "0" + QByteArray(70, ']')),
                                 std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(SettingsSchema::parseValue(QByteArray(4 * 1024 * 1024 + 1, 'a')),
                                 std::invalid_argument);
    }

    void annotationsAndReferences()
    {
        SettingsSchema schema(descriptor(R"(type: object
required: [port]
additionalProperties: false
x-groups: [{id: connection, title: Connection, order: 2}]
definitions:
  port: {type: integer, minimum: 1, maximum: 65535, default: 22}
properties:
  port:
    $ref: '#/definitions/port'
    title: Port
    x-settings-path: transport/port
    x-group: connection
    x-widget: integer
    x-order: 1
    x-widget-options: {suffix: ' baud'}
  label: {type: string, readOnly: true, x-group: connection, x-order: 2}
)"));
        QCOMPARE(schema.fields().size(), 2);
        QCOMPARE(schema.fields().front().key, "port");
        QCOMPARE(schema.fields().front().settingsPath, "transport/port");
        QVERIFY(schema.fields().front().required);
        QVERIFY(schema.fields().front().hasDefault);
        QCOMPARE(schema.fields().front().defaultValue.toInt(), 22);
        QCOMPARE(schema.fields().front().presentation.value("suffix").toString(), " baud");
        QCOMPARE(schema.groups().front().title, "Connection");
        QVERIFY(schema
                    .validate({
                        {"port", 22}
        })
                    .isEmpty());
        QVERIFY(!schema
                     .validate({
                         {"port", "22"}
        })
                     .isEmpty());
        QVERIFY(!schema
                     .validate({
                         {"port", 0}
        })
                     .isEmpty());
        QVERIFY(!schema.validate({}).isEmpty());
        const auto errors = schema.validate({
            {"port", "password-secret-value"}
        });
        QVERIFY(!errors.isEmpty());
        for (const auto& error : errors)
        {
            QVERIFY(!error.message.contains("password-secret-value"));
            QVERIFY(error.path.isEmpty() || error.path.startsWith('/'));
        }
    }

    void exactIntegersAndLimits()
    {
        SettingsSchema schema(descriptor("type: object\nproperties:\n  n: {type: integer, const: 9007199254740991}\n"));
        QVERIFY(schema
                    .validate({
                        {"n", qint64(9007199254740991LL)}
        })
                    .isEmpty());
        QVERIFY(!schema
                     .validate({
                         {"n", qint64(9007199254740992LL)}
        })
                     .isEmpty());
        QVERIFY(!schema
                     .validate({
                         {"n", qint64(9007199254740993LL)}
        })
                     .isEmpty());
        QVERIFY(!schema
                     .validate({
                         {"n", double(9007199254740992.0)}
        })
                     .isEmpty());
        QVERIFY(!schema
                     .validate({
                         {"n", std::numeric_limits<quint64>::max()}
        })
                     .isEmpty());
        QVERIFY_EXCEPTION_THROWN(SettingsSchema(descriptor("type: object\nproperties: {n: {const: 9007199254740993}}")),
                                 std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(SettingsSchema(
                                     descriptor("type: object\nproperties: {n: {enum: [9007199254740993]}}")),
                                 std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(SettingsSchema(
                                     descriptor("type: object\nproperties: {n: {const: 9007199254740992.0}}")),
                                 std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(SettingsSchema(
                                     descriptor("type: object\nproperties: {n: {maximum: 18446744073709551615}}")),
                                 std::invalid_argument);
    }

    void arithmeticPrecisionLimits()
    {
        SettingsSchema schema(descriptor("type: object\nproperties: {n: {type: integer, maximum: 9007199254740991}}"));
        QVERIFY(schema
                    .validate({
                        {"n", qint64(9007199254740991LL)}
        })
                    .isEmpty());
        QVERIFY(!schema
                     .validate({
                         {"n", qint64(9007199254740992LL)}
        })
                     .isEmpty());
        QVERIFY(!schema
                     .validate({
                         {"n", qint64(9007199254740993LL)}
        })
                     .isEmpty());
        QVERIFY_EXCEPTION_THROWN(SettingsSchema(
                                     descriptor("type: object\nproperties: {n: {maximum: 9007199254740992}}")),
                                 std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(SettingsSchema(
                                     descriptor("type: object\nproperties: {n: {minimum: -9007199254740992}}")),
                                 std::invalid_argument);
    }

    void invalidDescriptors_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("wrong keyword type")
            << QByteArray("type: object\nproperties: {n: {type: integer, minimum: bad}}");
        QTest::newRow("bad annotation") << QByteArray("type: object\nproperties: {n: {type: string, x-widget: 2}}");
        QTest::newRow("nested ui rejected")
            << QByteArray("type: object\nproperties: {n: {type: string, x-ui: {widget: text}}}");
        QTest::newRow("bad widget options")
            << QByteArray("type: object\nproperties: {n: {type: string, x-widget-options: text}}");
        QTest::newRow("bad order") << QByteArray("type: object\nproperties: {n: {type: string, x-order: 0.5}}");
        QTest::newRow("external ref") << QByteArray(
            "type: object\nproperties: {n: {$ref: 'https://example.invalid/schema'}}");
        QTest::newRow("cycle") << QByteArray(
            "type: object\ndefinitions: {n: {$ref: '#/definitions/n'}}\nproperties: {n: {$ref: '#/definitions/n'}}");
        QTest::newRow("ancestor cycle") << QByteArray("type: object\nproperties: {n: {$ref: '#'}}");
        QTest::newRow("path collision") << QByteArray(
            "type: object\nproperties: {a: {x-settings-path: branch}, b: {x-settings-path: branch-other}, c: {x-settings-path: branch/child}}");
        QTest::newRow("duplicate path") << QByteArray(
            "type: object\nproperties: {a: {x-settings-path: branch}, b: {x-settings-path: branch}}");
        QTest::newRow("invalid path") << QByteArray("type: object\nproperties: {a: {x-settings-path: '../branch'}}");
    }

    void invalidDescriptors()
    {
        QFETCH(QByteArray, content);
        QVERIFY_EXCEPTION_THROWN(SettingsSchema(descriptor(content)), std::invalid_argument);
    }
};

QTEST_GUILESS_MAIN(SettingsSchemaTests)
#include "SettingsSchemaTests.moc"
