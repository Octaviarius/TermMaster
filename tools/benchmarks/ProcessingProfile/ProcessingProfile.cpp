#include "Nodes/Bypass.h"
#include "Nodes/Framer.h"
#include "Nodes/PatternFramer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextStream>
#include <Termaster/Core/Processing/Lineflow.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <cstdlib>
#include <functional>
#include <memory>

namespace
{
void require(bool condition, const QString& message)
{
    if (!condition)
    {
        qFatal("%s", qPrintable(message));
    }
}
using Factory = std::function<std::shared_ptr<Processor>()>;
void measure(const QString& name, const Factory& factory, QByteArray packet, int iterations)
{
    const auto filter = qEnvironmentVariable("TERMASTER_PROCESSING_PROFILE_CASE");
    if (!filter.isEmpty() && !name.contains(filter))
    {
        return;
    }
    QTextStream out(stdout);
    for (int repeat = -1; repeat < 5; ++repeat)
    {
        auto processor = factory();
        require(bool(processor), "Factory failed: " + name);
        qint64        outputBytes = 0;
        qint64        outputs     = 0;
        QElapsedTimer timer;
        timer.start();
        if (name.contains("event-driven"))
        {
            QEventLoop            loop;
            int                   remaining = iterations;
            std::function<void()> submit;
            submit = [&] {
                const auto result = processor->accept({packet},
                                                      {[&](Processor::Chunk chunk) {
                                                           outputBytes += chunk.bytes.size();
                                                           ++outputs;
                                                       },
                                                       [&](QString error) {
                                                           require(error.isEmpty(), name + ": " + error);
                                                           if (--remaining == 0)
                                                           {
                                                               loop.quit();
                                                           }
                                                           else
                                                           {
                                                               submit();
                                                           }
                                                       }});
                require(result == Processor::AcceptResult::Accepted, "Not accepted: " + name);
            };
            submit();
            loop.exec();
        }
        else
        {
            for (int index = 0; index < iterations; ++index)
            {
                bool completed = false;
                auto result    = processor->accept({packet},
                                                   {[&](Processor::Chunk chunk) {
                                                     outputBytes += chunk.bytes.size();
                                                     ++outputs;
                                                    },
                                                    [&](QString error) {
                                                     require(error.isEmpty(), name + ": " + error);
                                                     completed = true;
                                                    }});
                require(result == Processor::AcceptResult::Accepted, "Not accepted: " + name);
                while (!completed)
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents);
                    require(timer.elapsed() < 60000, "Timed out: " + name);
                }
            }
        }
        const auto elapsed = timer.nsecsElapsed();
        if (name.contains("bypass"))
        {
            require(outputs == iterations && outputBytes == qint64(iterations) * packet.size(),
                    "Bypass output mismatch");
        }
        if (name.contains("partial-growing"))
        {
            require(outputs == 0 && outputBytes == 0, "Partial frame unexpectedly emitted");
        }
        bool cancelled = false;
        processor->cancel([&] { cancelled = true; });
        while (!cancelled)
        {
            QCoreApplication::processEvents();
        }
        if (repeat >= 0)
        {
            out << name << ',' << packet.size() << ',' << iterations << ',' << repeat << ',' << elapsed << ','
                << outputs << ',' << outputBytes << '\n';
            out.flush();
        }
    }
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication  app(argc, argv);
    ProcessorRegistry registry;
    registerNativeProcessors(registry, "benchmark");
    QTextStream(stdout) << "case,packet_bytes,iterations,repeat,elapsed_ns,output_packets,output_bytes\n";
    for (const int size : {1, 64, 4096})
    {
        QByteArray packet(size, 'a');
        const int  iterations = 20000;
        measure("direct-bypass", [] { return Bypass::create({}, {}); }, packet, iterations);
        for (const int depth : {1, 4, 16})
        {
            QVariantList nodes;
            for (int index = 0; index < depth; ++index)
            {
                nodes.append(QVariantMap {
                    {         "name", QString("node%1").arg(index)},
                    {         "type",              "native.bypass"},
                    {"configuration",               QVariantMap {}}
                });
            }
            QVariantMap blocks {
                {"root", QVariantMap {{"id", "root"}, {"name", "Root"}, {"nodes", nodes}}}
            };
            QString error;
            auto    plan = Lineflow::prepare(blocks, "root", registry, {}, &error);
            require(bool(plan), error);
            measure(
                QString("lineflow-bypass-%1").arg(depth),
                [&] { return plan->instantiate(registry); },
                packet,
                iterations);
            measure(
                QString("lineflow-event-driven-bypass-%1").arg(depth),
                [&] { return plan->instantiate(registry); },
                packet,
                iterations);
        }
        for (const auto& id : {"native.hex-encoder", "native.base64-encoder", "native.compress"})
        {
            measure(id, [&registry, id] { return registry.create(id, {}, {}); }, packet, iterations);
        }
        measure(
            "framer-size4096",
            [] {
                return Framer::create(
                    {
                        {   "ending", "\\r"},
                        {"frameSize",  4096}
                },
                    {});
            },
            packet,
            iterations);
        if (size > 1)
        {
            auto dense = packet;
            for (int index = 1; index < size; index += 2)
            {
                dense[index] = '\r';
            }
            Processor::Limits limits;
            limits.maxChunks = 8192;
            measure("framer-dense", [limits] { return Framer::create({}, limits); }, dense, iterations);
        }
    }
    for (const int size : {1, 64, 4096})
    {
        Processor::Limits limits;
        limits.maxBytes      = 2 * 1024 * 1024;
        const int iterations = 65536 / size;
        measure(
            "pattern-partial-growing",
            [limits] {
                return PatternFramer::create(
                    {
                        {"pattern", "(?<payload>a+)z"}
                },
                    limits);
            },
            QByteArray(size, 'a'),
            iterations);
        measure(
            "delimiter-partial-growing",
            [limits] {
                return Framer::create(
                    {
                        {"ending", "aaaaaz"}
                },
                    limits);
            },
            QByteArray(size, 'a'),
            iterations);
    }
}
