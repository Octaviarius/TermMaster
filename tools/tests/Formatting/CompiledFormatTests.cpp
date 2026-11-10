#include <QTest>
#include <Termaster/Core/Formatting/CompiledFormat.h>
#include <future>
#include <stdexcept>

class CompiledFormatTests final : public QObject
{
    Q_OBJECT

private:
    Q_SLOT void independentUsageAndSchemaLifetime();
    Q_SLOT void cachedFormatsAndRepeatedValues();
    Q_SLOT void binaryUnicodeEscapesAndOwnership();
    Q_SLOT void invalidPatterns_data();
    Q_SLOT void invalidPatterns();
    Q_SLOT void limitsAndAtomicFailures();
    Q_SLOT void schemaValidation();
    Q_SLOT void concurrentRendering();
};

void CompiledFormatTests::independentUsageAndSchemaLifetime()
{
    auto       schema = std::make_unique<FormatSchema>();
    const auto number = schema->add<quint64>("number");
    const auto text   = schema->add<QString>("text");
    const auto first  = CompiledFormat::compile("{number}", *schema);
    const auto second = CompiledFormat::compile("{text}", *schema);
    QVERIFY(first);
    QVERIFY(second);
    QVERIFY(first->uses(number));
    QVERIFY(!first->uses(text));
    QVERIFY(!second->uses(number));
    QVERIFY(second->uses(text));

    auto values = schema->createValues();
    QVERIFY(values.set(number, quint64(42)));
    schema.reset();
    QByteArray output;
    QVERIFY(first->render(values, output, 100));
    QCOMPARE(output, QByteArray("42"));
    QString error;
    QVERIFY(!second->render(values, output, 100, &error));
    QVERIFY(error.contains("unset"));
    QCOMPARE(output, QByteArray("42"));
    QVERIFY(values.set(text, QStringLiteral("ready")));
    QVERIFY(second->render(values, output, 100));
    QCOMPARE(output, QByteArray("ready"));

    FormatSchema other;
    const auto   foreign = other.add<quint64>("number");
    QVERIFY(!first->uses(foreign));
    QVERIFY(!values.set(foreign, quint64(7), &error));
    auto foreignValues = other.createValues();
    QVERIFY(!first->render(foreignValues, output, 100, &error));
    QVERIFY(error.contains("another schema"));
    values.clear();
    QVERIFY(!first->render(values, output, 100, &error));
}

void CompiledFormatTests::cachedFormatsAndRepeatedValues()
{
    FormatSchema schema;
    const auto   number       = schema.add<quint64>("index");
    const auto   signedNumber = schema.add<qint64>("signed");
    const auto   real         = schema.add<double>("real");
    const auto   flag         = schema.add<bool>("flag");
    const auto   text         = schema.add<QString>("text");
    QString      error;
    const auto
        plan = CompiledFormat::compile("{index:06d}|{signed:+5d}|{real:.2f}|{real:.3g}|{real:.1e}|{flag}|{flag:d}|"
                                       "{text:>5.3}|{index:#x}|{index}",
                                       schema,
                                       &error);
    QVERIFY2(plan, qPrintable(error));
    auto values = schema.createValues();
    QVERIFY(values.set(number, quint64(42)));
    QVERIFY(values.set(signedNumber, qint64(-7)));
    QVERIFY(values.set(real, 12.345));
    QVERIFY(values.set(flag, true));
    QVERIFY(values.set(text, QStringLiteral("abcdef")));
    QByteArray output;
    QVERIFY2(plan->render(values, output, 1000, &error), qPrintable(error));
    QCOMPARE(output, QByteArray("000042|   -7|12.35|12.3|1.2e+01|true|1|  abc|0x2a|42"));
    QVERIFY(values.set(number, quint64(8)));
    QVERIFY(plan->render(values, output, 1000));
    QVERIFY(output.startsWith("000008|"));
    QVERIFY(output.endsWith("|0x8|8"));
}

void CompiledFormatTests::binaryUnicodeEscapesAndOwnership()
{
    FormatSchema schema;
    const auto   packet = schema.add<QByteArrayView>("packet");
    const auto   text   = schema.add<QString>("text");
    const auto   plan = CompiledFormat::compile(QString::fromUtf8("{{{text}}}\\e\\r\\n\\t\\0\\\\\\xFF{packet}{packet}"),
                                                schema);
    QVERIFY(plan);
    auto       values   = schema.createValues();
    char       raw[]    = {'\0', char(0xff), char(0xc3), 'A'};
    const auto borrowed = QByteArray::fromRawData(raw, sizeof(raw));
    QVERIFY(values.set(packet, QByteArrayView(borrowed)));
    QVERIFY(values.set(text, QString::fromUtf8("世界")));
    QByteArray output;
    QVERIFY(plan->render(values, output, 100));
    auto expected = QString::fromUtf8("{世界}").toUtf8();
    expected.append("\x1b\r\n\t\0\\", 6);
    expected.append(char(0xff));
    expected.append(raw, sizeof(raw));
    expected.append(raw, sizeof(raw));
    QCOMPARE(output, expected);
    values.clear();
    raw[1] = 'z';
    QCOMPARE(output, expected);

    const auto direct = CompiledFormat::compile("{packet}", schema);
    QVERIFY(values.set(packet, QByteArrayView(raw, sizeof(raw))));
    QVERIFY(direct->render(values, output, sizeof(raw)));
    raw[0] = 'q';
    QCOMPARE(output[0], '\0');
}

void CompiledFormatTests::invalidPatterns_data()
{
    QTest::addColumn<QString>("pattern");
    const QStringList patterns = {"{",
                                  "}",
                                  "{}",
                                  "{missing}",
                                  "{n:garbage}",
                                  "{n:.2f}",
                                  "{n:6dtrailing}",
                                  "{n:{n}}",
                                  "{n:>{n}}",
                                  "{n:4097}",
                                  "{real:.513f}",
                                  "{n:9999999999999999}",
                                  "\\",
                                  "\\z",
                                  "\\x",
                                  "\\x0",
                                  "\\xGG",
                                  "{packet:q}",
                                  "{text:d}"};
    for (const auto& pattern : patterns)
    {
        QTest::newRow(qPrintable(pattern)) << pattern;
    }
}

void CompiledFormatTests::invalidPatterns()
{
    QFETCH(QString, pattern);
    FormatSchema schema;
    schema.add<quint64>("n");
    schema.add<double>("real");
    schema.add<QByteArrayView>("packet");
    schema.add<QString>("text");
    QString error;
    QVERIFY(!CompiledFormat::compile(pattern, schema, &error));
    QVERIFY(!error.isEmpty());
}

void CompiledFormatTests::limitsAndAtomicFailures()
{
    FormatSchema schema;
    const auto   number = schema.add<quint64>("n");
    const auto   packet = schema.add<QByteArrayView>("packet");
    const auto   plan   = CompiledFormat::compile("prefix{n:4096}{packet}", schema);
    QVERIFY(plan);
    auto values = schema.createValues();
    QVERIFY(values.set(number, quint64(3)));
    const QByteArray bytes(10000, 'x');
    QVERIFY(values.set(packet, QByteArrayView(bytes)));
    QByteArray output("unchanged");
    QString    error;
    QVERIFY(!plan->render(values, output, 4096, &error));
    QCOMPARE(output, QByteArray("unchanged"));
    QVERIFY(error.contains("limit"));
    QVERIFY(!plan->render(values, output, -1, &error));
    QCOMPARE(output, QByteArray("unchanged"));
    QVERIFY(plan->render(values, output, 14102));
    QCOMPARE(output.size(), 14102);
    QVERIFY(!CompiledFormat::compile(QString(16385, 'a'), schema, &error));
    QVERIFY(!CompiledFormat::compile(QString(6000, QChar(0x4e16)), schema, &error));
    QVERIFY(!CompiledFormat::compile(QString("{n}").repeated(1025), schema, &error));
    QVERIFY(CompiledFormat::compile(QString("{n}").repeated(1024), schema, &error));
    const auto empty = CompiledFormat::compile("", schema);
    QVERIFY(empty->render(values, output, 0));
    QVERIFY(output.isEmpty());
    const auto precision = CompiledFormat::compile("{n:04096}", schema, &error);
    QVERIFY2(precision, qPrintable(error));
}

void CompiledFormatTests::schemaValidation()
{
    FormatSchema schema;
    schema.add<qint64>("first");
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, schema.add<bool>("first"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, schema.add<bool>("1bad"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, schema.add<bool>("bad-name"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, schema.add<bool>(""));
    schema.createValues();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, schema.add<bool>("late"));

    FormatSchema bounded;
    for (int index = 0; index < 256; ++index)
    {
        bounded.add<bool>(QString("value%1").arg(index));
    }
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, bounded.add<bool>("overflow"));
    QVERIFY(CompiledFormat::compile("{value255}", bounded));
}

void CompiledFormatTests::concurrentRendering()
{
    FormatSchema schema;
    const auto   number = schema.add<quint64>("n");
    const auto   plan   = CompiledFormat::compile("{n:08d}:{n:x}", schema);
    QVERIFY(plan);
    auto left  = schema.createValues();
    auto right = schema.createValues();
    QVERIFY(left.set(number, quint64(42)));
    QVERIFY(right.set(number, quint64(255)));
    const auto run = [plan](FormatSchema::Values values, const QByteArray& expected) {
        for (int index = 0; index < 1000; ++index)
        {
            QByteArray output;
            if (!plan->render(values, output, 100) || output != expected)
            {
                return false;
            }
        }
        return true;
    };
    auto worker = std::async(std::launch::async, run, left, QByteArray("00000042:2a"));
    QVERIFY(run(right, QByteArray("00000255:ff")));
    QVERIFY(worker.get());
}

QTEST_GUILESS_MAIN(CompiledFormatTests)
#include "CompiledFormatTests.moc"
