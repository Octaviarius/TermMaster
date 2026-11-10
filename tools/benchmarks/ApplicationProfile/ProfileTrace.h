#pragma once

#include <chrono>
#include <cstdio>
#include <map>
#include <string>

/** Temporary, single-GUI-thread inclusive/exclusive instrumentation for the profiling tool. */
namespace ApplicationProfileTrace
{
struct Record
{
    unsigned long long calls       = 0;
    double             inclusiveMs = 0;
    double             exclusiveMs = 0;
};
struct Store
{
    std::map<std::string, Record> records;
    void                          dump() const
    {
        for (const auto& [name, value] : records)
        {
            fprintf(stderr,
                    "SCOPEJSON {\"stage\":\"%s\",\"calls\":%llu,\"inclusiveMs\":%.6f,\"exclusiveMs\":%.6f}\n",
                    name.c_str(),
                    value.calls,
                    value.inclusiveMs,
                    value.exclusiveMs);
        }
        fflush(stderr);
    }
    ~Store()
    {
        dump();
    }
};
inline Store& store()
{
    static Store data;
    return data;
}
class Scope
{
public:
    explicit Scope(const char* name) : _record(store().records[name]), _parent(_current), _start(Clock::now())
    {
        _current = this;
    }
    ~Scope()
    {
        const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - _start).count();
        ++_record.calls;
        _record.inclusiveMs += elapsed;
        _record.exclusiveMs += elapsed - _children;
        _current             = _parent;
        if (_parent)
        {
            _parent->_children += elapsed;
        }
    }

private:
    using Clock                                = std::chrono::steady_clock;
    inline static thread_local Scope* _current = nullptr;
    Record&                           _record;
    Scope*                            _parent;
    Clock::time_point                 _start;
    double                            _children = 0;
};
} // namespace ApplicationProfileTrace
