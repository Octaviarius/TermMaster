#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QPointer>
#include <QTextStream>
#include <QTimer>
#include <Termaster/Core/Processing/Codeflow.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ThreadedProcessor.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace
{
void require(bool condition, const QString& message)
{
    if (!condition)
    {
        throw std::runtime_error(message.toStdString());
    }
}

double milliseconds(const QElapsedTimer& timer)
{
    return double(timer.nsecsElapsed()) / 1000000.0;
}

double percentile(std::vector<double> values, double fraction)
{
    std::sort(values.begin(), values.end());
    return values.at(size_t(std::ceil(fraction * double(values.size()))) - 1);
}

QString script(bool paired)
{
    const QString prefix = "import time\n_import_start = time.perf_counter_ns()\nimport zlib\n"
                           "_import_ns = time.perf_counter_ns() - _import_start\n"
                           "from termaster import Processor, Input, Output, handles\n"
                           "class Codeflow(Processor):\n";
    if (!paired)
    {
        return prefix
             + "    incoming = Input()\n    outgoing = Output()\n"
               "    @handles(incoming, output=outgoing)\n    def process(self, data):\n"
               "        if data == b'__profile_import__': return str(_import_ns).encode()\n"
               "        return data\n";
    }
    return prefix
         + "    rx = Input(flow='rx')\n    rxout = Output(flow='rx')\n"
           "    tx = Input(flow='tx')\n    txout = Output(flow='tx')\n"
           "    @handles(rx, output=rxout)\n    def receive(self, data): return data\n"
           "    @handles(tx, output=txout)\n    def send(self, data): return data\n";
}

class Profile
{
public:
    ProcessorRegistry                 registry;
    QElapsedTimer                     total;
    QList<std::shared_ptr<Processor>> instances;
    QString                           error;

    Profile()
    {
        total.start();
        registerNativeProcessors(registry, "native");
        Codeflow::registerProvider(registry);
    }

    ~Profile()
    {
        for (const auto& processor : instances)
        {
            stop(processor);
        }
        instances.clear();
        registry.shutdown(1000);
    }

    void budget() const
    {
        require(total.elapsed() < 28000, "Codeflow profile exceeded its 28-second work budget");
    }

    QVariantMap definition(const QString& id, bool paired, double* discovery = nullptr)
    {
        const QVariantMap source {
            {  "code",            script(paired)},
            {"python", Codeflow::defaultPython()}
        };
        QElapsedTimer clock;
        clock.start();
        const auto description = Codeflow::inspect(source, &error);
        require(!description.isEmpty(), error);
        if (discovery)
        {
            *discovery = milliseconds(clock);
        }
        return Codeflow::definition(id, id, source, description);
    }

    void publish(const QVariantMap& definitions)
    {
        const auto parsed = ProcessorRegistry::parseDefinitions(definitions, &error);
        require(bool(parsed), error);
        require(registry.publishDefinitions("profile", *parsed, &error), error);
    }

    std::shared_ptr<Processor> create(const QString& id)
    {
        budget();
        const auto prepared = registry.prepare(id, {}, {}, &error);
        require(bool(prepared), error);
        const auto processor = ThreadedProcessor::create(prepared, {}, &error);
        require(bool(processor), error);
        instances.append(processor);
        return processor;
    }

    QList<QByteArray> packet(Processor&           processor,
                             const QByteArray&    input,
                             bool                 paired    = false,
                             std::vector<double>* latencies = nullptr,
                             double*              skew      = nullptr)
    {
        budget();
        QEventLoop loop;
        QTimer     timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        struct Result
        {
            QElapsedTimer        clock;
            QPointer<QEventLoop> loop;
            int                  completed = 0;
            QString              failure;
            QList<QByteArray>    outputs;
            double               finish[2] {};
        };
        const auto state = std::make_shared<Result>();
        state->clock.start();
        state->loop     = &loop;
        const int count = paired ? 2 : 1;
        for (int port = 0; port < count; ++port)
        {
            const auto result = processor.accept({input, port},
                                                 {[state](Processor::Chunk chunk) {
                                                      state->outputs.append(std::move(chunk.bytes));
                                                  },
                                                  [state, port, count](QString reason) {
                                                      if (!reason.isEmpty())
                                                      {
                                                          state->failure = std::move(reason);
                                                      }
                                                      state->finish[port] = milliseconds(state->clock);
                                                      ++state->completed;
                                                      if (state->completed == count && state->loop)
                                                      {
                                                          state->loop->quit();
                                                      }
                                                  }});
            require(result == Processor::AcceptResult::Accepted, "Profile facade rejected a sequential input");
        }
        if (state->completed != count)
        {
            timer.start(2000);
            loop.exec();
        }
        require(state->completed == count, "Codeflow profile packet timed out");
        require(state->failure.isEmpty(), state->failure);
        if (latencies)
        {
            for (int port = 0; port < count; ++port)
            {
                latencies->push_back(state->finish[port]);
            }
        }
        if (skew)
        {
            *skew = std::abs(state->finish[0] - state->finish[1]);
        }
        return state->outputs;
    }

    QVariantMap sample(Processor& processor, int size, int iterations)
    {
        const QByteArray data(size, char(0xa5));
        for (int i = 0; i < 5; ++i)
        {
            require(packet(processor, data) == QList<QByteArray> {data}, "Warmup output mismatch");
        }
        std::vector<double> latencies;
        QElapsedTimer       clock;
        clock.start();
        for (int i = 0; i < iterations; ++i)
        {
            require(packet(processor, data, false, &latencies) == QList<QByteArray> {data}, "Packet output mismatch");
        }
        const double elapsed = milliseconds(clock);
        return {
            {"packetBytes", size},
            {"packets", iterations},
            {"elapsedMs", elapsed},
            {"throughputMiBps", double(size) * iterations / (1024.0 * 1024.0) / (elapsed / 1000.0)},
            {"packetsps", iterations / (elapsed / 1000.0)},
            {"p50packetms", percentile(latencies, 0.50)},
            {"p95packetms", percentile(latencies, 0.95)}
        };
    }

    double stop(const std::shared_ptr<Processor>& processor) noexcept
    {
        QElapsedTimer clock;
        clock.start();
        QEventLoop loop;
        QTimer     timer;
        timer.setSingleShot(true);
        auto                 stopped = std::make_shared<bool>(false);
        QPointer<QEventLoop> guard(&loop);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        processor->cancel([stopped, guard] {
            *stopped = true;
            if (guard)
            {
                guard->quit();
            }
        });
        if (!*stopped)
        {
            timer.start(1500);
            loop.exec();
        }
        return *stopped ? milliseconds(clock) : -1.0;
    }
};
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QVariantMap      report {
        {  "measurement",      "single-run synthetic sequential packets"},
        {"configuration", QStringLiteral(CODEFLOW_PROFILE_CONFIGURATION)},
        {       "python",                      Codeflow::defaultPython()}
    };
    int status = 0;
    try
    {
        Profile            profile;
        double             discovery = 0;
        const auto         single    = profile.definition("single", false, &discovery);
        const auto         paired    = profile.definition("paired", true);
        const QVariantList nodes {
            QVariantMap { {"name", "first"}, {"type", "single"}},
            QVariantMap {{"name", "second"}, {"type", "single"}}
        };
        const QVariantMap shared {
            {    "id",                       "shared"},
            {  "name",                       "Shared"},
            {  "kind",                     "lineflow"},
            {"source", QVariantMap {{"nodes", nodes}}}
        };
        profile.publish({
            {"single", single},
            {"paired", paired},
            {"shared", shared}
        });
        report.insert("coldDescribeMs", discovery);
        QVariantMap cases;
        for (const auto& id : QStringList {"native.bypass", "single", "shared"})
        {
            QElapsedTimer startup;
            startup.start();
            const auto processor = profile.create(id);
            require(profile.packet(*processor, "ready") == QList<QByteArray> {"ready"}, "Startup output mismatch");
            QVariantMap item {
                {"startupms", milliseconds(startup)}
            };
            if (id == "single")
            {
                const auto imported = profile.packet(*processor, "__profile_import__");
                require(imported.size() == 1, "Import timing output mismatch");
                bool       valid = false;
                const auto nanos = imported.first().toLongLong(&valid);
                require(valid && nanos >= 0, "Invalid interpreter import duration");
                item.insert("coldimportms", double(nanos) / 1000000.0);
            }
            item.insert("1KiB", profile.sample(*processor, 1024, 1000));
            item.insert("64KiB", profile.sample(*processor, 65536, 100));
            const double stop = profile.stop(processor);
            require(stop >= 0, "Codeflow cancellation exceeded the profile grace period");
            item.insert("stopms", stop);
            cases.insert(id, item);
        }
        report.insert("cases", cases);
        const auto       processor = profile.create("paired");
        const QByteArray packet(1024, char(0x5a));
        require(profile.packet(*processor, packet, true) == QList<QByteArray> {packet, packet},
                "Paired warmup mismatch");
        std::vector<double> latencies;
        double              maxSkew = 0;
        QElapsedTimer       fairness;
        fairness.start();
        for (int i = 0; i < 250; ++i)
        {
            double skew = 0;
            require(profile.packet(*processor, packet, true, &latencies, &skew) == QList<QByteArray> {packet, packet},
                    "Paired output mismatch");
            maxSkew = std::max(maxSkew, skew);
        }
        const double elapsed = milliseconds(fairness);
        report.insert("pairedRxTx",
                      QVariantMap {
                          {"rxPackets", 250},
                          {"txPackets", 250},
                          {"elapsedMs", elapsed},
                          {"packetsps", 500.0 / (elapsed / 1000.0)},
                          {"maxCompletionSkewMs", maxSkew},
                          {"p50packetms", percentile(latencies, 0.50)},
                          {"p95packetms", percentile(latencies, 0.95)}
        });
        require(profile.stop(processor) >= 0, "Paired cancellation timed out");
        report.insert("totalms", milliseconds(profile.total));
    }
    catch (const std::exception& error)
    {
        report.insert("error", QString::fromUtf8(error.what()));
        status = 1;
    }
    QTextStream(stdout) << QJsonDocument::fromVariant(report).toJson(QJsonDocument::Indented);
    return status;
}
