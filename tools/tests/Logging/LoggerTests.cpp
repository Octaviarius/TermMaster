#ifdef _WIN32
#include <windows.h>
#endif
#include <QTemporaryDir>
#include <Termaster/Core/Logging/Logger.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace
{

void check(bool condition, const std::source_location& source = std::source_location::current())
{
    if (!condition)
    {
        throw std::runtime_error("logger_check_" + std::to_string(source.line()));
    }
}

std::string read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct Directory
{
    QTemporaryDir         temporary;
    std::filesystem::path path;

    explicit Directory(std::string_view name)
    {
        (void)name;
        check(temporary.isValid());
#ifdef _WIN32
        path = temporary.path().toStdWString();
#else
        path = temporary.path().toStdString();
#endif
    }
};

bool privateWrite(const std::filesystem::path& path, std::string_view bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

void valuesAndFormatting()
{
    Logger     log("core");
    const auto record = log.info("Message",
                                 {
                                     {    "mtu",                                                             1500},
                                     {  "ports",                                                      {443, 1194}},
                                     {"enabled",                                                             true},
                                     {  "empty",                                                          nullptr},
                                     {   "name",                                                "quoted\"\nvalue"},
                                     { "object", Logger::Value::Object {{"count", 2}, {"labels", {"one", "two"}}}}
    });
    check(record.has_value());
    check(record->tag == "core" && record->message == "Message");
    check(
        record->parameters
        == "mtu=1500,ports=[443,1194],enabled=true,empty=null,name=\"quoted\\\"\\u000avalue\",object={\"count\":2,\"labels\":[\"one\",\"two\"]}");
    check(record->timestamp.size() == 29 && record->timestamp[23] != 'Z' && record->timestamp[26] == ':');
    const auto line = Logger::format(*record);
    check(line.starts_with(record->timestamp + " INFO core Message mtu=1500,ports=[443,1194]"));
    check(line.find('\n') == std::string::npos && line.find('\r') == std::string::npos);
    check(Logger::Value(std::vector<int> {1, 2}).format() == "[1,2]");
    check(Logger::Value(std::numeric_limits<double>::infinity()).format() == "\"number:+Infinity\"");
    check(Logger::Value(-std::numeric_limits<double>::infinity()).format() == "\"number:-Infinity\"");
    check(Logger::Value(std::numeric_limits<double>::quiet_NaN()).format() == "\"number:NaN\"");
    check(Logger::Value(std::numeric_limits<std::uint64_t>::max()).format() == "18446744073709551615");
    const auto controls = log.warning("line1\nline2\r",
                                      {
                                          {"bad\nkey", "line\nvalue"}
    });
    check(controls->message == "line1\\u000aline2\\u000d");
    check(Logger::format(*controls).find('\n') == std::string::npos);
    check(Logger::Value(std::string(1, static_cast<char>(0xff))).format() == "\"\\u00ff\"");
    check(Logger::Value("Ж😀").format() == "\"Ж😀\"");
    check(log.info("invalid key",
                   {
                       {std::string(1, static_cast<char>(0xff)), "value"}
    })
              ->parameters
          == "\"\\u00ff\"=\"value\"");
    Logger::Value nested(1);
    for (unsigned i = 0; i < 20; ++i)
    {
        nested = Logger::Value(Logger::Value::Array {std::move(nested)});
    }
    check(nested.format() == "\"[truncated]\"");
    check(log.info("deep",
                   {
                       {"nested", nested}
    })
              ->parameters
          == "truncated=true");
    check(log.info("large",
                   {
                       {"large", std::string(10000, 'x')}
    })
              ->parameters
          == "truncated=true");
    check(log.info(std::string(10000, 'x'))->message.size() < 1100);
    const std::string cyrillic = "Ж";
    const std::string emoji    = "😀";
    check(log.info(std::string(1023, 'x') + cyrillic)->message == std::string(1023, 'x') + "[truncated]");
    check(log.info(std::string(1022, 'x') + cyrillic + "x")->message
          == std::string(1022, 'x') + cyrillic + "[truncated]");
    check(log.info(std::string(1021, 'x') + emoji)->message == std::string(1021, 'x') + "[truncated]");
    check(log.info(std::string(1020, 'x') + emoji + "x")->message == std::string(1020, 'x') + emoji + "[truncated]");
}

void contextAndObservers()
{
    Logger                      logger("app");
    auto                        first  = logger.withContext({"first", "session-one", 3});
    auto                        second = logger.withContext({"second", "session-two", 1});
    std::vector<Logger::Record> received;
    auto                        subscription = logger.subscribe([&](const Logger::Record& record) {
        received.push_back(record);
        /** Logging and querying the family from a callback must not deadlock. */
        check(!logger.history().empty());
        if (record.message == "outer")
        {
            second.info("nested");
        }
    });
    first.child("protocol").info("outer");
    check(received.size() == 2);
    check(received[0].profileId == "first" && received[0].sessionId == "session-one" && received[0].attempt == 3);
    check(received[1].profileId == "second" && received[1].sessionId == "session-two" && received[1].attempt == 1);
    check(received[0].tag == "app:protocol");
    const auto ordinary = logger.info("ordinary");
    check(ordinary && !ordinary->profileId && !ordinary->sessionId && ordinary->attempt == 0);
    subscription.reset();
    first.info("after cancellation");
    check(received.size() == 3);

    Logger::Subscription self;
    unsigned             calls {};
    self = logger.subscribe([&](const Logger::Record&) {
        ++calls;
        self.reset();
    });
    logger.info("self cancellation");
    logger.info("cancelled");
    check(calls == 1);

    std::atomic_bool entered {};
    std::atomic_bool release {};
    std::atomic_bool resetFinished {};
    auto             waiting = logger.subscribe([&](const Logger::Record&) {
        entered.store(true);
        while (!release.load())
        {
            std::this_thread::yield();
        }
    });
    std::thread      writer([&] { first.info("in flight"); });
    while (!entered.load())
    {
        std::this_thread::yield();
    }
    std::thread cancel([&] {
        waiting.reset();
        resetFinished.store(true);
    });
    check(!resetFinished.load());
    release.store(true);
    writer.join();
    cancel.join();
    check(resetFinished.load());
    logger.info("after joined cancellation");
}

void hierarchy()
{
    Logger root;
    check(root.tag() == "root");
    auto core    = root.child("core");
    auto network = core.child("network");
    auto sibling = root.child("corex");
    check(network.tag() == "root:core:network");
    core.setLevel(Logger::Level::warning);
    const auto tags = root.tags();
    check(tags == std::vector<std::string>({"root", "root:core", "root:core:network", "root:corex"}));
    check(!core.info("filtered") && !network.info("filtered"));
    check(sibling.info("allowed").has_value());
    network.setLevel(Logger::Level::debug);
    check(network.debug("override").has_value());
    network.clearLevel();
    check(!network.info("inherited"));
    core.clearLevel();
    check(network.info("default").has_value());
    auto copied = core;
    copied.setLevel(Logger::Level::critical);
    check(!core.error("shared override"));
    Logger independent("core");
    check(independent.info("independent").has_value());
    check(independent.tag() == "core");
    check(root.history().size() == 3 && independent.history().size() == 1);
    auto survivor = [] {
        Logger parent("parent");
        return parent.child("child");
    }();
    check(survivor.info("parent destroyed").has_value());
}

void historyAndSettings()
{
    Logger           logger;
    Logger::Settings settings;
    settings.historyLimit = 16;
    check(logger.configure(settings).empty());
    check(!logger.debug("filtered"));
    for (unsigned i = 0; i < 20; ++i)
    {
        check(logger.info("public message").has_value());
    }
    const auto records = logger.history();
    check(records.size() == 16 && records.front().sequence == 5 && records.back().sequence == 20);
    check(logger.dropped() == 4 && logger.history(18).size() == 2 && logger.history(0, 0).empty());
    settings.historyLimit = 15;
    check(logger.configure(settings) == "logging_invalid_settings" && logger.settings().historyLimit == 16);
    settings.historyLimit = 16;
    settings.file         = true;
    check(logger.configure(settings) == "logging_file_unavailable" && !logger.settings().file);
    settings.file = false;
    settings.gui  = false;
    check(logger.configure(settings).empty() && logger.history().empty());
    check(logger.warning("accepted without history").has_value() && logger.history().empty());
    settings.minimumLevel = static_cast<Logger::Level>(99);
    check(logger.configure(settings) == "logging_invalid_settings");
    check(!logger.write(static_cast<Logger::Level>(99), "invalid"));
    check(Logger::parseLevel("warning") == Logger::Level::warning && !Logger::parseLevel("WARNING"));
}

void configuredTags()
{
    Logger           logger("app");
    auto             profile = logger.child("profile");
    auto             parser  = profile.child("parser");
    auto             sibling = logger.child("profilex");
    Logger::Settings settings;
    settings.minimumLevel = Logger::Level::error;
    settings.tagLevels    = {
        {       "app:profile",   Logger::Level::debug},
        {"app:profile:parser", Logger::Level::warning}
    };
    check(logger.configure(settings).empty());
    const auto tags = logger.tags();
    check(std::find(tags.begin(), tags.end(), "app:profile") != tags.end());
    check(profile.debug("configured child").has_value());
    check(!parser.info("specific child rule"));
    check(!sibling.warning("not a tag segment"));
    check(!logger.warning("global fallback"));
    settings.tagLevels.erase("app:profile:parser");
    check(logger.configure(settings).empty());
    check(parser.debug("parent inherited after removal").has_value());
    settings.tagLevels.clear();
    check(logger.configure(settings).empty());
    check(!profile.warning("global inherited after removal"));
    settings.tagLevels["bad::tag"] = Logger::Level::trace;
    check(logger.configure(settings) == "logging_invalid_settings");
    check(logger.settings().tagLevels.empty());
    settings.tagLevels.clear();
    settings.tagLevels["app"] = static_cast<Logger::Level>(99);
    check(logger.configure(settings) == "logging_invalid_settings");
}

void concurrency()
{
    Logger           logger("parallel");
    Logger::Settings settings;
    settings.historyLimit = 4096;
    check(logger.configure(settings).empty());
    std::atomic<bool>        failed {};
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 4; ++worker)
    {
        workers.emplace_back([&, worker] {
            auto local = logger.child(std::to_string(worker));
            for (unsigned i = 0; i < 250; ++i)
            {
                if (!local.info("concurrent",
                                {
                                    {"number", i}
                }))
                {
                    failed = true;
                }
                static_cast<void>(local.history(0, 2));
            }
        });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    check(!failed);
    const auto records = logger.history(0, 4096);
    check(records.size() == 1000 && logger.dropped() == 0);
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        check(records[i].sequence == i + 1);
    }
}

void asynchronousFiles()
{
    Directory        directory("async-order");
    Logger           logger("async", directory.path);
    Logger::Settings settings;
    settings.file             = true;
    settings.historyLimit     = 4096;
    settings.maximumFileBytes = 16 * 1024 * 1024;
    check(logger.configure(settings).empty());

    std::vector<std::thread> producers;
    for (unsigned index = 0; index < 4; ++index)
    {
        producers.emplace_back([&, index] {
            auto child = logger.child(std::to_string(index));
            for (unsigned record = 0; record < 100; ++record)
            {
                child.info("Ordered output",
                           {
                               {"index", record}
                });
            }
        });
    }
    for (auto& producer : producers)
    {
        producer.join();
    }

    check(logger.flush().empty());
    check(logger.outputDropped() == 0);
    std::string expected;
    for (const auto& record : logger.history(0, 4096))
    {
        expected += Logger::format(record) + '\n';
    }
    check(read(directory.path / "application.log") == expected);

    logger.info("Before configuration boundary");
    settings.file = false;
    check(logger.configure(settings).empty());
    logger.info("After disabled boundary");
    check(logger.flush().empty());
    const auto bytes = read(directory.path / "application.log");
    check(bytes.find("Before configuration boundary") != std::string::npos);
    check(bytes.find("After disabled boundary") == std::string::npos);

    Directory lifetime("async-lifetime");
    {
        auto child = [&] {
            Logger parent("parent", lifetime.path);
            settings.file = true;
            check(parent.configure(settings).empty());
            parent.info("Parent released");
            return parent.child("child");
        }();
        child.info("Child remains usable");
    }
    const auto drained = read(lifetime.path / "application.log");
    check(drained.find("Parent released") != std::string::npos);
    check(drained.find("Child remains usable") != std::string::npos);
}

void queueAccounting()
{
    Directory        directory("async-pressure");
    Logger           logger("pressure", directory.path);
    Logger::Settings settings;
    settings.file             = true;
    settings.gui              = false;
    settings.maximumFileBytes = 16 * 1024 * 1024;
    check(logger.configure(settings).empty());

    const auto         started = std::chrono::steady_clock::now();
    constexpr unsigned records = 10000;
    for (unsigned index = 0; index < records; ++index)
    {
        check(logger
                  .info("Bounded output",
                        {
                            {"index", index}
        })
                  .has_value());
    }
    const auto queuedAt   = std::chrono::steady_clock::now();
    const auto failure    = logger.flush();
    const auto finishedAt = std::chrono::steady_clock::now();
    std::cerr << "logger_pressure records=" << records
              << " enqueueUs=" << std::chrono::duration_cast<std::chrono::microseconds>(queuedAt - started).count()
              << " drainUs=" << std::chrono::duration_cast<std::chrono::microseconds>(finishedAt - queuedAt).count()
              << " outputDropped=" << logger.outputDropped() << '\n';
    const auto lost = logger.outputDropped();
    check(failure == (lost ? "logging_queue_full" : ""));
    const auto bytes = read(directory.path / "application.log");
    check(static_cast<std::uint64_t>(std::count(bytes.begin(), bytes.end(), '\n')) + lost == records);
    check(logger.dropped() == 0);

    settings.file = false;
    check(logger.configure(settings).empty());
    logger.info("In-memory after pressure");
    check(logger.flush().empty());
    check(logger.outputDropped() == lost);
}

void files()
{
    Directory        directory("rotation");
    Logger::Settings settings;
    settings.file             = true;
    settings.maximumFileBytes = 4096;
    settings.retainedFiles    = 2;
    {
        Logger logger("file", directory.path);
        check(logger.configure(settings).empty());
        check(read(directory.path / "application.log").empty());
        check(logger.info("Readable while logger is alive").has_value());
        check(logger.flush().empty());
        check(read(directory.path / "application.log").find("Readable while logger is alive") != std::string::npos);
#ifdef _WIN32
        const auto reader = CreateFileW((directory.path / "application.log").c_str(),
                                        GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr,
                                        OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL,
                                        nullptr);
        check(reader != INVALID_HANDLE_VALUE);
        const auto record = logger.info("Readable without delete sharing");
        check(logger.flush().empty());
        CloseHandle(reader);
        check(record.has_value() && logger.error().empty());
#endif
        for (unsigned i = 0; i < 48; ++i)
        {
            check(logger
                      .info("Rotation message",
                            {
                                {"index", i},
                                {"padding", std::string(256, 'x')}
            })
                      .has_value());
        }
        check(logger.flush().empty());
        check(logger.error().empty());
    }
    check(std::filesystem::exists(directory.path / "application.log.1"));
    check(std::filesystem::exists(directory.path / "application.log.2"));
    check(!std::filesystem::exists(directory.path / "application.log.3"));
    for (const auto& item : std::filesystem::directory_iterator(directory.path))
    {
        check(item.file_size() <= 4096);
        check(!read(item.path()).starts_with("Application log"));
        check(read(item.path()).find(" INFO file Rotation message index=") != std::string::npos);
    }
    {
        Logger logger("file", directory.path);
        check(logger.configure(settings).empty());
        settings.retainedFiles = 0;
        check(logger.configure(settings).empty());
        for (unsigned i = 0; i < 48; ++i)
        {
            logger.info("Rotation message " + std::string(256, 'x'));
        }
        check(logger.flush().empty());
        check(logger.error().empty());
        check(!std::filesystem::exists(directory.path / "application.log.1"));
        check(!std::filesystem::exists(directory.path / "application.log.2"));
    }
    Directory foreign("foreign");
    check(privateWrite(foreign.path / "application.log", "foreign-content"));
    Logger logger("foreign", foreign.path);
    check(logger.configure(settings) == "logging_file_unavailable");
    check(read(foreign.path / "application.log") == "foreign-content" && !logger.settings().file);
    Directory unsafe("hardlink");
    check(privateWrite(unsafe.path / "original", "synthetic-hardlink-content"));
    std::filesystem::create_hard_link(unsafe.path / "original", unsafe.path / "application.log");
    Logger linked("hardlink", unsafe.path);
    check(linked.configure(settings) == "logging_file_unavailable");
    check(read(unsafe.path / "original") == "synthetic-hardlink-content");
    Directory blocked("blocked");
    {
        Logger logger("blocked", blocked.path);
        check(logger.configure(settings).empty());
        check(privateWrite(blocked.path / "application.log.1", "foreign-rotation"));
        for (unsigned i = 0; i < 48; ++i)
        {
            logger.info("Rotation message " + std::string(256, 'x'));
        }
        check(logger.flush() == "logging_write_failed");
        check(logger.error() == "logging_write_failed");
        check(read(blocked.path / "application.log.1") == "foreign-rotation");
        check(std::filesystem::file_size(blocked.path / "application.log") <= 4096);
    }
}
} // namespace

int main()
{
    try
    {
        valuesAndFormatting();
        contextAndObservers();
        hierarchy();
        historyAndSettings();
        configuredTags();
        concurrency();
        asynchronousFiles();
        queueAccounting();
        files();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
