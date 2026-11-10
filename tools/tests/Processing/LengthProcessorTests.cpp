#include "Nodes/LengthField.h"
#include "Nodes/LengthUnwrap.h"
#include "Nodes/LengthWrap.h"

#include <QComboBox>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <Termaster/Core/Gui/Settings/SettingsFormWidget.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <limits>

namespace
{
struct Result
{
    Processor::AcceptResult accepted = Processor::AcceptResult::Rejected;
    QList<Processor::Chunk> output;
    QString                 error;
    int                     completed = 0;
};
Result invoke(Processor& processor, QByteArray bytes)
{
    Result result;
    result.accepted = processor.accept({std::move(bytes)},
                                       {[&](auto chunk) { result.output.append(std::move(chunk)); },
                                        [&](QString error) {
                                            result.error = std::move(error);
                                            ++result.completed;
                                        }});
    return result;
}
} // namespace

class LengthProcessorTests final : public QObject
{
    Q_OBJECT
private:
    ProcessorRegistry _registry;

    std::shared_ptr<Processor> create(const char*              id,
                                      const QVariantMap&       configuration = {},
                                      const Processor::Limits& limits        = {})
    {
        QString error;
        auto    node = _registry.create(QString::fromLatin1(id), configuration, limits, &error);
        if (!node)
        {
            qWarning().noquote() << error;
        }
        return node;
    }

    Q_SLOT void initTestCase()
    {
        registerNativeProcessors(_registry, "native");
    }
    Q_SLOT void defaultsAndCatalog()
    {
        for (const auto* id : {"native.length-wrap", "native.length-unwrap"})
        {
            const auto type = _registry.type(id);
            QVERIFY(type);
            QCOMPARE(type->group, QString("Framing"));
            QCOMPARE(type->inputCount, 1);
            QCOMPARE(type->outputCount, 1);
            auto node = create(id,
                               {
                                   {"unknown", QVariantMap {{"retained", 42}}}
            });
            QVERIFY(node);
            const auto config = node->configuration();
            QCOMPARE(config.value("encoding").toString(), QString("fixed"));
            QCOMPARE(config.value("position").toString(), QString("prefix"));
            QCOMPARE(config.value("width").toInt(), 2);
            QCOMPARE(config.value("endianness").toString(), QString("little"));
            QCOMPARE(config.value("includeField").toBool(), false);
            QCOMPARE(config.value("adjustment").toInt(), 0);
            QCOMPARE(config.value("unknown").toMap().value("retained").toInt(), 42);
            SettingsSchema schema(type->settingsSchema);
            QVERIFY(schema.validate(config).isEmpty());
        }
        auto node = create("native.length-wrap");
        QCOMPARE(invoke(*node, QByteArray("A\0B", 3)).output.first().bytes, QByteArray::fromHex("0300410042"));
    }
    Q_SLOT void fixedEndianVectors()
    {
        const auto              bytes  = QByteArray::fromHex("4100ff");
        const QList<QByteArray> little = {QByteArray::fromHex("03"),
                                          QByteArray::fromHex("0300"),
                                          QByteArray::fromHex("030000"),
                                          QByteArray::fromHex("03000000"),
                                          QByteArray::fromHex("0300000000"),
                                          QByteArray::fromHex("030000000000"),
                                          QByteArray::fromHex("03000000000000"),
                                          QByteArray::fromHex("0300000000000000")};
        const QList<QByteArray> big    = {QByteArray::fromHex("03"),
                                          QByteArray::fromHex("0003"),
                                          QByteArray::fromHex("000003"),
                                          QByteArray::fromHex("00000003"),
                                          QByteArray::fromHex("0000000003"),
                                          QByteArray::fromHex("000000000003"),
                                          QByteArray::fromHex("00000000000003"),
                                          QByteArray::fromHex("0000000000000003")};
        int                     index  = 0;
        for (const auto width : {1, 2, 3, 4, 5, 6, 7, 8})
        {
            for (const auto* order : {"little", "big"})
            {
                for (const auto* position : {"prefix", "suffix"})
                {
                    const QVariantMap config {
                        {     "width",    width},
                        {"endianness",    order},
                        {  "position", position}
                    };
                    const auto field    = QString(order) == "little" ? little[index] : big[index];
                    const auto expected = QString(position) == "prefix" ? field + bytes : bytes + field;
                    auto       wrap     = create("native.length-wrap", config);
                    auto       unwrap   = create("native.length-unwrap", config);
                    const auto result   = invoke(*wrap, bytes);
                    QCOMPARE(result.completed, 1);
                    QVERIFY(result.error.isEmpty());
                    QCOMPARE(result.output.size(), 1);
                    QCOMPARE(result.output.first().bytes, expected);
                    QCOMPARE(result.output.first().port, 0);
                    const auto decoded = invoke(*unwrap, expected);
                    QVERIFY(decoded.error.isEmpty());
                    QCOMPARE(decoded.output.size(), 1);
                    QCOMPARE(decoded.output.first().bytes, bytes);
                }
            }
            ++index;
        }
        // Every byte position is significant, independently of the small payload vectors.
        auto wrap = create("native.length-wrap",
                           {
                               {     "width",        8},
                               {"endianness",    "big"},
                               {"adjustment", 16909060}
        });
        QCOMPARE(invoke(*wrap, {}).output.first().bytes, QByteArray::fromHex("0000000001020304"));
        wrap = create("native.length-wrap",
                      {
                          {     "width",        8},
                          {"adjustment", 16909060}
        });
        QCOMPARE(invoke(*wrap, {}).output.first().bytes, QByteArray::fromHex("0403020100000000"));
    }
    Q_SLOT void packetMatrixAndAdjustments()
    {
        for (const auto width : {1, 2, 3, 4, 5, 6, 7, 8})
        {
            for (const auto* order : {"little", "big"})
            {
                for (const auto* position : {"prefix", "suffix"})
                {
                    for (const auto include : {false, true})
                    {
                        for (const auto adjustment : {0, 7, -2})
                        {
                            const QVariantMap config {
                                {       "width",      width},
                                {  "endianness",      order},
                                {    "position",   position},
                                {"includeField",    include},
                                {  "adjustment", adjustment}
                            };
                            auto wrap   = create("native.length-wrap", config);
                            auto unwrap = create("native.length-unwrap", config);
                            QVERIFY(wrap && unwrap);
                            for (const auto& bytes : {QByteArray::fromHex("000102ff004142"), QByteArray("next packet")})
                            {
                                const auto packed = invoke(*wrap, bytes);
                                QVERIFY2(packed.error.isEmpty(), qPrintable(packed.error));
                                QCOMPARE(packed.output.size(), 1);
                                const auto decoded = invoke(*unwrap, packed.output.first().bytes);
                                QVERIFY2(decoded.error.isEmpty(), qPrintable(decoded.error));
                                QCOMPARE(decoded.output.size(), 1);
                                QCOMPARE(decoded.output.first().bytes, bytes);
                            }
                        }
                    }
                }
            }
        }
        for (const auto include : {false, true})
        {
            auto       wrap   = create("native.length-wrap",
                                       {
                                           {"includeField", include}
            });
            const auto packed = invoke(*wrap, {});
            QCOMPARE(packed.output.first().bytes, QByteArray::fromHex(include ? "0200" : "0000"));
            auto       unwrap  = create("native.length-unwrap",
                                        {
                                            {"includeField", include}
            });
            const auto decoded = invoke(*unwrap, packed.output.first().bytes);
            QVERIFY(decoded.error.isEmpty());
            QCOMPARE(decoded.output.size(), 1);
            QVERIFY(decoded.output.first().bytes.isEmpty());
        }
        auto wrap = create("native.length-wrap",
                           {
                               {"includeField", true},
                               {  "adjustment",   -2}
        });
        QCOMPARE(invoke(*wrap, {}).output.first().bytes, QByteArray::fromHex("0000"));
    }
    Q_SLOT void widthAndAdjustmentBounds()
    {
        auto       wrap   = create("native.length-wrap",
                                   {
                                       {"width", 1}
        });
        const auto packed = invoke(*wrap, QByteArray(255, 'x'));
        QVERIFY(packed.error.isEmpty());
        QCOMPARE(packed.output.first().bytes.at(0), char(255));
        const auto overflow = invoke(*wrap, QByteArray(256, 'x'));
        QVERIFY(!overflow.error.isEmpty());
        QVERIFY(overflow.output.isEmpty());
        QCOMPARE(overflow.completed, 1);
        QCOMPARE(invoke(*wrap, {}).accepted, Processor::AcceptResult::Rejected);
        wrap = create("native.length-wrap",
                      {
                          {"width", 2}
        });
        QCOMPARE(invoke(*wrap, QByteArray(256, 'x')).output.first().bytes.left(2), QByteArray::fromHex("0001"));
        wrap = create("native.length-wrap",
                      {
                          {       "width",    1},
                          {"includeField", true}
        });
        QVERIFY(invoke(*wrap, QByteArray(254, 'x')).error.isEmpty());
        QVERIFY(!invoke(*wrap, QByteArray(255, 'x')).error.isEmpty());
        wrap                = create("native.length-wrap",
                                     {
                                         {"adjustment", -1}
        });
        const auto negative = invoke(*wrap, {});
        QVERIFY(!negative.error.isEmpty());
        QVERIFY(negative.output.isEmpty());
        wrap = create("native.length-wrap",
                      {
                          {     "width",          4},
                          {"adjustment", 2147483647}
        });
        QCOMPARE(invoke(*wrap, {}).output.first().bytes, QByteArray::fromHex("ffffff7f"));
        wrap = create("native.length-wrap",
                      {
                          {     "width",   1},
                          {"adjustment", 255}
        });
        QCOMPARE(invoke(*wrap, {}).output.first().bytes, QByteArray::fromHex("ff"));
        QVERIFY(!invoke(*wrap, "x").error.isEmpty());
    }
    Q_SLOT void malformedPacketsAndNoAccumulation()
    {
        for (const auto& packet : {QByteArray {},
                                   QByteArray::fromHex("00"),
                                   QByteArray::fromHex("020041"),
                                   QByteArray::fromHex("000041"),
                                   QByteArray::fromHex("01004142")})
        {
            auto       unwrap = create("native.length-unwrap");
            const auto result = invoke(*unwrap, packet);
            QCOMPARE(result.accepted, Processor::AcceptResult::Accepted);
            QCOMPARE(result.completed, 1);
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.output.isEmpty());
            QCOMPARE(invoke(*unwrap, QByteArray::fromHex("010041")).accepted, Processor::AcceptResult::Rejected);
        }
        for (const auto* order : {"little", "big"})
        {
            for (const auto* position : {"prefix", "suffix"})
            {
                for (const auto adjustment : {-2147483647, 0, 2147483647})
                {
                    auto       unwrap = create("native.length-unwrap",
                                               {
                                                   {       "width",          8},
                                                   {  "endianness",      order},
                                                   {    "position",   position},
                                                   {"includeField",       true},
                                                   {  "adjustment", adjustment}
                    });
                    const auto result = invoke(*unwrap, QByteArray(8, char(255)));
                    QVERIFY(!result.error.isEmpty());
                    QVERIFY(result.output.isEmpty());
                }
            }
        }
        auto unwrap = create("native.length-unwrap",
                             {
                                 {"adjustment", -1}
        });
        QVERIFY(!invoke(*unwrap, QByteArray::fromHex("0000")).error.isEmpty());
        unwrap = create("native.length-unwrap",
                        {
                            {"width", 1}
        });
        QVERIFY(!invoke(*unwrap, QByteArray::fromHex("00") + QByteArray(256, 'x')).error.isEmpty());
    }
    Q_SLOT void limitsCancellationAndOwnedOutput()
    {
        for (const auto* id : {"native.length-wrap", "native.length-unwrap"})
        {
            Processor::Limits limits;
            limits.maxBytes = 4;
            auto node       = create(id, {}, limits);
            QCOMPARE(invoke(*node, "12345").accepted, Processor::AcceptResult::Rejected);
            QCOMPARE(node->accept({{}, 1}, {}), Processor::AcceptResult::Rejected);
            bool ready = false;
            node->cancel([&] { ready = true; });
            QVERIFY(ready);
            QCOMPARE(invoke(*node, {}).accepted, Processor::AcceptResult::Rejected);
            limits.maxSteps   = 1;
            node              = create(id, {}, limits);
            const auto result = invoke(*node, QByteArray::fromHex("0000"));
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.output.isEmpty());
        }
        Processor::Limits limits;
        limits.maxBytes = 4;
        auto wrap       = create("native.length-wrap", {}, limits);
        QVERIFY(invoke(*wrap, "12").error.isEmpty());
        const auto expansion = invoke(*wrap, "123");
        QVERIFY(!expansion.error.isEmpty());
        QVERIFY(expansion.output.isEmpty());
        limits.maxBytes = 1;
        wrap            = create("native.length-wrap", {}, limits);
        QVERIFY(!invoke(*wrap, {}).error.isEmpty());

        char source[] = {'A', '\0', char(255)};
        wrap          = create("native.length-wrap");
        auto packet   = invoke(*wrap, QByteArray::fromRawData(source, 3)).output.first().bytes;
        source[0]     = 'B';
        QCOMPARE(packet, QByteArray::fromHex("03004100ff"));
        auto unwrap = create("native.length-unwrap");
        const auto
            decoded = invoke(*unwrap, QByteArray::fromRawData(packet.data(), packet.size())).output.first().bytes;
        packet.fill('x');
        QVERIFY(invoke(*unwrap, QByteArray::fromHex("0000")).error.isEmpty());
        QCOMPARE(decoded, QByteArray::fromHex("4100ff"));
    }
    Q_SLOT void invalidConfiguration()
    {
        const QList<QVariantMap> invalid = {
            {{"position", "middle"}},
            {{"position", 1}},
            {{"endianness", "native"}},
            {{"includeField", 1}},
            {{"width", 0}},
            {{"width", 9}},
            {{"encoding", "unknown"}},
            {{"encoding", 1}},
            {{"encoding", "varint"}, {"maxBytes", 0}},
            {{"encoding", "varint"}, {"maxBytes", 11}},
            {{"encoding", "varint"}, {"maxBytes", true}},
            {{"encoding", "varint"}, {"maxBytes", "2"}},
            {{"encoding", "varint"}, {"maxBytes", 2.5}},
            {{"width", 16}},
            {{"width", "2"}},
            {{"width", true}},
            {{"width", 2.5}},
            {{"adjustment", "1"}},
            {{"adjustment", false}},
            {{"adjustment", 1.5}},
            {{"adjustment", std::numeric_limits<double>::infinity()}},
            {{"adjustment", std::numeric_limits<double>::quiet_NaN()}},
            {{"adjustment", qint64(-2147483648LL)}},
            {{"adjustment", quint64(2147483648ULL)}},
            {{"adjustment", std::numeric_limits<quint64>::max()}}
        };
        for (const auto& config : invalid)
        {
            QString error;
            QVERIFY(!LengthWrap::create(config, {}, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(!LengthUnwrap::create(config, {}, &error));
            QVERIFY(!error.isEmpty());
        }
        for (const QVariant width : {QVariant(2),
                                     QVariant(uint(2)),
                                     QVariant(qint64(2)),
                                     QVariant(quint64(2)),
                                     QVariant(2.0),
                                     QVariant(float(2))})
        {
            QString error;
            auto    node = LengthWrap::create(
                {
                    {"width", width}
            },
                {},
                &error);
            QVERIFY2(node, qPrintable(error));
            QCOMPARE(node->configuration().value("width").typeId(), QMetaType::Int);
        }
        Processor::Limits limits;
        limits.maxSteps = 0;
        QString error;
        QVERIFY(!LengthWrap::create({}, limits, &error));
        QVERIFY(!LengthUnwrap::create({}, limits, &error));
        limits          = {};
        limits.maxBytes = 0;
        QVERIFY(!LengthWrap::create({}, limits, &error));
        QVERIFY(!LengthUnwrap::create({}, limits, &error));
    }
    Q_SLOT void varintIndependentVectors()
    {
        const QList<quint64>    values  = {0, 127, 128, 16383, 16384, std::numeric_limits<quint64>::max()};
        const QList<QByteArray> vectors = {QByteArray::fromHex("00"),
                                           QByteArray::fromHex("7f"),
                                           QByteArray::fromHex("8001"),
                                           QByteArray::fromHex("ff7f"),
                                           QByteArray::fromHex("808001"),
                                           QByteArray::fromHex("ffffffffffffffffff01")};
        const LengthField       field({
            {"encoding", "varint"},
            {"maxBytes",       10}
        });
        for (int index = 0; index < values.size(); ++index)
        {
            QCOMPARE(field.encode(values[index]), vectors[index]);
            quint64 decoded = 0;
            int     width   = 0;
            QString error;
            QVERIFY2(field.decode(vectors[index], decoded, width, 10, error), qPrintable(error));
            QCOMPARE(decoded, values[index]);
            QCOMPARE(width, vectors[index].size());
            if (index == values.size() - 1)
            {
                continue;
            }
            const QByteArray  payload(qsizetype(values[index]), 'x');
            const QVariantMap config {
                {"encoding", "varint"}
            };
            auto wrap   = create("native.length-wrap", config);
            auto unwrap = create("native.length-unwrap", config);
            QCOMPARE(invoke(*wrap, payload).output.first().bytes, vectors[index] + payload);
            QCOMPARE(invoke(*unwrap, vectors[index] + payload).output.first().bytes, payload);
        }
        for (const auto width : {3, 5, 6, 7})
        {
            const LengthField fixed({
                {     "width",    width},
                {  "position", "prefix"},
                {"endianness",    "big"}
            });
            const auto        maximum = (quint64(1) << (width * 8)) - 1;
            QCOMPARE(fixed.encode(maximum), QByteArray(width, char(255)));
            quint64 decoded  = 0;
            int     observed = 0;
            QString error;
            QVERIFY(fixed.decode(QByteArray(width, char(255)), decoded, observed, width, error));
            QCOMPARE(decoded, maximum);
            QVERIFY(!fixed.value(qsizetype(maximum + 1), decoded, observed, error));
        }
        auto wrap = create("native.length-wrap",
                           {
                               {     "width",        3},
                               {"adjustment", 16777215}
        });
        QCOMPARE(invoke(*wrap, {}).output.first().bytes, QByteArray::fromHex("ffffff"));
        QVERIFY(!invoke(*wrap, "x").error.isEmpty());
    }
    Q_SLOT void varintFieldCountingAndAdjustments()
    {
        for (const auto adjustment : {-7, -1, 0, 1, 7})
        {
            for (const auto include : {false, true})
            {
                for (const auto size : {8, 125, 126, 127, 128, 16380, 16381, 16382, 16383, 16384})
                {
                    const QVariantMap config {
                        {    "encoding",   "varint"},
                        {"includeField",    include},
                        {  "adjustment", adjustment}
                    };
                    auto             wrap   = create("native.length-wrap", config);
                    auto             unwrap = create("native.length-unwrap", config);
                    const QByteArray payload(size, 'x');
                    const auto       packed = invoke(*wrap, payload);
                    QVERIFY2(packed.error.isEmpty(), qPrintable(packed.error));
                    const auto decoded = invoke(*unwrap, packed.output.first().bytes);
                    QVERIFY2(decoded.error.isEmpty(), qPrintable(decoded.error));
                    QCOMPARE(decoded.output.first().bytes, payload);
                }
            }
        }
        const QVariantMap config {
            {    "encoding", "varint"},
            {"includeField",     true}
        };
        for (const auto& example : QList<QPair<int, QByteArray>> {
                 {  126,     QByteArray::fromHex("7f")},
                 {  127,   QByteArray::fromHex("8101")},
                 {16381,   QByteArray::fromHex("ff7f")},
                 {16382, QByteArray::fromHex("818001")}
        })
        {
            auto wrap = create("native.length-wrap", config);
            QCOMPARE(invoke(*wrap, QByteArray(example.first, 'x')).output.first().bytes,
                     example.second + QByteArray(example.first, 'x'));
        }
        for (const auto& example : QList<QPair<int, QByteArray>> {
                 {  126,   QByteArray::fromHex("8001")},
                 {16381, QByteArray::fromHex("808001")}
        })
        {
            auto       unwrap = create("native.length-unwrap", config);
            const auto result = invoke(*unwrap, example.second + QByteArray(example.first, 'x'));
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.output.isEmpty());
        }
        for (const auto* id : {"native.length-wrap", "native.length-unwrap"})
        {
            auto       node   = create(id,
                                       {
                                           {    "encoding", "varint"},
                                           {"includeField",     true},
                                           {  "adjustment",       -2}
            });
            const auto result = invoke(*node,
                                       QString(id).endsWith("unwrap") ? QByteArray::fromHex("00") : QByteArray {});
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.output.isEmpty());
        }
        auto wrap = create("native.length-wrap",
                           {
                               {    "encoding", "varint"},
                               {"includeField",     true},
                               {  "adjustment",       -1}
        });
        QCOMPARE(invoke(*wrap, {}).output.first().bytes, QByteArray::fromHex("00"));
    }
    Q_SLOT void varintMalformedBoundsAndNormalization()
    {
        for (const auto& packet : {QByteArray {},
                                   QByteArray::fromHex("80"),
                                   QByteArray::fromHex("8000"),
                                   QByteArray::fromHex("8100"),
                                   QByteArray::fromHex("ffffffffffffffffff02"),
                                   QByteArray::fromHex("ffffffffffffffffff81"),
                                   QByteArray(11, char(0x80)),
                                   QByteArray::fromHex("014142"),
                                   QByteArray::fromHex("0241")})
        {
            auto       unwrap = create("native.length-unwrap",
                                       {
                                           {"encoding", "varint"}
            });
            const auto result = invoke(*unwrap, packet);
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.output.isEmpty());
            QCOMPARE(result.completed, 1);
            QCOMPARE(invoke(*unwrap, QByteArray::fromHex("00")).accepted, Processor::AcceptResult::Rejected);
        }
        // Each complete call is independent; an incomplete prefix cannot be resumed.
        auto unwrap = create("native.length-unwrap",
                             {
                                 {"encoding", "varint"}
        });
        QVERIFY(!invoke(*unwrap, QByteArray::fromHex("80")).error.isEmpty());
        QCOMPARE(invoke(*unwrap, QByteArray::fromHex("01")).accepted, Processor::AcceptResult::Rejected);
        for (int cap = 1; cap <= 10; ++cap)
        {
            const LengthField field({
                {"encoding", "varint"},
                {"maxBytes",      cap}
            });
            for (int width = 1; width <= 10; ++width)
            {
                const quint64 value    = width == 1 ? 0 : quint64(1) << ((width - 1) * 7);
                const auto    bytes    = field.encode(value);
                quint64       decoded  = 0;
                int           observed = 0;
                QString       error;
                QCOMPARE(field.decode(bytes, decoded, observed, 10, error), width <= cap);
                if (width <= cap)
                {
                    QCOMPARE(decoded, value);
                }
                error.clear();
                QCOMPARE(field.decode(bytes, decoded, observed, width - 1, error), false);
            }
        }
        for (const auto* id : {"native.length-wrap", "native.length-unwrap"})
        {
            QString error;
            auto    direct = QString(id).endsWith("unwrap") ? LengthUnwrap::create(
                                                                  {
                                                                      {  "encoding",   "varint"},
                                                                      {     "width", "inactive"},
                                                                      {  "position",      false},
                                                                      {"endianness",          9},
                                                                      {   "unknown",         42}
            },
                                                                  {},
                                                                  &error) :
                                                              LengthWrap::create({{"encoding", "varint"},
                                                                                  {"width", "inactive"},
                                                                                  {"position", false},
                                                                                  {"endianness", 9},
                                                                                  {"unknown", 42}},
                                                                                 {},
                                                                                 &error);
            QVERIFY2(direct, qPrintable(error));
            const auto config = direct->configuration();
            QCOMPARE(config.value("unknown").toInt(), 42);
            QCOMPARE(config.value("maxBytes").toInt(), 10);
            QVERIFY(!config.contains("width") && !config.contains("position") && !config.contains("endianness"));
            auto node = create(id,
                               {
                                   {"maxBytes", "inactive"}
            });
            QVERIFY(node && !node->configuration().contains("maxBytes"));
            Processor::Limits limits;
            limits.maxSteps   = 1;
            node              = create(id,
                                       {
                                           {"encoding", "varint"}
            },
                                       limits);
            const auto result = invoke(*node,
                                       QString(id).endsWith("unwrap") ?
                                           QByteArray::fromHex("8001") + QByteArray(128, 'x') :
                                           QByteArray(128, 'x'));
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.output.isEmpty());
        }
        for (const auto cap : {1, 2})
        {
            auto wrap = create("native.length-wrap",
                               {
                                   {"encoding", "varint"},
                                   {"maxBytes",      cap}
            });
            QVERIFY(!invoke(*wrap, QByteArray(cap == 1 ? 128 : 16384, 'x')).error.isEmpty());
        }
        Processor::Limits limits;
        limits.maxBytes = 129;
        auto wrap       = create("native.length-wrap",
                                 {
                                     {"encoding", "varint"}
        },
                                 limits);
        QVERIFY(invoke(*wrap, QByteArray(127, 'x')).error.isEmpty());
        const auto result = invoke(*wrap, QByteArray(128, 'x'));
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.output.isEmpty());
    }
    Q_SLOT void generatedSettings()
    {
        QTemporaryDir          directory;
        Settings               owner(directory.filePath("settings.yaml"));
        SettingsWidgetRegistry widgets;
        for (const auto* id : {"native.length-wrap", "native.length-unwrap"})
        {
            auto               descriptor = std::make_shared<const SettingsSchema>(_registry.type(id)->settingsSchema);
            SettingsFormModel  model(descriptor, owner.node(id));
            SettingsFormWidget form(model, widgets);
            form.setWindowTitle(_registry.type(id)->title);
            auto* width = form.editor("width")->findChild<QSpinBox*>();
            QVERIFY(width);
            QCOMPARE(width->minimum(), 1);
            QCOMPARE(width->maximum(), 8);
            QCOMPARE(width->singleStep(), 1);
            QCOMPARE(width->value(), 2);
            model.setValue("width", qint64(8));
            QCOMPARE(width->value(), 8);
            width->setValue(3);
            QCOMPARE(model.value("width").toInt(), 3);
            QVERIFY(model.isValid());
            QVERIFY(form.visibleFields().contains("position"));
            QVERIFY(form.visibleFields().contains("endianness"));
            QVERIFY(form.visibleFields().contains("includeField"));
            QVERIFY(form.visibleFields().contains("adjustment"));
            QVERIFY(!form.visibleFields().contains("maxBytes"));
            auto* selector = form.editor("encoding")->findChild<QComboBox*>();
            QVERIFY(selector);
            selector->setCurrentIndex(1);
            QVERIFY(form.visibleFields().contains("maxBytes"));
            for (const auto* field : {"position", "width", "endianness"})
            {
                QVERIFY(!form.visibleFields().contains(field));
            }
            model.setValue("maxBytes", 4);
            selector->setCurrentIndex(0);
            QCOMPARE(width->value(), 3);
            selector->setCurrentIndex(1);
            QCOMPARE(model.value("maxBytes").toInt(), 4);
            QString error;
            QVERIFY2(model.apply(&error), qPrintable(error));
            const auto effective = model.values();
            QVERIFY(!effective.contains("width"));
            auto node = create(id, effective);
            QVERIFY(node);
            QCOMPARE(node->configuration().value("maxBytes").toInt(), 4);
            form.resize(620, 340);
            form.show();
            QVERIFY(QTest::qWaitForWindowExposed(&form));
        }
    }
};
QTEST_MAIN(LengthProcessorTests)
#include "LengthProcessorTests.moc"
