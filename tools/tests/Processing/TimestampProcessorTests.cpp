#include "Nodes/Timestamp.h"

#include <QDateTime>
#include <QTest>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/SettingsSchema.h>

namespace
{
struct Result
{
    QList<Processor::Chunk> output;
    QString                 error;
    int                     completed = 0;
};

Result invoke(Processor& node, QByteArray bytes)
{
    Result     result;
    const auto accepted = node.accept({std::move(bytes)},
                                      {[&](auto chunk) { result.output.append(std::move(chunk)); },
                                       [&](auto error) {
                                           result.error = std::move(error);
                                           ++result.completed;
                                       }});
    if (accepted != Processor::AcceptResult::Accepted)
    {
        result.error = "Input rejected";
    }
    return result;
}
} // namespace

class TimestampProcessorTests final : public QObject
{
    Q_OBJECT
private slots:
    void defaultsAndCatalog()
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        const auto type = registry.type("native.timestamp");
        QVERIFY(type);
        QCOMPARE(type->group, QString("Timing"));
        QCOMPARE(type->inputCount, 1);
        QCOMPARE(type->outputCount, 1);
        auto node = registry.create("native.timestamp", {}, {});
        QVERIFY(node);
        SettingsSchema schema(Timestamp::schema());
        QVERIFY(schema.validate(node->configuration()).isEmpty());
        QCOMPARE(node->configuration().value("format").toString(), QString("yyyy-MM-dd HH:mm:ss.zzz"));
        QCOMPARE(node->configuration().value("timeZone").toString(), QString("local"));
        QCOMPARE(node->configuration().value("template").toString(), QString("[${timestamp}] ${packet}"));
    }

    void packetBoundariesAndBinaryOwnership()
    {
        for (const auto& position : {QString("prefix"), QString("suffix")})
        {
            auto node = Timestamp::create(
                {
                    {  "format",                                                               "'stamp:'"},
                    {"template", position == "prefix" ? "${timestamp}${packet}" : "${packet}${timestamp}"},
                    {   "extra",                                                                       42}
            },
                {});
            QVERIFY(node);
            QCOMPARE(node->configuration().value("extra").toInt(), 42);
            char             source[] {'a', '\0', char(255)};
            const QByteArray expected(source, 3);
            const auto       result = invoke(*node, QByteArray::fromRawData(source, 3));
            source[0]               = 'z';
            QVERIFY(result.error.isEmpty());
            QCOMPARE(result.completed, 1);
            QCOMPARE(result.output.size(), 1);
            QCOMPARE(result.output.first().port, 0);
            QCOMPARE(result.output.first().bytes, position == "prefix" ? "stamp:" + expected : expected + "stamp:");
            QCOMPARE(invoke(*node, "next").output.first().bytes,
                     position == "prefix" ? QByteArray("stamp:next") : QByteArray("nextstamp:"));
            QCOMPARE(invoke(*node, {}).output.first().bytes, QByteArray("stamp:"));
        }
        auto empty = Timestamp::create(
            {
                {"template", ""}
        },
            {});
        QVERIFY(empty);
        const auto result = invoke(*empty, {});
        QCOMPARE(result.output.size(), 1);
        QVERIFY(result.output.first().bytes.isEmpty());
        auto unicode = Timestamp::create(
            {
                {  "format", QString::fromUtf8("'Время: 'Щ")},
                {"template",         "${timestamp}${packet}"}
        },
            {});
        QVERIFY(unicode);
        QCOMPARE(invoke(*unicode, "A").output.first().bytes, QString::fromUtf8("Время: ЩA").toUtf8());
    }

    void templateColorsEscapesAndRepetition()
    {
        auto raw = Timestamp::create(
            {
                {"template", "${packet}"}
        },
            {});
        char       source[] {'x', '\0', char(255)};
        const auto copied = invoke(*raw, QByteArray::fromRawData(source, 3));
        source[0]         = 'y';
        QCOMPARE(copied.output.first().bytes, QByteArray("x\0\xff", 3));

        auto node = Timestamp::create(
            {
                {  "format",                                                         "'TIME'"},
                {"template", R"(\e[90m${timestamp}\x1b[0m ${packet}|${packet}\r\n\0\xFF\\$$)"}
        },
            {});
        QVERIFY(node);
        const QByteArray packet("${timestamp}\0\xff", 14);
        const auto       result = invoke(*node, packet);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.output.size(), 1);
        QCOMPARE(result.output.first().bytes,
                 QByteArray("\x1b[90mTIME\x1b[0m ") + packet + '|' + packet + QByteArray::fromHex("0d0a00ff5c24"));

        auto repeat = Timestamp::create(
            {
                {  "format",              "HH:mm:ss.zzz"},
                {"template", "${timestamp}|${timestamp}"}
        },
            {});
        const auto repeated = invoke(*repeat, {}).output.first().bytes.split('|');
        QCOMPARE(repeated.size(), 2);
        QCOMPARE(repeated.first(), repeated.last());

        Processor::Limits limits;
        limits.maxBytes = 4;
        auto bounded    = Timestamp::create(
            {
                {"template", "${packet}${packet}${packet}"}
        },
            limits);
        const auto oversized = invoke(*bounded, "AB");
        QVERIFY(!oversized.error.isEmpty());
        QVERIFY(oversized.output.isEmpty());
    }

    void utcWallClock()
    {
        auto node = Timestamp::create(
            {
                {  "format", "yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"},
                {"timeZone",                          "utc"},
                {"template",        "${timestamp}${packet}"}
        },
            {});
        const auto before = QDateTime::currentDateTimeUtc();
        const auto result = invoke(*node, "DATA");
        const auto after  = QDateTime::currentDateTimeUtc();
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.output.size(), 1);
        QVERIFY(result.output.first().bytes.endsWith("DATA"));
        const auto stamp = QDateTime::fromString(QString::fromUtf8(result.output.first().bytes.chopped(4)),
                                                 Qt::ISODateWithMs);
        QVERIFY(stamp.isValid());
        QVERIFY(stamp >= before);
        QVERIFY(stamp <= after);
    }

    void validationLimitsAndCancellation()
    {
        QString error;
        for (const auto& invalid : {QVariantMap {{"format", 12}},
                                    QVariantMap {{"format", QString(513, 'x')}},
                                    QVariantMap {{"timeZone", "wrong"}},
                                    QVariantMap {{"template", 12}},
                                    QVariantMap {{"template", "${unknown}"}},
                                    QVariantMap {{"template", "${packet"}},
                                    QVariantMap {{"template", "\\q"}},
                                    QVariantMap {{"template", "\\xG0"}},
                                    QVariantMap {{"template", QString(4097, 'x')}}})
        {
            QVERIFY(!Timestamp::create(invalid, {}, &error));
            QVERIFY(!error.isEmpty());
        }
        Processor::Limits limits;
        limits.maxBytes = 4;
        auto node       = Timestamp::create(
            {
                {"format", "'123'"}
        },
            limits);
        const auto result = invoke(*node, "AB");
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.output.isEmpty());
        QCOMPARE(result.completed, 1);
        QCOMPARE(node->accept({"late"}, {}), Processor::AcceptResult::Rejected);

        auto cancelled = Timestamp::create({}, {});
        bool done      = false;
        cancelled->cancel([&] { done = true; });
        QVERIFY(done);
        QCOMPARE(cancelled->accept({"late"}, {}), Processor::AcceptResult::Rejected);
    }
};

QTEST_GUILESS_MAIN(TimestampProcessorTests)
#include "TimestampProcessorTests.moc"
