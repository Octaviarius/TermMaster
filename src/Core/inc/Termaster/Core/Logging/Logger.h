#pragma once

#include <QtGlobal>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

/** Named, thread-safe logger. Copies and children share outputs and bounded history.
 * Callers provide public, secret-free messages and parameters.
 */
class Logger
{
public:
    enum class Level
    {
        trace,
        debug,
        info,
        warning,
        error,
        critical
    };

    class Value
    {
    public:
        using Array  = std::vector<Value>;
        using Object = std::vector<std::pair<std::string, Value>>;

        Value() : _data(nullptr)
        {
        }

        Value(std::nullptr_t) : _data(nullptr)
        {
        }

        Value(bool value) : _data(value)
        {
        }

        template <std::signed_integral T>
            requires(!std::same_as<T, bool>)
        Value(T value) : _data(static_cast<std::int64_t>(value))
        {
        }

        template <std::unsigned_integral T>
            requires(!std::same_as<T, bool>)
        Value(T value) : _data(static_cast<std::uint64_t>(value))
        {
        }

        Value(double value) : _data(value)
        {
        }

        Value(const char* value) : _data(std::string(value ? value : ""))
        {
        }

        Value(std::string value) : _data(std::move(value))
        {
        }

        Value(std::string_view value) : _data(std::string(value))
        {
        }

        Value(Array value) : _data(std::move(value))
        {
        }

        Value(Object value) : _data(std::move(value))
        {
        }

        Value(std::initializer_list<Value> values) : _data(Array(values))
        {
        }

        template <class T>
            requires(!std::same_as<T, Value>)
        Value(const std::vector<T>& values) : _data(Array {})
        {
            auto& array = std::get<Array>(_data);
            array.reserve(values.size());
            for (const auto& value : values)
            {
                array.emplace_back(value);
            }
        }

        /** Compact typed text. Limits produce "[truncated]"; non-finite numbers use
         * quoted number:NaN, number:+Infinity and number:-Infinity markers.
         */
        std::string format() const;

    private:
        using Storage = std::
            variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double, std::string, Array, Object>;

        friend class Logger;
        Storage _data;

        void _append(std::string& output, std::size_t& nodes, std::size_t depth, bool& full) const;
    };

    using Parameters = std::vector<std::pair<std::string, Value>>;

    struct Context
    {
        std::optional<std::string> profileId;
        std::optional<std::string> sessionId;
        std::uint64_t              attempt {};
    };

    struct Record
    {
        std::uint64_t              sequence {};
        std::uint64_t              timestampMs {};
        std::string                timestamp;
        Level                      level {Level::info};
        std::string                tag;
        std::string                message;
        std::string                parameters;
        std::optional<std::string> profileId;
        std::optional<std::string> sessionId;
        std::uint64_t              attempt {};
    };

    /** A family observer. Destruction waits for an executing callback; callbacks may
     * log or cancel their own subscription. Callback arguments are borrowed for the call.
     */
    class Subscription
    {
    public:
        Subscription() = default;
        ~Subscription();
        Subscription(Subscription&&) noexcept;
        Subscription& operator=(Subscription&&) noexcept;
        Q_DISABLE_COPY(Subscription)
        void reset();

    private:
        friend class Logger;
        struct _State;
        std::shared_ptr<_State> _state;
        explicit Subscription(std::shared_ptr<_State> state);
    };

    struct Settings
    {
        Level minimumLevel {Level::info};
        /** Persisted family-wide rules; most-specific colon-delimited tag wins. */
        std::map<std::string, Level> tagLevels;
        bool                         console {};
        bool                         file {};
        bool                         gui {true};
        std::size_t                  historyLimit {256};
        std::size_t                  maximumFileBytes {2 * 1024 * 1024};
        std::size_t                  retainedFiles {3};
    };

    explicit Logger(std::string tag = "root", std::filesystem::path directory = {});
    Logger child(std::string_view suffix) const;
    Logger withContext(Context context) const;
    /** Observes accepted GUI records from this logger family; no history replay. */
    Subscription subscribe(std::function<void(const Record&)> receive) const;
    std::string  tag() const;
    /** Sorted family tag names, including parent tags and configured rules; independent of filtering. */
    std::vector<std::string> tags() const;
    std::string              configure(const Settings& settings);
    Settings                 settings() const;
    std::string              error() const;
    /** Wait for sink attempts for records accepted before this call; return the family sink error.
     * A returned write record and GUI history describe acceptance, not durable persistence.
     */
    std::string flush() const;
    /** Records refused by the bounded output queue; independent of history eviction. */
    std::uint64_t           outputDropped() const;
    void                    setLevel(Level level);
    void                    clearLevel();
    std::optional<Record>   write(Level level, std::string_view message, const Parameters& parameters = {}) noexcept;
    std::optional<Record>   trace(std::string_view message, const Parameters& parameters = {}) noexcept;
    std::optional<Record>   debug(std::string_view message, const Parameters& parameters = {}) noexcept;
    std::optional<Record>   info(std::string_view message, const Parameters& parameters = {}) noexcept;
    std::optional<Record>   warning(std::string_view message, const Parameters& parameters = {}) noexcept;
    std::optional<Record>   error(std::string_view message, const Parameters& parameters = {}) noexcept;
    std::optional<Record>   critical(std::string_view message, const Parameters& parameters = {}) noexcept;
    std::vector<Record>     history(std::uint64_t after = 0, std::size_t limit = 256) const;
    std::uint64_t           dropped() const;
    static std::string      format(const Record& record);
    static std::string_view levelName(Level level);
    static std::optional<Level> parseLevel(std::string_view name);

private:
    class _Impl;

    Context                _context;
    std::string            _tag;
    std::shared_ptr<_Impl> _impl;

    Logger(std::string tag, std::shared_ptr<_Impl> impl);
};
