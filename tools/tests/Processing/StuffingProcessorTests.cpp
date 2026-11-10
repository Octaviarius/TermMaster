#include "Nodes/StuffingUnwrap.h"
#include "Nodes/StuffingWrap.h"

#include <QTest>
#include <Termaster/Core/Processing/Lineflow.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/SettingsSchema.h>
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

class StuffingProcessorTests final : public QObject
{
    Q_OBJECT
private:
    ProcessorRegistry _registry;

    std::shared_ptr<Processor> _create(const char*              id,
                                       const QVariantMap&       configuration = {},
                                       const Processor::Limits& limits        = {})
    {
        QString error;
        auto    node = _registry.create(id, configuration, limits, &error);
        if (!node)
        {
            qWarning().noquote() << error;
        }
        return node;
    }

    std::shared_ptr<Processor> _packet(const char*              id,
                                       QVariantMap              configuration = {},
                                       const Processor::Limits& limits        = {})
    {
        if (QString(id).endsWith("unwrap"))
        {
            configuration.insert("framing", false);
        }
        return _create(id, configuration, limits);
    }

    Q_SLOT void initTestCase()
    {
        registerNativeProcessors(_registry, "native");
    }
    Q_SLOT void defaultsAndCatalog()
    {
        for (const auto* id : {"native.stuffing-wrap", "native.stuffing-unwrap"})
        {
            const auto type = _registry.type(id);
            QVERIFY(type);
            QCOMPARE(type->group, QString("Framing"));
            QCOMPARE(type->inputCount, 1);
            QCOMPARE(type->outputCount, 1);
            auto node = _create(id,
                                {
                                    {"future", QVariantMap {{"retained", 42}}}
            });
            QVERIFY(node);
            const auto config = node->configuration();
            QCOMPARE(config.value("boundaryMode").toString(), QString("suffix"));
            QCOMPARE(config.value("delimiter").toInt(), 192);
            QCOMPARE(config.value("escape").toInt(), 219);
            QCOMPARE(config.value("delimiterCode").toInt(), 220);
            QCOMPARE(config.value("escapeCode").toInt(), 221);
            if (QString(id).endsWith("unwrap"))
            {
                QCOMPARE(config.value("framing"), QVariant(true));
            }
            else
            {
                QVERIFY(!config.contains("framing"));
            }
            QCOMPARE(config.value("future").toMap().value("retained").toInt(), 42);
            SettingsSchema schema(type->settingsSchema);
            QVERIFY(schema.validate(config).isEmpty());
            for (const auto* key : {"delimiter", "escape", "delimiterCode", "escapeCode"})
            {
                auto invalid = config;
                invalid.insert(key, 256);
                QVERIFY(!schema.validate(invalid).isEmpty());
                invalid.insert(key, -1);
                QVERIFY(!schema.validate(invalid).isEmpty());
            }
        }
    }
    Q_SLOT void independentDefaultVectors()
    {
        // RFC 1055 mapping: END C0 -> DB DC; ESC DB -> DB DD; other bytes remain literal.
        const QList<QPair<QByteArray, QByteArray>> vectors {
            {                                   {},                     QByteArray::fromHex("c0")},
            {        QByteArray::fromHex("00ff41"),               QByteArray::fromHex("00ff41c0")},
            {QByteArray::fromHex("c0dbdcdd00ffc0"), QByteArray::fromHex("dbdcdbdddcdd00ffdbdcc0")}
        };
        for (const auto& [payload, packet] : vectors)
        {
            for (const auto* mode : {"suffix", "both", "none"})
            {
                const QVariantMap config {
                    {"boundaryMode", mode}
                };
                auto expected = packet;
                if (QString(mode) == "both")
                {
                    expected.prepend(char(0xc0));
                }
                else if (QString(mode) == "none")
                {
                    expected.chop(1);
                }
                auto wrap   = _packet("native.stuffing-wrap", config);
                auto unwrap = _packet("native.stuffing-unwrap", config);
                QVERIFY(wrap && unwrap);
                const auto encoded = invoke(*wrap, payload);
                QCOMPARE(encoded.accepted, Processor::AcceptResult::Accepted);
                QCOMPARE(encoded.completed, 1);
                QVERIFY(encoded.error.isEmpty());
                QCOMPARE(encoded.output.size(), 1);
                QCOMPARE(encoded.output.first().bytes, expected);
                const auto decoded = invoke(*unwrap, expected);
                QVERIFY(decoded.error.isEmpty());
                QCOMPARE(decoded.output.size(), 1);
                QCOMPARE(decoded.output.first().bytes, payload);
            }
        }
    }
    Q_SLOT void allBytesAndNondefaultMappings()
    {
        QByteArray payload;
        for (int byte = 0; byte < 256; ++byte)
        {
            payload.append(char(byte));
        }
        const QList<QVariantMap> mappings {
            {},
            {{"delimiter", 0}, {"escape", 255}, {"delimiterCode", 255}, {"escapeCode", 1}},
            {{"delimiter", 255}, {"escape", 0}, {"delimiterCode", 1}, {"escapeCode", 0}}
        };
        for (auto config : mappings)
        {
            for (const auto* mode : {"none", "suffix", "both"})
            {
                config.insert("boundaryMode", mode);
                auto wrap   = _packet("native.stuffing-wrap", config);
                auto unwrap = _packet("native.stuffing-unwrap", config);
                QVERIFY(wrap && unwrap);
                const auto packet = invoke(*wrap, payload);
                QVERIFY(packet.error.isEmpty());
                const auto decoded = invoke(*unwrap, packet.output.first().bytes);
                QVERIFY(decoded.error.isEmpty());
                QCOMPARE(decoded.output.first().bytes, payload);
                QCOMPARE(invoke(*wrap, {}).output.size(), 1);
            }
        }
        // Replacement equal to escape is a fixed pair, never recursively interpreted.
        const QVariantMap config {
            {    "delimiter",   0},
            {       "escape", 255},
            {"delimiterCode", 255},
            {   "escapeCode",   1}
        };
        auto wrap   = _packet("native.stuffing-wrap", config);
        auto unwrap = _packet("native.stuffing-unwrap", config);
        QCOMPARE(invoke(*wrap, QByteArray::fromHex("00ff01ff")).output.first().bytes,
                 QByteArray::fromHex("ffffff0101ff0100"));
        QCOMPARE(invoke(*unwrap, QByteArray::fromHex("ffffff0101ff0100")).output.first().bytes,
                 QByteArray::fromHex("00ff01ff"));
    }
    Q_SLOT void malformedPacketsFailAtomically()
    {
        const QList<QPair<QString, QStringList>> cases {
            {"suffix",               {"", "41", "41db00c0", "41dbc0", "41c042c0", "c0c0", "41c000"}},
            {  "both", {"", "c0", "41c0", "c041", "c041db00c0", "c041dbc0", "c041c042c0", "c0c0c0"}},
            {  "none",                                 {"41db", "41db00", "41c042", "c0", "dbdcc0"}}
        };
        for (const auto& [mode, packets] : cases)
        {
            for (const auto& packet : packets)
            {
                auto       node   = _packet("native.stuffing-unwrap",
                                            {
                                                {"boundaryMode", mode}
                });
                const auto result = invoke(*node, QByteArray::fromHex(packet.toLatin1()));
                QCOMPARE(result.accepted, Processor::AcceptResult::Accepted);
                QCOMPARE(result.completed, 1);
                QVERIFY2(!result.error.isEmpty(), qPrintable(mode + ": " + packet));
                QVERIFY(result.output.isEmpty());
                const auto stopped = invoke(*node, QByteArray::fromHex("c0"));
                QCOMPARE(stopped.accepted, Processor::AcceptResult::Rejected);
                QCOMPARE(stopped.completed, 0);
            }
        }
        auto node = _packet("native.stuffing-unwrap",
                            {
                                {"boundaryMode", "none"}
        });
        QVERIFY(!invoke(*node, QByteArray::fromHex("db")).error.isEmpty());
        QCOMPARE(invoke(*node, QByteArray::fromHex("dc")).accepted, Processor::AcceptResult::Rejected);
    }
    Q_SLOT void strictConfiguration()
    {
        const QList<QVariant> invalidBytes {true,
                                            QString("192"),
                                            -1,
                                            256,
                                            1.5,
                                            std::numeric_limits<double>::infinity(),
                                            std::numeric_limits<double>::quiet_NaN(),
                                            QVariantList {},
                                            qulonglong(0xffffffffffffffff)};
        for (const auto* id : {"native.stuffing-wrap", "native.stuffing-unwrap"})
        {
            QString error;
            for (const auto* key : {"delimiter", "escape", "delimiterCode", "escapeCode"})
            {
                for (const auto& value : invalidBytes)
                {
                    QVERIFY(!_registry.create(id,
                                              {
                                                  {key, value}
                    },
                                              {},
                                              &error));
                    QVERIFY(!error.isEmpty());
                    const auto factory = QString(id).endsWith("unwrap") ? StuffingUnwrap::create : StuffingWrap::create;
                    QVERIFY(!factory(
                        {
                            {key, value}
                    },
                        {},
                        &error));
                    QVERIFY2(error.contains(key), qPrintable(error));
                }
            }
            for (const auto& value : QList<QVariant> {true, 0, "invalid", QVariant {}})
            {
                QVERIFY(!_registry.create(id,
                                          {
                                              {"boundaryMode", value}
                },
                                          {},
                                          &error));
                QVERIFY(error.contains("boundaryMode"));
            }
            for (const auto& config : QList<QVariantMap> {{{"delimiter", 219}},
                                                          {{"delimiterCode", 221}},
                                                          {{"delimiterCode", 192}},
                                                          {{"escapeCode", 192}}})
            {
                QVERIFY(!_registry.create(id, config, {}, &error));
                QVERIFY(!error.isEmpty());
            }
            auto limits     = Processor::Limits {};
            limits.maxSteps = 0;
            QVERIFY(!_registry.create(id, {}, limits, &error));
            limits          = {};
            limits.maxBytes = 0;
            QVERIFY(!_registry.create(id, {}, limits, &error));
        }
    }
    Q_SLOT void byteAndStepBounds()
    {
        Processor::Limits limits;
        limits.maxBytes = 4;
        limits.maxSteps = 100;
        auto exact      = _packet("native.stuffing-wrap", {}, limits);
        QCOMPARE(invoke(*exact, "abc").output.first().bytes, QByteArray("abc") + char(0xc0));
        auto expanded = _packet("native.stuffing-wrap", {}, limits);
        QVERIFY(!invoke(*expanded, QByteArray::fromHex("41c0db")).error.isEmpty());
        auto exactEscapes = _packet("native.stuffing-wrap", {}, limits);
        QCOMPARE(invoke(*exactEscapes, QByteArray::fromHex("41c0")).output.first().bytes,
                 QByteArray::fromHex("41dbdcc0"));
        QCOMPARE(invoke(*exact, "abcde").accepted, Processor::AcceptResult::Rejected);
        limits.maxBytes = 1;
        auto empty      = _packet("native.stuffing-wrap", {}, limits);
        QCOMPARE(invoke(*empty, {}).output.first().bytes, QByteArray::fromHex("c0"));
        auto both = _packet("native.stuffing-wrap",
                            {
                                {"boundaryMode", "both"}
        },
                            limits);
        QVERIFY(!invoke(*both, {}).error.isEmpty());
        for (const auto* id : {"native.stuffing-wrap", "native.stuffing-unwrap"})
        {
            const auto input = QString(id).endsWith("unwrap") ? QByteArray::fromHex("41dbdcc0") :
                                                                QByteArray::fromHex("41c0");
            limits.maxBytes  = 10;
            limits.maxSteps  = 6; // Input bytes plus output bytes, including boundaries.
            auto fits        = _packet(id, {}, limits);
            QVERIFY(invoke(*fits, input).error.isEmpty());
            limits.maxSteps   = 5;
            auto       over   = _packet(id, {}, limits);
            const auto result = invoke(*over, input);
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.output.isEmpty());
        }
    }
    Q_SLOT void ownershipCancellationAndReentry()
    {
        for (const auto* id : {"native.stuffing-wrap", "native.stuffing-unwrap"})
        {
            auto node  = _packet(id);
            auto bytes = QString(id).endsWith("unwrap") ? QByteArray::fromHex("41dbdcc0") : QByteArray::fromHex("41c0");
            auto result = invoke(*node, QByteArray::fromRawData(bytes.constData(), bytes.size()));
            bytes.fill('x');
            QCOMPARE(result.output.first().bytes,
                     QString(id).endsWith("unwrap") ? QByteArray::fromHex("41c0") : QByteArray::fromHex("41dbdcc0"));
            auto recursive = Processor::AcceptResult::Accepted;
            int  completed = 0;
            node->accept({QString(id).endsWith("unwrap") ? QByteArray::fromHex("c0") : QByteArray {}},
                         {[&](auto) { recursive = invoke(*node, {}).accepted; }, [&](auto) { ++completed; }});
            QCOMPARE(recursive, Processor::AcceptResult::WouldBlock);
            QCOMPARE(completed, 1);
            int acknowledged = 0;
            node->cancel([&] { ++acknowledged; });
            node->cancel([&] { ++acknowledged; });
            QCOMPARE(acknowledged, 2);
            QCOMPARE(invoke(*node, {}).accepted, Processor::AcceptResult::Rejected);

            node      = _packet(id);
            completed = 0;
            node->accept({QString(id).endsWith("unwrap") ? QByteArray::fromHex("c0") : QByteArray {}},
                         {[&](auto) { node->cancel([] { }); }, [&](auto) { ++completed; }});
            QCOMPARE(completed, 0);
            QCOMPARE(invoke(*node, {}).accepted, Processor::AcceptResult::Rejected);
        }
        auto node   = _packet("native.stuffing-unwrap");
        auto nested = Processor::AcceptResult::Accepted;
        node->accept({QByteArray::fromHex("41dbc0")}, {{}, [&](auto error) {
                                                           QVERIFY(!error.isEmpty());
                                                           nested = invoke(*node, QByteArray::fromHex("c0")).accepted;
                                                       }});
        QCOMPARE(nested, Processor::AcceptResult::Rejected);
    }
    Q_SLOT void independentPackets()
    {
        auto wrap = _packet("native.stuffing-wrap");
        QCOMPARE(invoke(*wrap, "a").output.first().bytes, QByteArray::fromHex("61c0"));
        QCOMPARE(invoke(*wrap, "b").output.first().bytes, QByteArray::fromHex("62c0"));
        auto unwrap = _packet("native.stuffing-unwrap");
        QCOMPARE(invoke(*unwrap, QByteArray::fromHex("61c0")).output.first().bytes, QByteArray("a"));
        QCOMPARE(invoke(*unwrap, QByteArray::fromHex("62c0")).output.first().bytes, QByteArray("b"));
    }
    Q_SLOT void defaultFramingHandlesFragmentedStream()
    {
        QString error;
        auto    node = StuffingUnwrap::create({}, {}, &error);
        QVERIFY2(node, qPrintable(error));
        QCOMPARE(node->configuration().value("framing"), QVariant(true));
        const auto first = invoke(*node, QByteArray::fromHex("00db"));
        QVERIFY(first.error.isEmpty());
        QVERIFY(first.output.isEmpty());
        const auto second = invoke(*node, QByteArray::fromHex("dcc041c0"));
        QVERIFY(second.error.isEmpty());
        QCOMPARE(second.output.size(), 2);
        QCOMPARE(second.output[0].bytes, QByteArray::fromHex("00c0"));
        QCOMPARE(second.output[1].bytes, QByteArray("A"));
    }
    Q_SLOT void framedStreamEverySplit()
    {
        const auto payload = QByteArray::fromHex("00ffc0db");
        for (const auto* mode : {"suffix", "none", "both"})
        {
            const auto wire = QString(mode) == "both" ? QByteArray::fromHex("c000ffdbdcdbddc0c0c0c041c0") :
                                                        QByteArray::fromHex("00ffdbdcdbddc0c041c0");
            for (qsizetype split = 0; split <= wire.size(); ++split)
            {
                auto              node = _create("native.stuffing-unwrap",
                                                 {
                                                     {     "framing", true},
                                                     {"boundaryMode", mode}
                });
                QList<QByteArray> decoded;
                for (const auto& chunk : {wire.first(split), wire.sliced(split)})
                {
                    const auto result = invoke(*node, chunk);
                    QCOMPARE(result.completed, 1);
                    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
                    for (const auto& packet : result.output)
                    {
                        decoded.append(packet.bytes);
                    }
                }
                QCOMPARE(decoded, (QList<QByteArray> {payload, {}, "A"}));
            }
            auto              node = _create("native.stuffing-unwrap",
                                             {
                                                 {     "framing", true},
                                                 {"boundaryMode", mode}
            });
            QList<QByteArray> decoded;
            for (const char byte : wire)
            {
                const auto result = invoke(*node, QByteArray(1, byte));
                QVERIFY(result.error.isEmpty());
                for (const auto& packet : result.output)
                {
                    decoded.append(packet.bytes);
                }
            }
            QCOMPARE(decoded, (QList<QByteArray> {payload, {}, "A"}));
            const auto empty = invoke(*node, {});
            QVERIFY(empty.output.isEmpty());
            QCOMPARE(empty.completed, 1);
        }
        auto       node       = _create("native.stuffing-unwrap",
                                        {
                                            {"framing", true}
        });
        const auto incomplete = invoke(*node, QByteArray::fromHex("00db"));
        QVERIFY(incomplete.error.isEmpty());
        QVERIFY(incomplete.output.isEmpty());
        QVERIFY(invoke(*node, {}).output.isEmpty());
        QCOMPARE(invoke(*node, QByteArray::fromHex("dcc0")).output.first().bytes, QByteArray::fromHex("00c0"));
    }
    Q_SLOT void framedLimitsAndAtomicFailure()
    {
        Processor::Limits limits;
        limits.maxBytes = 4;
        auto node       = _create("native.stuffing-unwrap",
                                  {
                                      {"framing", true}
        },
                                  limits);
        QVERIFY(invoke(*node, "abcd").output.isEmpty());
        QCOMPARE(invoke(*node, QByteArray::fromHex("c0")).output.first().bytes, QByteArray("abcd"));
        node = _create("native.stuffing-unwrap",
                       {
                           {"framing", true}
        },
                       limits);
        QVERIFY(invoke(*node, "abcd").error.isEmpty());
        const auto exceeded = invoke(*node, "e");
        QVERIFY(!exceeded.error.isEmpty());
        QVERIFY(exceeded.output.isEmpty());
        node = _create("native.stuffing-unwrap",
                       {
                           {"framing", true}
        },
                       limits);
        QVERIFY(invoke(*node, "ab").error.isEmpty());
        // Previously buffered bytes plus outputs and the next pending body share one byte cap.
        const auto aggregate = invoke(*node, QByteArray::fromHex("63c06465"));
        QVERIFY(!aggregate.error.isEmpty());
        QVERIFY(aggregate.output.isEmpty());
        limits            = {};
        limits.maxChunks  = 1;
        node              = _create("native.stuffing-unwrap",
                                    {
                                        {"framing", true}
        },
                                    limits);
        const auto chunks = invoke(*node, QByteArray::fromHex("c0c0"));
        QVERIFY(!chunks.error.isEmpty());
        QVERIFY(chunks.output.isEmpty());
        limits          = {};
        limits.maxSteps = 4;
        node            = _create("native.stuffing-unwrap",
                                  {
                                      {"framing", true}
        },
                                  limits);
        QCOMPARE(invoke(*node, QByteArray::fromHex("61c0")).output.first().bytes, QByteArray("a"));
        limits.maxSteps = 3;
        node            = _create("native.stuffing-unwrap",
                                  {
                                      {"framing", true}
        },
                                  limits);
        QVERIFY(!invoke(*node, QByteArray::fromHex("61c0")).error.isEmpty());
        limits.maxSteps = 6;
        node            = _create("native.stuffing-unwrap",
                                  {
                                      {"framing", true}
        },
                                  limits);
        QVERIFY(invoke(*node, "abc").error.isEmpty());
        QVERIFY(!invoke(*node, QByteArray::fromHex("c0")).error.isEmpty());
        for (const auto& packet : {QByteArray::fromHex("41c042db00c0"), QByteArray::fromHex("41c042dbc0")})
        {
            node = _create("native.stuffing-unwrap",
                           {
                               {"framing", true}
            });
            QCOMPARE(invoke(*node, QByteArray::fromHex("50c0")).output.first().bytes, QByteArray("P"));
            const auto failed = invoke(*node, packet);
            QCOMPARE(failed.completed, 1);
            QVERIFY(!failed.error.isEmpty());
            QVERIFY(failed.output.isEmpty());
            QCOMPARE(invoke(*node, QByteArray::fromHex("c0")).accepted, Processor::AcceptResult::Rejected);
        }
        node = _create("native.stuffing-unwrap",
                       {
                           {     "framing",   true},
                           {"boundaryMode", "both"}
        });
        QVERIFY(!invoke(*node, QByteArray::fromHex("41c0")).error.isEmpty());
        QString error;
        for (const auto& invalid : QList<QVariant> {"true", 1, QVariantList {}})
        {
            QVERIFY(!StuffingUnwrap::create(
                {
                    {"framing", invalid}
            },
                {},
                &error));
            QVERIFY(error.contains("framing"));
        }
        QVERIFY(!StuffingWrap::schema().contains("    framing:"));
    }
    Q_SLOT void framedLifecycleAndOwnership()
    {
        auto node = _create("native.stuffing-unwrap",
                            {
                                {"framing", true}
        });
        QVERIFY(invoke(*node, QByteArray::fromHex("41db")).output.isEmpty());
        int acknowledged = 0;
        node->cancel([&] { ++acknowledged; });
        QCOMPARE(acknowledged, 1);
        QCOMPARE(invoke(*node, QByteArray::fromHex("dcc0")).accepted, Processor::AcceptResult::Rejected);
        node              = _create("native.stuffing-unwrap",
                                    {
                                        {"framing", true}
        });
        auto       bytes  = QByteArray::fromHex("41dbdcc042c0");
        const auto result = invoke(*node, QByteArray::fromRawData(bytes.constData(), bytes.size()));
        bytes.fill('x');
        QCOMPARE(result.output.size(), 2);
        QCOMPARE(result.output[0].bytes, QByteArray::fromHex("41c0"));
        QCOMPARE(result.output[1].bytes, QByteArray("B"));
        auto recursive = Processor::AcceptResult::Accepted;
        int  outputs   = 0;
        int  completed = 0;
        node->accept({QByteArray::fromHex("41c042c0")},
                     {[&](auto) {
                          ++outputs;
                          recursive = invoke(*node, QByteArray::fromHex("c0")).accepted;
                          node->cancel([] { });
                      },
                      [&](auto) { ++completed; }});
        QCOMPARE(recursive, Processor::AcceptResult::WouldBlock);
        QCOMPARE(outputs, 1);
        QCOMPARE(completed, 0);
        node    = _create("native.stuffing-unwrap",
                          {
                              {"framing", true}
        });
        outputs = 0;
        node->accept({QByteArray::fromHex("41c042c0")},
                     {[&](auto) {
                          ++outputs;
                          node.reset();
                      },
                      [&](auto) { ++completed; }});
        QCOMPARE(outputs, 1);
        QCOMPARE(completed, 0);
        QVERIFY(!node);
        node = _create("native.stuffing-unwrap",
                       {
                           {"framing", true}
        });
        QVERIFY(invoke(*node, "a").error.isEmpty());
        QCOMPARE(node->accept({QByteArray::fromHex("c0"), 1}, {}), Processor::AcceptResult::Rejected);
        QCOMPARE(invoke(*node, QByteArray::fromHex("c0")).output.first().bytes, QByteArray("a"));
    }
    Q_SLOT void registeredLineflowAndFramerSplits()
    {
        const auto run = [&](const QVariantList& nodes, const QList<QByteArray>& packets, QList<QByteArray>& output) {
            const QVariantMap blocks {
                {"packet", QVariantMap {{"id", "packet"}, {"name", "Stuffed packets"}, {"nodes", nodes}}}
            };
            QString    error;
            const auto prepared = Lineflow::prepare(blocks, "packet", _registry, {}, &error);
            QVERIFY2(prepared, qPrintable(error));
            auto node = prepared->instantiate(_registry, &error);
            QVERIFY2(node, qPrintable(error));
            int completed = 0;
            for (const auto& packet : packets)
            {
                const auto before = completed;
                QCOMPARE(node->accept({packet},
                                      {[&](auto chunk) { output.append(chunk.bytes); },
                                       [&](QString failure) {
                                           error = failure;
                                           ++completed;
                                       }}),
                         Processor::AcceptResult::Accepted);
                QTRY_COMPARE(completed, before + 1);
                QVERIFY2(error.isEmpty(), qPrintable(error));
            }
        };
        const QVariantList pair {
            QVariantMap {  {"name", "wrap"},   {"type", "native.stuffing-wrap"}},
            QVariantMap {{"name", "unwrap"}, {"type", "native.stuffing-unwrap"}}
        };
        const auto        payload = QByteArray::fromHex("00ffc0db");
        QList<QByteArray> output;
        run(pair, {payload, {}}, output);
        QCOMPARE(output, (QList<QByteArray> {payload, {}}));
        const QVariantList stream {
            QVariantMap {{"name", "frames"},
                         {"type", "native.framer"},
                         {"configuration", QVariantMap {{"ending", "\\xC0"}}}                         },
            QVariantMap {{"name", "unwrap"},
                         {"type", "native.stuffing-unwrap"},
                         {"configuration", QVariantMap {{"boundaryMode", "none"}, {"framing", false}}}}
        };
        const auto wire = QByteArray::fromHex("00ffdbdcdbddc0c041c0");
        for (qsizetype split = 0; split <= wire.size(); ++split)
        {
            output.clear();
            run(stream, {wire.first(split), wire.sliced(split)}, output);
            QCOMPARE(output, (QList<QByteArray> {payload, {}, QByteArray("A")}));
        }
        output.clear();
        QList<QByteArray> bytewise;
        for (const char byte : wire)
        {
            bytewise.append(QByteArray(1, byte));
        }
        run(stream, bytewise, output);
        QCOMPARE(output, (QList<QByteArray> {payload, {}, QByteArray("A")}));
        output.clear();
        const QVariantList integrated {
            QVariantMap {{"name", "unwrap"},
                         {"type", "native.stuffing-unwrap"},
                         {"configuration", QVariantMap {{"framing", true}}}}
        };
        run(integrated, bytewise, output);
        QCOMPARE(output, (QList<QByteArray> {payload, {}, QByteArray("A")}));
    }
};
QTEST_GUILESS_MAIN(StuffingProcessorTests)
#include "StuffingProcessorTests.moc"
