#pragma once
#include <QtGlobal>
#include <filesystem>
#include <memory>
#include <string_view>

namespace TermasterLoggingDetail
{
/** Private append-only file; short-lived write/rotation handles permit concurrent readers. */
class LogFile
{
public:
    LogFile();
    ~LogFile();
    Q_DISABLE_COPY(LogFile)
    bool open(const std::filesystem::path& directory);
    bool append(std::string_view bytes, std::size_t maximumBytes, std::size_t retainedFiles);

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};
} // namespace TermasterLoggingDetail
