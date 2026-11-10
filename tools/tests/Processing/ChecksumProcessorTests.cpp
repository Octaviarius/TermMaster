#include "Nodes/ChecksumUnwrap.h"
#include "Nodes/ChecksumWrap.h"

#include <QTest>
#include <Termaster/Core/Processing/Lineflow.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/SettingsSchema.h>
#include <algorithm>
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

quint32 reflect(quint32 value, int bits)
{
    quint32 result = 0;
    for (int bit = 0; bit < bits; ++bit)
    {
        result   = (result << 1) | (value & 1);
        value  >>= 1;
    }
    return result;
}

// Independent normal-register bit recurrence, deliberately unlike the runtime table path.
QByteArray reference(const QByteArray& payload, const QVariantMap& config)
{
    const auto width      = config.value("width").toInt();
    const auto mask       = width == 32 ? quint32(0xffffffff) : (quint32(1) << width) - 1;
    const auto polynomial = config.value("polynomial").toUInt();
    auto       value      = config.value("init").toUInt();
    for (const auto byte : payload)
    {
        const auto input = config.value("refin").toBool() ? reflect(static_cast<unsigned char>(byte), 8) :
                                                            static_cast<unsigned char>(byte);
        for (int bit = 7; bit >= 0; --bit)
        {
            const bool feedback = ((value >> (width - 1)) ^ (input >> bit)) & 1;
            value               = ((value << 1) ^ (feedback ? polynomial : 0)) & mask;
        }
    }
    if (config.value("refout").toBool())
    {
        value = reflect(value, width);
    }
    value ^= config.value("xorout").toUInt();
    QByteArray result(width / 8, Qt::Uninitialized);
    for (int index = 0; index < result.size(); ++index)
    {
        result[config.value("endianness") == "big" ? result.size() - 1 - index : index]   = char(value & 0xff);
        value                                                                           >>= 8;
    }
    return result;
}
} // namespace

class ChecksumProcessorTests final : public QObject
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
        for (const auto* id : {"native.checksum-wrap", "native.checksum-unwrap"})
        {
            const auto type = _registry.type(id);
            QVERIFY(type);
            QCOMPARE(type->group, QString("Integrity"));
            QCOMPARE(type->inputCount, 1);
            QCOMPARE(type->outputCount, 1);
            auto node = create(id,
                               {
                                   {"unknown", QVariantMap {{"retained", 42}}}
            });
            QVERIFY(node);
            const auto config = node->configuration();
            QCOMPARE(config.value("algorithm").toString(), QString("crc16-modbus"));
            QCOMPARE(config.value("position").toString(), QString("suffix"));
            QCOMPARE(config.value("endianness").toString(), QString("little"));
            QVERIFY(!config.contains("polynomial"));
            QCOMPARE(config.value("unknown").toMap().value("retained").toInt(), 42);
            SettingsSchema schema(type->settingsSchema);
            QVERIFY(schema.validate(config).isEmpty());
        }
    }
    Q_SLOT void publishedCheckVectorsAndLayouts()
    {
        // CRC RevEng catalogue check values for ASCII 123456789; arithmetic checks calculated separately.
        const QList<QPair<QString, QByteArray>> vectors {
            {          "sum8",       QByteArray::fromHex("dd")},
            {          "xor8",       QByteArray::fromHex("31")},
            {    "crc8-smbus",       QByteArray::fromHex("f4")},
            {  "crc16-modbus",     QByteArray::fromHex("374b")},
            { "crc16-ibm3740",     QByteArray::fromHex("b129")},
            {"crc32-iso-hdlc", QByteArray::fromHex("2639f4cb")},
            {   "crc32-iscsi", QByteArray::fromHex("839206e3")}
        };
        for (const auto& [algorithm, little] : vectors)
        {
            for (const auto* position : {"prefix", "suffix"})
            {
                for (const auto* order : {"little", "big"})
                {
                    const QVariantMap config {
                        { "algorithm", algorithm},
                        {  "position",  position},
                        {"endianness",     order}
                    };
                    auto field = little;
                    if (QString(order) == "big")
                    {
                        std::reverse(field.begin(), field.end());
                    }
                    const QByteArray payload("123456789");
                    const auto       packet = QString(position) == "prefix" ? field + payload : payload + field;
                    auto             wrap   = create("native.checksum-wrap", config);
                    auto             unwrap = create("native.checksum-unwrap", config);
                    QVERIFY(wrap && unwrap);
                    const auto encoded = invoke(*wrap, payload);
                    QVERIFY(encoded.error.isEmpty());
                    QCOMPARE(encoded.output.size(), 1);
                    QCOMPARE(encoded.output.first().bytes, packet);
                    const auto decoded = invoke(*unwrap, packet);
                    QVERIFY(decoded.error.isEmpty());
                    QCOMPARE(decoded.output.first().bytes, payload);
                }
            }
        }
    }
    Q_SLOT void emptyAndBinaryPackets()
    {
        const QList<QPair<QString, QByteArray>> empty {
            {          "sum8",       QByteArray::fromHex("00")},
            {          "xor8",       QByteArray::fromHex("00")},
            {    "crc8-smbus",       QByteArray::fromHex("00")},
            {  "crc16-modbus",     QByteArray::fromHex("ffff")},
            { "crc16-ibm3740",     QByteArray::fromHex("ffff")},
            {"crc32-iso-hdlc", QByteArray::fromHex("00000000")},
            {   "crc32-iscsi", QByteArray::fromHex("00000000")}
        };
        QByteArray binary;
        for (int byte = 0; byte < 256; ++byte)
        {
            binary.append(char(byte));
        }
        for (const auto& [algorithm, field] : empty)
        {
            auto wrap   = create("native.checksum-wrap",
                                 {
                                     {"algorithm", algorithm}
            });
            auto unwrap = create("native.checksum-unwrap",
                                 {
                                     {"algorithm", algorithm}
            });
            QCOMPARE(invoke(*wrap, {}).output.first().bytes, field);
            const auto decoded = invoke(*unwrap, field);
            QCOMPARE(decoded.output.size(), 1);
            QVERIFY(decoded.output.first().bytes.isEmpty());
            const auto packet = invoke(*wrap, binary).output.first().bytes;
            QCOMPARE(invoke(*unwrap, packet).output.first().bytes, binary);
            QCOMPARE(invoke(*wrap, {}).output.first().bytes, field);
        }
        auto sum   = create("native.checksum-wrap",
                            {
                                {"algorithm", "sum8"}
        });
        auto xored = create("native.checksum-wrap",
                            {
                                {"algorithm", "xor8"}
        });
        QCOMPARE(invoke(*sum, QByteArray::fromHex("00ff0102")).output.first().bytes, QByteArray::fromHex("00ff010202"));
        QCOMPARE(invoke(*xored, QByteArray::fromHex("00ff0102")).output.first().bytes,
                 QByteArray::fromHex("00ff0102fc"));
    }
    Q_SLOT void customTableAgainstBitReference()
    {
        QByteArray binary;
        for (int byte = 0; byte < 256; ++byte)
        {
            binary.append(char(byte));
        }
        for (const auto width : {8, 16, 32})
        {
            const auto mask = width == 32 ? quint32(0xffffffff) : (quint32(1) << width) - 1;
            for (const auto refin : {false, true})
            {
                for (const auto refout : {false, true})
                {
                    for (const auto polynomial : {quint32(0x07), quint32(0x06), mask})
                    {
                        for (const auto* order : {"little", "big"})
                        {
                            const QVariantMap config {
                                { "algorithm",                  "crc-custom"},
                                {     "width",                         width},
                                {"polynomial",        qulonglong(polynomial)},
                                {      "init", qulonglong(0x96a53c1e & mask)},
                                {     "refin",                         refin},
                                {    "refout",                        refout},
                                {    "xorout", qulonglong(0x379bd105 & mask)},
                                {"endianness",                         order}
                            };
                            auto wrap = create("native.checksum-wrap", config);
                            QVERIFY(wrap);
                            for (const auto& payload :
                                 {QByteArray {}, QByteArray("123456789"), binary, binary.repeated(9)})
                            {
                                const auto expected = payload + reference(payload, config);
                                const auto result   = invoke(*wrap, payload);
                                QVERIFY(result.error.isEmpty());
                                QCOMPARE(result.output.first().bytes, expected);
                            }
                        }
                    }
                }
            }
        }
    }
    Q_SLOT void malformedPacketsStopAtomically()
    {
        for (const auto* position : {"prefix", "suffix"})
        {
            const QVariantMap config {
                {"position", position}
            };
            auto       wrap           = create("native.checksum-wrap", config);
            const auto packet         = invoke(*wrap, QByteArray::fromHex("4100ff")).output.first().bytes;
            auto       payloadCorrupt = packet;
            payloadCorrupt[QString(position) == "prefix" ? 2 : 0]               ^= 1;
            auto fieldCorrupt                                                    = packet;
            fieldCorrupt[QString(position) == "prefix" ? 0 : packet.size() - 1] ^= 1;
            for (const auto& invalid : {QByteArray {},
                                        QByteArray("x"),
                                        payloadCorrupt,
                                        fieldCorrupt,
                                        packet + 'x',
                                        packet.left(packet.size() - 1),
                                        packet + packet})
            {
                auto       unwrap = create("native.checksum-unwrap", config);
                const auto result = invoke(*unwrap, invalid);
                QCOMPARE(result.accepted, Processor::AcceptResult::Accepted);
                QCOMPARE(result.completed, 1);
                QVERIFY(!result.error.isEmpty());
                QVERIFY(result.output.isEmpty());
                QCOMPARE(invoke(*unwrap, packet).accepted, Processor::AcceptResult::Rejected);
            }
        }
    }
    Q_SLOT void limitsCancellationOwnershipAndReentry()
    {
        const auto payload       = QByteArray::fromHex("4100ff");
        auto       referenceWrap = create("native.checksum-wrap");
        const auto packet        = invoke(*referenceWrap, payload).output.first().bytes;
        for (const auto* id : {"native.checksum-wrap", "native.checksum-unwrap"})
        {
            const bool        unwrap = QString(id).endsWith("unwrap");
            const auto        input  = unwrap ? packet : payload;
            Processor::Limits limits;
            limits.maxBytes  = packet.size();
            limits.maxSteps  = packet.size();
            auto       node  = create(id, {}, limits);
            const auto exact = invoke(*node, input);
            QVERIFY(exact.error.isEmpty());
            QCOMPARE(exact.output.first().bytes, unwrap ? payload : packet);
            QCOMPARE(node->accept({{}, 1}, {}), Processor::AcceptResult::Rejected);
            QCOMPARE(invoke(*node, QByteArray(6, 'x')).accepted, Processor::AcceptResult::Rejected);
            QVERIFY(invoke(*node, input).error.isEmpty());
            limits.maxSteps--;
            node                = create(id, {}, limits);
            const auto exceeded = invoke(*node, input);
            QVERIFY(!exceeded.error.isEmpty());
            QVERIFY(exceeded.output.isEmpty());
            limits.maxSteps = 1;
            node            = create(id, {}, limits);
            QVERIFY(!invoke(*node, unwrap ? QByteArray::fromHex("ffff") : QByteArray {}).error.isEmpty());
            node                = create(id);
            auto       borrowed = input;
            const auto result   = invoke(*node, QByteArray::fromRawData(borrowed.constData(), borrowed.size()));
            borrowed.fill('x');
            QCOMPARE(result.output.first().bytes, unwrap ? payload : packet);
            Processor::AcceptResult reentrant = Processor::AcceptResult::Accepted;
            QCOMPARE(node->accept({input}, {[&](auto) { reentrant = invoke(*node, input).accepted; }, {}}),
                     Processor::AcceptResult::Accepted);
            QCOMPARE(reentrant, Processor::AcceptResult::WouldBlock);
            bool ready = false;
            node->cancel([&] { ready = true; });
            QVERIFY(ready);
            QCOMPARE(invoke(*node, input).accepted, Processor::AcceptResult::Rejected);
        }
        Processor::Limits limits;
        limits.maxBytes   = packet.size() - 1;
        auto       wrap   = create("native.checksum-wrap", {}, limits);
        const auto failed = invoke(*wrap, payload);
        QVERIFY(!failed.error.isEmpty());
        QVERIFY(failed.output.isEmpty());
        limits.maxBytes = 1;
        wrap            = create("native.checksum-wrap", {}, limits);
        QVERIFY(!invoke(*wrap, {}).error.isEmpty());
    }
    Q_SLOT void strictConfigurationAndInactiveKeys()
    {
        const QList<QVariantMap> invalid {
            {{"algorithm", "crc16"}},
            {{"algorithm", false}},
            {{"position", 1}},
            {{"position", "middle"}},
            {{"endianness", 0}},
            {{"endianness", "native"}},
            {{"algorithm", "crc-custom"}, {"width", 9}},
            {{"algorithm", "crc-custom"}, {"width", "16"}},
            {{"algorithm", "crc-custom"}, {"width", true}},
            {{"algorithm", "crc-custom"}, {"polynomial", 0}},
            {{"algorithm", "crc-custom"}, {"polynomial", -1}},
            {{"algorithm", "crc-custom"}, {"polynomial", 1.5}},
            {{"algorithm", "crc-custom"}, {"init", false}},
            {{"algorithm", "crc-custom"}, {"init", "0"}},
            {{"algorithm", "crc-custom"}, {"refin", 1}},
            {{"algorithm", "crc-custom"}, {"refout", "true"}},
            {{"algorithm", "crc-custom"}, {"xorout", 65536}},
            {{"algorithm", "crc-custom"}, {"width", 8}, {"polynomial", 256}},
            {{"algorithm", "crc-custom"}, {"width", 32}, {"polynomial", qulonglong(4294967296)}},
            {{"algorithm", "crc-custom"}, {"init", std::numeric_limits<double>::infinity()}},
            {{"algorithm", "crc-custom"}, {"xorout", std::numeric_limits<double>::quiet_NaN()}}
        };
        for (const auto& config : invalid)
        {
            for (const auto wrap : {false, true})
            {
                QString    error;
                const auto node = wrap ? ChecksumWrap::create(config, {}, &error) :
                                         ChecksumUnwrap::create(config, {}, &error);
                QVERIFY(!node);
                QVERIFY(!error.isEmpty());
            }
        }
        for (const auto* id : {"native.checksum-wrap", "native.checksum-unwrap"})
        {
            auto node = create(id,
                               {
                                   { "algorithm",     "sum8"},
                                   {"polynomial", "inactive"},
                                   {     "width",         -1},
                                   {      "init",       true},
                                   {     "refin",          2},
                                   {    "refout",        "x"},
                                   {    "xorout",      false},
                                   {"endianness", "inactive"},
                                   {   "unknown",         42}
            });
            QVERIFY(node);
            const auto config = node->configuration();
            for (const auto* key : {"width", "polynomial", "init", "refin", "refout", "xorout", "endianness"})
            {
                QVERIFY(!config.contains(key));
            }
            QCOMPARE(config.value("unknown").toInt(), 42);
            node = create(id,
                          {
                              { "algorithm",           "crc-custom"},
                              {     "width",                     32},
                              {"polynomial", qulonglong(4294967295)},
                              {      "init", qulonglong(4294967295)},
                              {    "xorout", qulonglong(4294967295)}
            });
            QVERIFY(node);
            QCOMPARE(node->configuration().value("polynomial").toULongLong(), qulonglong(4294967295));
            Processor::Limits limits;
            limits.maxSteps = 0;
            QString error;
            QVERIFY(!ChecksumWrap::create({}, limits, &error));
            QVERIFY(!ChecksumUnwrap::create({}, limits, &error));
            limits          = {};
            limits.maxBytes = 0;
            QVERIFY(!ChecksumWrap::create({}, limits, &error));
            QVERIFY(!ChecksumUnwrap::create({}, limits, &error));
        }
    }
    Q_SLOT void schemaWidthBounds()
    {
        SettingsSchema schema(ChecksumWrap::schema());
        for (const auto width : {8, 16})
        {
            const auto  maximum = width == 8 ? 255 : 65535;
            QVariantMap config {
                { "algorithm", "crc-custom"},
                {     "width",        width},
                {"polynomial",            7},
                {      "init",            0},
                {    "xorout",      maximum},
                {     "refin",        false},
                {    "refout",         true}
            };
            QVERIFY(schema.validate(config).isEmpty());
            for (const auto* key : {"polynomial", "init", "xorout"})
            {
                auto invalid = config;
                invalid.insert(key, maximum + 1);
                QVERIFY(!schema.validate(invalid).isEmpty());
            }
        }
    }
    Q_SLOT void registeredLineflowComposition()
    {
        const QVariantMap blocks {
            {"packet",
             QVariantMap {{"id", "packet"},
                          {"name", "Checksum packets"},
                          {"nodes",
                           QVariantList {QVariantMap {{"name", "wrap"}, {"type", "native.checksum-wrap"}},
                                         QVariantMap {{"name", "unwrap"}, {"type", "native.checksum-unwrap"}}}}}}
        };
        QString    error;
        const auto prepared = Lineflow::prepare(blocks, "packet", _registry, {}, &error);
        QVERIFY2(prepared, qPrintable(error));
        auto node = prepared->instantiate(_registry, &error);
        QVERIFY2(node, qPrintable(error));
        QList<QByteArray> output;
        int               completed = 0;
        const auto        payload   = QByteArray::fromHex("00ff4100");
        QCOMPARE(node->accept({payload},
                              {[&](auto chunk) { output.append(chunk.bytes); },
                               [&](QString failure) {
                                   error = failure;
                                   ++completed;
                               }}),
                 Processor::AcceptResult::Accepted);
        QTRY_COMPARE(completed, 1);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(output, QList<QByteArray> {payload});
    }
};
QTEST_MAIN(ChecksumProcessorTests)
#include "ChecksumProcessorTests.moc"
