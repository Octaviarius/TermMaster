#include "LogFile.h"
#include "LogTimestamp.h"

#include <Termaster/Core/Logging/Logger.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace
{
constexpr std::array<std::string_view, 6> levels {"trace", "debug", "info", "warning", "error", "critical"};
constexpr std::size_t                     maximumParameters = 2048;

void append(std::string& output, std::string_view text, bool& full);

std::size_t utf8Width(std::string_view value, std::size_t position);

void quoted(std::string& output, std::string_view text, bool& full);

std::string singleLine(std::string_view value, std::size_t maximum);

} // namespace

struct Logger::Subscription::_State
{
    std::shared_ptr<std::recursive_mutex> mutex;
    std::function<void(const Record&)>    receive;
};

Logger::Subscription::Subscription(std::shared_ptr<_State> state) : _state(std::move(state))
{
}

Logger::Subscription::~Subscription()
{
    reset();
}

Logger::Subscription::Subscription(Subscription&& other) noexcept : _state(std::move(other._state))
{
}

Logger::Subscription& Logger::Subscription::operator=(Subscription&& other) noexcept
{
    if (this != &other)
    {
        reset();
        _state = std::move(other._state);
    }
    return *this;
}

void Logger::Subscription::reset()
{
    if (auto state = std::exchange(_state, {}))
    {
        std::lock_guard lock(*state->mutex);
        state->receive = {};
    }
}

class Logger::_Impl
{
public:
    struct Output
    {
        std::string                                      bytes;
        bool                                             console {};
        std::shared_ptr<TermasterLoggingDetail::LogFile> _file;
        std::size_t                                      maximumFileBytes {};
        std::size_t                                      retainedFiles {};
    };

    std::mutex              _configurationMutex;
    std::condition_variable _changed;
    std::thread             _worker;
    std::deque<Output>      _queue;
    std::size_t             _pendingBytes {};
    std::size_t             _pendingCount {};
    std::uint64_t           _accepted {};
    std::uint64_t           _completed {};
    std::uint64_t           _outputDropped {};
    bool                    _stopping {};

    mutable std::mutex                               _mutex;
    std::filesystem::path                            _directory;
    Settings                                         _settings;
    std::string                                      _error;
    std::deque<Record>                               _history;
    std::uint64_t                                    _sequence {};
    std::uint64_t                                    _dropped {};
    std::map<std::string, Level>                     _overrides;
    std::shared_ptr<TermasterLoggingDetail::LogFile> _file;

    std::set<std::string>                            _tags;
    std::vector<std::weak_ptr<Subscription::_State>> _observers;
    std::shared_ptr<std::recursive_mutex>            _observerMutex {std::make_shared<std::recursive_mutex>()};

    explicit _Impl(std::filesystem::path value) : _directory(std::move(value))
    {
    }

    ~_Impl()
    {
        {
            std::lock_guard lock(_mutex);
            _stopping = true;
        }
        _changed.notify_all();
        if (_worker.joinable())
        {
            _worker.join();
        }
    }

    void _run()
    {
        std::unique_lock lock(_mutex);
        for (;;)
        {
            _changed.wait(lock, [&] { return _stopping || !_queue.empty(); });
            if (_queue.empty())
            {
                return;
            }

            auto output = std::move(_queue.front());
            _queue.pop_front();
            auto        bytes = std::move(output.bytes);
            std::size_t count {1};
            const auto  maximum = std::min<std::size_t>(64 * 1024, output.maximumFileBytes);
            try
            {
                while (!_queue.empty() && bytes.size() + _queue.front().bytes.size() <= maximum
                       && output.console == _queue.front().console && output._file == _queue.front()._file
                       && output.maximumFileBytes == _queue.front().maximumFileBytes
                       && output.retainedFiles == _queue.front().retainedFiles)
                {
                    bytes += _queue.front().bytes;
                    _queue.pop_front();
                    ++count;
                }
            }
            catch (...)
            {
                /** The first complete record remains deliverable if batch allocation fails. */
            }
            lock.unlock();

            bool failed {};
            try
            {
                if (output.console)
                {
                    failed = std::fwrite(bytes.data(), 1, bytes.size(), stderr) != bytes.size() || std::fflush(stderr);
                }
                if (output._file)
                {
                    failed = !output._file->append(bytes, output.maximumFileBytes, output.retainedFiles) || failed;
                }
            }
            catch (...)
            {
                failed = true;
            }

            output._file.reset();
            lock.lock();
            _pendingBytes -= bytes.size();
            _pendingCount -= count;
            _completed    += count;
            if (failed && _error != "logging_queue_full")
            {
                _error = "logging_write_failed";
            }
            _changed.notify_all();
        }
    }

    void _registerTag(std::string tag)
    {
        std::lock_guard lock(_mutex);
        for (;;)
        {
            _tags.insert(tag);
            const auto separator = tag.rfind(':');
            if (separator == std::string::npos)
            {
                break;
            }
            tag.resize(separator);
        }
    }

    Level _level(std::string tag) const
    {
        for (;;)
        {
            if (const auto found = _overrides.find(tag); found != _overrides.end())
            {
                return found->second;
            }
            if (const auto found = _settings.tagLevels.find(tag); found != _settings.tagLevels.end())
            {
                return found->second;
            }
            const auto separator = tag.rfind(':');
            if (separator == std::string::npos)
            {
                return _settings.minimumLevel;
            }
            tag.resize(separator);
        }
    }
};

std::string Logger::Value::format() const
{
    std::string output;
    std::size_t nodes {};
    bool        full {};
    _append(output, nodes, 0, full);
    return full ? "\"[truncated]\"" : output;
}

void Logger::Value::_append(std::string& output, std::size_t& nodes, std::size_t depth, bool& full) const
{
    if (++nodes > 256 || depth > 8)
    {
        full = true;
        return;
    }
    std::visit(
        [&](const auto& data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>)
            {
                append(output, "null", full);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                append(output, data ? "true" : "false", full);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                quoted(output, data, full);
            }
            else if constexpr (std::is_same_v<T, Array>)
            {
                append(output, "[", full);
                bool first = true;
                for (const auto& child : data)
                {
                    if (full)
                    {
                        break;
                    }
                    if (!first)
                    {
                        append(output, ",", full);
                    }
                    first = false;
                    child._append(output, nodes, depth + 1, full);
                }
                append(output, "]", full);
            }
            else if constexpr (std::is_same_v<T, Object>)
            {
                append(output, "{", full);
                bool first = true;
                for (const auto& [key, child] : data)
                {
                    if (full)
                    {
                        break;
                    }
                    if (!first)
                    {
                        append(output, ",", full);
                    }
                    first = false;
                    quoted(output, key, full);
                    append(output, ":", full);
                    child._append(output, nodes, depth + 1, full);
                }
                append(output, "}", full);
            }
            else
            {
                if constexpr (std::is_same_v<T, double>)
                {
                    if (!std::isfinite(data))
                    {
                        quoted(output,
                               std::isnan(data)   ? "number:NaN" :
                               std::signbit(data) ? "number:-Infinity" :
                                                    "number:+Infinity",
                               full);
                        return;
                    }
                }
                std::array<char, 64> buffer {};
                const auto           result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), data);
                if (result.ec == std::errc {})
                {
                    append(output,
                           std::string_view(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())),
                           full);
                }
                else
                {
                    append(output, "null", full);
                }
            }
        },
        _data);
}

Logger::Logger(std::string tag, std::filesystem::path directory) :
    _tag(singleLine(tag, 256)), _impl(std::make_shared<_Impl>(std::move(directory)))
{
    _impl->_registerTag(_tag);
}

Logger::Logger(std::string tag, std::shared_ptr<_Impl> impl) : _tag(std::move(tag)), _impl(std::move(impl))
{
    _impl->_registerTag(_tag);
}

Logger Logger::child(std::string_view suffix) const
{
    auto logger     = Logger(singleLine(_tag + ":" + singleLine(suffix, 256), 256), _impl);
    logger._context = _context;
    return logger;
}

Logger Logger::withContext(Context context) const
{
    auto logger     = *this;
    logger._context = std::move(context);
    return logger;
}

Logger::Subscription Logger::subscribe(std::function<void(const Record&)> receive) const
{
    auto state     = std::make_shared<Subscription::_State>();
    state->receive = std::move(receive);
    state->mutex   = _impl->_observerMutex;
    std::lock_guard lock(_impl->_mutex);
    std::erase_if(_impl->_observers, [](const auto& observer) { return observer.expired(); });
    _impl->_observers.push_back(state);
    return Subscription(std::move(state));
}

std::string Logger::tag() const
{
    return _tag;
}

std::vector<std::string> Logger::tags() const
{
    std::lock_guard lock(_impl->_mutex);
    auto            names = _impl->_tags;
    for (const auto& [tag, level] : _impl->_settings.tagLevels)
    {
        names.insert(tag);
    }
    return {names.begin(), names.end()};
}

std::string Logger::configure(const Settings& settings)
{
    std::lock_guard  configurationLock(_impl->_configurationMutex);
    std::unique_lock lock(_impl->_mutex);
    if (levelName(settings.minimumLevel).empty() || settings.historyLimit < 16 || settings.historyLimit > 4096
        || settings.maximumFileBytes < 4096 || settings.maximumFileBytes > 16 * 1024 * 1024
        || settings.retainedFiles > 10)
    {
        return _impl->_error = "logging_invalid_settings";
    }
    if (settings.tagLevels.size() > 64)
    {
        return _impl->_error = "logging_invalid_settings";
    }
    for (const auto& [tag, level] : settings.tagLevels)
    {
        if (tag.empty() || tag.size() > 256 || tag.front() == ':' || tag.back() == ':'
            || tag.find("::") != std::string::npos || levelName(level).empty()
            || !std::all_of(tag.begin(), tag.end(), [](unsigned char c) {
                   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ':'
                       || c == '_' || c == '-' || c == '.';
               }))
        {
            return _impl->_error = "logging_invalid_settings";
        }
    }
    const auto target = _impl->_accepted;
    _impl->_changed.wait(lock, [&] { return _impl->_completed >= target; });

    /** Preparation may perform filesystem I/O; producers retain the previous policy. */
    lock.unlock();
    std::shared_ptr<TermasterLoggingDetail::LogFile> candidate;
    std::string                                      preparationError;
    if (settings.file && !_impl->_file)
    {
        try
        {
            candidate = std::make_shared<TermasterLoggingDetail::LogFile>();
            if (_impl->_directory.empty() || !candidate->open(_impl->_directory))
            {
                preparationError = "logging_file_unavailable";
            }
        }
        catch (...)
        {
            preparationError = "logging_file_unavailable";
        }
    }
    lock.lock();
    if (!preparationError.empty())
    {
        return _impl->_error = preparationError;
    }
    if ((settings.console || settings.file) && !_impl->_worker.joinable())
    {
        try
        {
            _impl->_worker = std::thread([impl = _impl.get()] { impl->_run(); });
        }
        catch (...)
        {
            return _impl->_error = "logging_write_failed";
        }
    }
    if (candidate)
    {
        _impl->_file = std::move(candidate);
    }
    std::shared_ptr<TermasterLoggingDetail::LogFile> retired;
    if (!settings.file)
    {
        retired = std::move(_impl->_file);
    }
    _impl->_settings = settings;
    while (_impl->_history.size() > settings.historyLimit)
    {
        _impl->_history.pop_front();
        ++_impl->_dropped;
    }
    if (!settings.gui)
    {
        _impl->_history.clear();
    }
    _impl->_error.clear();
    lock.unlock();
    return {};
}

Logger::Settings Logger::settings() const
{
    std::lock_guard lock(_impl->_mutex);
    return _impl->_settings;
}

std::string Logger::error() const
{
    std::lock_guard lock(_impl->_mutex);
    return _impl->_error;
}

std::string Logger::flush() const
{
    std::unique_lock lock(_impl->_mutex);
    const auto       target = _impl->_accepted;
    _impl->_changed.wait(lock, [&] { return _impl->_completed >= target; });
    return _impl->_error;
}

std::uint64_t Logger::outputDropped() const
{
    std::lock_guard lock(_impl->_mutex);
    return _impl->_outputDropped;
}

void Logger::setLevel(Level level)
{
    std::lock_guard lock(_impl->_mutex);
    if (levelName(level).empty())
    {
        _impl->_error = "logging_invalid_settings";
        return;
    }
    _impl->_overrides[_tag] = level;
}

void Logger::clearLevel()
{
    std::lock_guard lock(_impl->_mutex);
    _impl->_overrides.erase(_tag);
}

std::optional<Logger::Record> Logger::write(Level             level,
                                            std::string_view  message,
                                            const Parameters& parameters) noexcept
{
    try
    {
        std::unique_lock lock(_impl->_mutex);
        if (levelName(level).empty())
        {
            _impl->_error = "logging_invalid_settings";
            return {};
        }
        if (level < _impl->_level(_tag))
        {
            return {};
        }
        Record record;
        record.sequence    = ++_impl->_sequence;
        record.timestampMs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
        record.timestamp = TermasterLoggingDetail::localLogTimestamp(record.timestampMs);
        record.profileId = _context.profileId;
        record.sessionId = _context.sessionId;
        record.attempt   = _context.attempt;
        record.level     = level;
        record.tag       = _tag;
        record.message   = singleLine(message, 1024);
        std::size_t nodes {};
        bool        full {};
        bool        first = true;
        for (const auto& [key, value] : parameters)
        {
            if (full)
            {
                break;
            }
            if (!first)
            {
                append(record.parameters, ",", full);
            }
            first             = false;
            const bool simple = !key.empty() && key.size() <= maximumParameters
                             && std::all_of(key.begin(), key.end(), [](unsigned char c) {
                                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                                        || c == '_' || c == '-' || c == '.';
                                });
            if (simple)
            {
                append(record.parameters, key, full);
            }
            else
            {
                quoted(record.parameters, key, full);
            }
            append(record.parameters, "=", full);
            value._append(record.parameters, nodes, 0, full);
        }
        if (full)
        {
            record.parameters = "truncated=true";
        }
        if (_impl->_settings.gui)
        {
            _impl->_history.push_back(record);
            if (_impl->_history.size() > _impl->_settings.historyLimit)
            {
                _impl->_history.pop_front();
                ++_impl->_dropped;
            }
        }
        if (_impl->_settings.console || _impl->_settings.file)
        {
            try
            {
                auto                  bytes               = format(record) + '\n';
                constexpr std::size_t maximumQueueRecords = 1024;
                constexpr std::size_t maximumQueueBytes   = 4 * 1024 * 1024;
                if (_impl->_pendingCount >= maximumQueueRecords
                    || bytes.size() > maximumQueueBytes - _impl->_pendingBytes)
                {
                    ++_impl->_outputDropped;
                    _impl->_error = "logging_queue_full";
                }
                else
                {
                    const auto size = bytes.size();
                    _impl->_queue.push_back({std::move(bytes),
                                             _impl->_settings.console,
                                             _impl->_settings.file ? _impl->_file : nullptr,
                                             _impl->_settings.maximumFileBytes,
                                             _impl->_settings.retainedFiles});
                    _impl->_pendingBytes += size;
                    ++_impl->_pendingCount;
                    ++_impl->_accepted;
                    _impl->_changed.notify_one();
                }
            }
            catch (...)
            {
                ++_impl->_outputDropped;
                _impl->_error = "logging_write_failed";
            }
        }
        std::vector<std::shared_ptr<Subscription::_State>> observers;
        if (_impl->_settings.gui)
        {
            for (const auto& observer : _impl->_observers)
            {
                if (auto state = observer.lock())
                {
                    observers.push_back(std::move(state));
                }
            }
        }
        lock.unlock();
        std::lock_guard observerLock(*_impl->_observerMutex);
        for (const auto& observer : observers)
        {
            if (observer->receive)
            {
                /** Retain the callable if it cancels its own subscription. */
                auto receive = observer->receive;
                try
                {
                    receive(record);
                }
                catch (...)
                {
                    /** Observer failures must not fail other outputs. */
                }
            }
        }
        return record;
    }
    catch (...)
    {
        try
        {
            std::lock_guard lock(_impl->_mutex);
            _impl->_error = "logging_write_failed";
        }
        catch (...)
        {
        }
        return {};
    }
}

std::optional<Logger::Record> Logger::trace(std::string_view message, const Parameters& parameters) noexcept
{
    return write(Level::trace, message, parameters);
}

std::optional<Logger::Record> Logger::debug(std::string_view message, const Parameters& parameters) noexcept
{
    return write(Level::debug, message, parameters);
}

std::optional<Logger::Record> Logger::info(std::string_view message, const Parameters& parameters) noexcept
{
    return write(Level::info, message, parameters);
}

std::optional<Logger::Record> Logger::warning(std::string_view message, const Parameters& parameters) noexcept
{
    return write(Level::warning, message, parameters);
}

std::optional<Logger::Record> Logger::error(std::string_view message, const Parameters& parameters) noexcept
{
    return write(Level::error, message, parameters);
}

std::optional<Logger::Record> Logger::critical(std::string_view message, const Parameters& parameters) noexcept
{
    return write(Level::critical, message, parameters);
}

std::vector<Logger::Record> Logger::history(std::uint64_t after, std::size_t limit) const
{
    std::lock_guard     lock(_impl->_mutex);
    std::vector<Record> records;
    const auto          bounded = std::min(limit, _impl->_settings.historyLimit);
    for (const auto& record : _impl->_history)
    {
        if (records.size() == bounded)
        {
            break;
        }
        if (record.sequence > after)
        {
            records.push_back(record);
        }
    }
    return records;
}

std::uint64_t Logger::dropped() const
{
    std::lock_guard lock(_impl->_mutex);
    return _impl->_dropped;
}

std::string Logger::format(const Record& record)
{
    std::string level(levelName(record.level));
    for (auto& c : level)
    {
        if (c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    auto output = singleLine(record.timestamp, 64) + " " + level + " " + singleLine(record.tag, 256) + " "
                + singleLine(record.message, 1024);
    if (!record.parameters.empty())
    {
        output += " " + singleLine(record.parameters, maximumParameters);
    }
    return output;
}

std::string_view Logger::levelName(Level level)
{
    const auto index = static_cast<std::size_t>(level);
    return index < levels.size() ? levels[index] : std::string_view {};
}

std::optional<Logger::Level> Logger::parseLevel(std::string_view name)
{
    for (std::size_t i = 0; i < levels.size(); ++i)
    {
        if (levels[i] == name)
        {
            return static_cast<Level>(i);
        }
    }
    return {};
}

namespace
{
void append(std::string& output, std::string_view text, bool& full)
{
    if (full)
    {
        return;
    }
    if (output.size() + text.size() > maximumParameters)
    {
        full = true;
        return;
    }
    output += text;
}

std::size_t utf8Width(std::string_view value, std::size_t position)
{
    const auto c = static_cast<unsigned char>(value[position]);
    if (c < 0x80)
    {
        return 1;
    }
    const std::size_t width = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!width || position + width > value.size())
    {
        return 0;
    }
    for (std::size_t offset = 1; offset < width; ++offset)
    {
        const auto next = static_cast<unsigned char>(value[position + offset]);
        if (next < 0x80 || next > 0xbf)
        {
            return 0;
        }
    }
    const auto next = static_cast<unsigned char>(value[position + 1]);
    if ((c == 0xe0 && next < 0xa0) || (c == 0xed && next > 0x9f) || (c == 0xf0 && next < 0x90)
        || (c == 0xf4 && next > 0x8f))
    {
        return 0;
    }
    return width;
}

void quoted(std::string& output, std::string_view text, bool& full)
{
    append(output, "\"", full);
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t position = 0; position < text.size() && !full;)
    {
        const auto c          = static_cast<unsigned char>(text[position]);
        const auto validWidth = utf8Width(text, position);
        if (c == '"')
        {
            append(output, "\\\"", full);
        }
        else if (c == '\\')
        {
            append(output, "\\\\", full);
        }
        else if (c < 32 || c == 127 || !validWidth)
        {
            const char escaped[] {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            append(output, std::string_view(escaped, sizeof(escaped)), full);
        }
        else
        {
            append(output, text.substr(position, validWidth), full);
        }
        position += validWidth ? validWidth : 1;
    }
    append(output, "\"", full);
}

std::string singleLine(std::string_view value, std::size_t maximum)
{
    std::string    output;
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t position = 0; position < value.size();)
    {
        const auto        c          = static_cast<unsigned char>(value[position]);
        const auto        validWidth = utf8Width(value, position);
        const bool        control    = c < 32 || c == 127 || !validWidth;
        const std::size_t width      = validWidth ? validWidth : 1;
        const std::size_t count      = control ? 6 : width;
        if (position + width > value.size() || output.size() + count > maximum)
        {
            output += "[truncated]";
            break;
        }
        if (control)
        {
            const char escaped[] {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            output.append(escaped, sizeof(escaped));
        }
        else
        {
            output.append(value.substr(position, width));
        }
        position += width;
    }
    return output;
}
} // namespace
