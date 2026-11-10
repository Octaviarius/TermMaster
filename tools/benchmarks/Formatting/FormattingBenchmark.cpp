#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <Termaster/Core/Formatting/CompiledFormat.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fmt/format.h>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Clock                          = std::chrono::steady_clock;
constexpr qsizetype maximumBytes     = 1024 * 1024;
volatile quint64    consumedChecksum = 0;

void require(bool condition, const QString& error)
{
    if (!condition)
    {
        throw std::runtime_error(error.toStdString());
    }
}

double elapsedNs(Clock::time_point start)
{
    return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
}

// Sample output bytes, including binary payload, without measuring a second full byte scan.
quint64 consume(const char* bytes, size_t size)
{
    return size + static_cast<unsigned char>(bytes[0]) + static_cast<unsigned char>(bytes[size / 2])
         + static_cast<unsigned char>(bytes[size - 1]);
}

template <typename Render>
QJsonObject measure(Render render, qsizetype packetSize)
{
    auto trial = [&](quint64 iterations) {
        quint64    checksum = 0;
        quint64    bytes    = 0;
        const auto start    = Clock::now();
        for (quint64 i = 0; i < iterations; ++i)
        {
            const auto output  = render(i % 1000);
            bytes             += output.size();
            checksum          += consume(output.data(), static_cast<size_t>(output.size()));
        }
        const double duration = elapsedNs(start);
        consumedChecksum      = checksum;
        return std::pair(duration, bytes);
    };

    trial(64);
    quint64 iterations = 64;
    for (;;)
    {
        const auto [duration, bytes] = trial(iterations);
        if (duration >= 10e6 || iterations >= 1000000)
        {
            break;
        }
        const auto estimate = static_cast<quint64>(iterations * 15e6 / std::max(duration, 1.0));
        iterations          = std::clamp(estimate, iterations + 1, quint64(1000000));
    }

    std::vector<double> samples;
    quint64             outputBytes = 0;
    for (int sample = 0; sample < 5; ++sample)
    {
        const auto [duration, bytes] = trial(iterations);
        samples.push_back(duration / iterations);
        outputBytes = bytes;
    }
    std::sort(samples.begin(), samples.end());
    const double median = samples[2];
    return QJsonObject {
        {   "iterations_per_sample",                                static_cast<double>(iterations)},
        {                 "samples",                                                              5},
        {    "median_ns_per_packet",                                                         median},
        {       "min_ns_per_packet",                                                samples.front()},
        {       "max_ns_per_packet",                                                 samples.back()},
        {      "packets_per_second",                                                   1e9 / median},
        {"payload_bytes_per_second",                                      packetSize * 1e9 / median},
        { "output_bytes_per_second", (static_cast<double>(outputBytes) / iterations) * 1e9 / median}
    };
}
} // namespace

int main()
{
    const char* activePattern    = "<none>";
    const char* operation        = "setup";
    qsizetype   activePacketSize = 0;
    try
    {
        const std::array patterns {"{packet}",
                                   "#{index:03} {packet}",
                                   "[{datetime}] #{index:03} {unix:.6f} {packet}",
                                   "[{datetime}] #{index:08x}/{index:06d} {unix:.6f}/{unix:020.3f} {packet}"};
        const std::array<qsizetype, 4> packetSizes {16, 128, 1024, 65536};
        QJsonArray                     results;
        for (const char* pattern : patterns)
        {
            for (const qsizetype packetSize : packetSizes)
            {
                activePattern    = pattern;
                activePacketSize = packetSize;
                operation        = "prepare";
                QByteArray packet(packetSize, Qt::Uninitialized);
                for (qsizetype i = 0; i < packetSize; ++i)
                {
                    packet[i] = static_cast<char>(i % 256);
                }

                FormatSchema schema;
                const auto   dateSlot   = schema.add<QString>(QStringLiteral("datetime"));
                const auto   indexSlot  = schema.add<quint64>(QStringLiteral("index"));
                const auto   unixSlot   = schema.add<double>(QStringLiteral("unix"));
                const auto   packetSlot = schema.add<QByteArrayView>(QStringLiteral("packet"));
                QString      error;
                const auto   start     = Clock::now();
                const auto   plan      = CompiledFormat::compile(QString::fromUtf8(pattern), schema, &error);
                const double prepareNs = elapsedNs(start);
                require(static_cast<bool>(plan), error);
                auto                   values   = schema.createValues();
                const QString          date     = QStringLiteral("2026-11-10 12:34:56.789");
                const std::string      dateUtf8 = date.toStdString();
                const double           unixTime = 1794314096.789123;
                const fmt::string_view packetView(packet.constData(), static_cast<size_t>(packet.size()));
                require(values.set(dateSlot, date, &error), error);
                require(values.set(unixSlot, unixTime, &error), error);
                require(values.set(packetSlot, QByteArrayView(packet), &error), error);
                QByteArray  preparedOutput;
                std::string fmtOutput;
                fmtOutput.reserve(static_cast<size_t>(packetSize) + 256);
                quint64 index  = 0;
                // fmt 12 counts named argument types before removing const qualifiers.
                // Keep the named wrappers mutable so make_format_args retains their names.
                auto dateArg   = fmt::arg("datetime", dateUtf8);
                auto indexArg  = fmt::arg("index", index);
                auto unixArg   = fmt::arg("unix", unixTime);
                auto packetArg = fmt::arg("packet", packetView);
                auto prepared  = [&](quint64 nextIndex) {
                    require(values.set(indexSlot, nextIndex, &error), error);
                    require(plan->render(values, preparedOutput, maximumBytes, &error), error);
                    return QByteArrayView(preparedOutput);
                };
                auto runtime = [&](quint64 nextIndex) {
                    index = nextIndex;
                    fmtOutput.clear();
                    // The packed argument store snapshots scalar values; rebuild this stack-only
                    // store for index updates, retaining names and borrowed payload storage.
                    fmt::vformat_to(std::back_inserter(fmtOutput),
                                    pattern,
                                    fmt::make_format_args(dateArg, indexArg, unixArg, packetArg));
                    return fmt::string_view(fmtOutput);
                };

                operation = "output equivalence";
                for (const quint64 testIndex : {quint64(0), quint64(42), quint64(999)})
                {
                    const auto expected = runtime(testIndex);
                    const auto actual   = prepared(testIndex);
                    require(actual.size() == static_cast<qsizetype>(expected.size())
                                && std::equal(actual.begin(), actual.end(), expected.begin()),
                            QStringLiteral("Output mismatch: %1, packet size %2, index %3")
                                .arg(QString::fromUtf8(pattern))
                                .arg(packetSize)
                                .arg(testIndex));
                }

                operation = "timing";
                QJsonObject result {
                    {"pattern", QString::fromUtf8(pattern)},
                    {"packet_bytes", static_cast<double>(packetSize)},
                    {"prepared_compile_ns", prepareNs},
                    {"prepared", measure(prepared, packetSize)},
                    {"fmt_runtime", measure(runtime, packetSize)}
                };
                results.append(result);
            }
        }
        const QJsonObject report {
            {  "benchmark",                       "FormattingBenchmark"                           },
            {"fmt_version",                                                            FMT_VERSION},
            {     "timing",
             "Render, index update, owning output and sampled-byte checksum; no I/O. "
             "Prepared parsing excluded; fmt runtime parsing included. Five samples after warmup."},
            {    "outputs",
             "Prepared: QByteArray with atomic replacement and byte limit. "
             "fmt runtime: reused owning std::string, without atomic replacement or byte limit. "
             "No output conversion. Binary payload uses explicit lengths."                        },
            {    "results",                                                                results},
            {   "checksum",                                  static_cast<double>(consumedChecksum)}
        };
        const QByteArray json = QJsonDocument(report).toJson(QJsonDocument::Indented);
        return std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout)
                    == static_cast<size_t>(json.size()) ?
                   0 :
                   1;
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr,
                     "Formatting benchmark failed during %s: pattern %s, packet size %lld: %s\n",
                     operation,
                     activePattern,
                     static_cast<long long>(activePacketSize),
                     exception.what());
        return 1;
    }
}
