#include <QtGlobal>
#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
// clang-format on
#include "LogFile.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace TermasterLoggingDetail
{
namespace
{
    struct Handle
    {
        HANDLE value {INVALID_HANDLE_VALUE};

        ~Handle()
        {
            close();
        }

        void close()
        {
            if (value != INVALID_HANDLE_VALUE)
            {
                CloseHandle(value);
                value = INVALID_HANDLE_VALUE;
            }
        }
    };

    bool owned(HANDLE file, bool directory = false)
    {
        BY_HANDLE_FILE_INFORMATION info {};
        if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            || bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory
            || (!directory && info.nNumberOfLinks != 1))
        {
            return false;
        }
        PSID                 owner {};
        PSECURITY_DESCRIPTOR descriptor {};
        PACL                 dacl {};
        if (GetSecurityInfo(file,
                            SE_FILE_OBJECT,
                            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                            &owner,
                            nullptr,
                            &dacl,
                            nullptr,
                            &descriptor)
            != ERROR_SUCCESS)
        {
            return false;
        }
        Handle token;
        bool   result = false;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value))
        {
            DWORD size {};
            GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
            std::vector<unsigned char> buffer(size);
            result = size && GetTokenInformation(token.value, TokenUser, buffer.data(), size, &size)
                  && EqualSid(owner, reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid);
        }
        if (result && !directory)
        {
            SECURITY_DESCRIPTOR_CONTROL control {};
            DWORD                       revision {};
            result = dacl && GetSecurityDescriptorControl(descriptor, &control, &revision)
                  && (control & SE_DACL_PROTECTED);
            for (DWORD i = 0; result && i < dacl->AceCount; ++i)
            {
                void* raw {};
                result = GetAce(dacl, i, &raw) && static_cast<ACE_HEADER*>(raw)->AceType == ACCESS_ALLOWED_ACE_TYPE;
                if (result)
                {
                    auto* sid = &static_cast<ACCESS_ALLOWED_ACE*>(raw)->SidStart;
                    result    = EqualSid(owner, sid) || IsWellKnownSid(sid, WinLocalSystemSid)
                             || IsWellKnownSid(sid, WinBuiltinAdministratorsSid);
                }
            }
        }
        LocalFree(descriptor);
        return result;
    }

    bool openExisting(Handle& handle, const std::filesystem::path& path, bool& missing, bool rotation = false)
    {
        handle.value = CreateFileW(path.c_str(),
                                   FILE_READ_DATA | FILE_APPEND_DATA | READ_CONTROL | (rotation ? DELETE : 0),
                                   FILE_SHARE_READ | FILE_SHARE_DELETE,
                                   nullptr,
                                   OPEN_EXISTING,
                                   FILE_FLAG_OPEN_REPARSE_POINT,
                                   nullptr);
        missing      = handle.value == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND;
        if (handle.value == INVALID_HANDLE_VALUE)
        {
            return missing;
        }
        if (!owned(handle.value))
        {
            return false;
        }
        std::array<char, 96> bytes {};
        DWORD                count {};
        if (!ReadFile(handle.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr))
        {
            return false;
        }
        if (!count)
        {
            return true;
        }
        constexpr std::string_view pattern = "0000-00-00T00:00:00.000+00:00 ";
        if (count < pattern.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < pattern.size(); ++i)
        {
            if (pattern[i] == '0' ? bytes[i] < '0' || bytes[i] > '9' :
                i == 23           ? bytes[i] != '+' && bytes[i] != '-' :
                                    bytes[i] != pattern[i])
            {
                return false;
            }
        }
        const std::string_view tail(bytes.data() + pattern.size(), count - pattern.size());
        return tail.starts_with("TRACE ") || tail.starts_with("DEBUG ") || tail.starts_with("INFO ")
            || tail.starts_with("WARNING ") || tail.starts_with("ERROR ") || tail.starts_with("CRITICAL ");
    }

    bool erase(Handle& handle)
    {
        FILE_DISPOSITION_INFO disposition {TRUE};
        if (!SetFileInformationByHandle(handle.value, FileDispositionInfo, &disposition, sizeof(disposition)))
        {
            return false;
        }
        handle.close();
        return true;
    }

    bool rename(Handle& handle, const std::filesystem::path& destination)
    {
        const auto                 name = destination.wstring();
        std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t));
        auto*                      info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        info->ReplaceIfExists           = FALSE;
        info->RootDirectory             = nullptr;
        info->FileNameLength            = static_cast<DWORD>(name.size() * sizeof(wchar_t));
        std::memcpy(info->FileName, name.data(), info->FileNameLength);
        return SetFileInformationByHandle(handle.value, FileRenameInfo, info, static_cast<DWORD>(buffer.size()));
    }
} // namespace

class LogFile::Impl
{
public:
    std::filesystem::path _directory;
    Handle                _directoryHandle;

    std::filesystem::path _path(std::size_t index) const
    {
        return _directory / (index ? L"application.log." + std::to_wstring(index) : L"application.log");
    }

    bool _create()
    {
        Handle token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value))
        {
            return false;
        }
        DWORD size {};
        GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
        std::vector<unsigned char> buffer(size);
        if (!size || !GetTokenInformation(token.value, TokenUser, buffer.data(), size, &size))
        {
            return false;
        }
        LPWSTR sid {};
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid))
        {
            return false;
        }
        const std::wstring sddl = L"O:" + std::wstring(sid) + L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;"
                                + std::wstring(sid) + L")";
        LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor {};
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        {
            return false;
        }
        SECURITY_ATTRIBUTES attributes {sizeof(attributes), descriptor, FALSE};
        Handle              active;
        active.value = CreateFileW(_path(0).c_str(),
                                   FILE_APPEND_DATA | READ_CONTROL,
                                   FILE_SHARE_READ | FILE_SHARE_DELETE,
                                   &attributes,
                                   CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                   nullptr);
        LocalFree(descriptor);
        if (active.value == INVALID_HANDLE_VALUE || !owned(active.value))
        {
            return false;
        }
        return true;
    }

    bool _inspect(std::array<Handle, 10>& previous)
    {
        for (std::size_t i = 0; i < previous.size(); ++i)
        {
            bool missing {};
            if (!openExisting(previous[i], _path(i + 1), missing, true))
            {
                return false;
            }
        }
        return true;
    }
};

LogFile::LogFile() : _impl(std::make_unique<Impl>())
{
}

LogFile::~LogFile() = default;

bool LogFile::open(const std::filesystem::path& directory)
{
    _impl->_directory = std::filesystem::absolute(directory).lexically_normal();
    for (auto component = _impl->_directory; !component.empty(); component = component.parent_path())
    {
        const auto attributes = GetFileAttributesW(component.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return false;
        }
        if (component == component.parent_path())
        {
            break;
        }
    }
    _impl->_directoryHandle.value = CreateFileW(_impl->_directory.c_str(),
                                                READ_CONTROL,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                nullptr,
                                                OPEN_EXISTING,
                                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                                nullptr);
    if (_impl->_directoryHandle.value == INVALID_HANDLE_VALUE || !owned(_impl->_directoryHandle.value, true))
    {
        return false;
    }
    std::array<Handle, 10> previous;
    if (!_impl->_inspect(previous))
    {
        return false;
    }
    bool   missing {};
    Handle active;
    if (!openExisting(active, _impl->_path(0), missing))
    {
        return false;
    }
    return !missing || _impl->_create();
}

bool LogFile::append(std::string_view bytes, std::size_t maximumBytes, std::size_t retainedFiles)
{
    Handle active;
    bool   missing {};
    if (!openExisting(active, _impl->_path(0), missing) || missing)
    {
        return false;
    }
    LARGE_INTEGER size {};
    if (bytes.size() > maximumBytes || !GetFileSizeEx(active.value, &size) || size.QuadPart < 0)
    {
        return false;
    }
    if (static_cast<std::uint64_t>(size.QuadPart) + bytes.size() > maximumBytes)
    {
        active.close();
        if (!openExisting(active, _impl->_path(0), missing, true) || missing)
        {
            return false;
        }
        std::array<Handle, 10> previous;
        if (!_impl->_inspect(previous))
        {
            return false;
        }
        for (std::size_t i = previous.size(); i > 0; --i)
        {
            auto& file = previous[i - 1];
            if (file.value == INVALID_HANDLE_VALUE)
            {
                continue;
            }
            if (i >= retainedFiles)
            {
                if (!erase(file))
                {
                    return false;
                }
            }
            else if (!rename(file, _impl->_path(i + 1)))
            {
                return false;
            }
        }
        if (retainedFiles)
        {
            if (!rename(active, _impl->_path(1)))
            {
                return false;
            }
            active.close();
        }
        else if (!erase(active))
        {
            return false;
        }
        if (!_impl->_create() || !openExisting(active, _impl->_path(0), missing) || missing)
        {
            return false;
        }
    }
    DWORD count {};
    return WriteFile(active.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)
        && count == bytes.size() && FlushFileBuffers(active.value);
}
} // namespace TermasterLoggingDetail

#else
#include "LogFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <array>

namespace TermasterLoggingDetail
{
namespace
{
    QString fileName(const std::filesystem::path& path)
    {
        return QString::fromStdString(path.string());
    }

    bool inspect(const QString& path)
    {
        const QFileInfo info(path);
        if (!info.exists())
        {
            return !info.isSymLink();
        }
        if (info.isSymLink() || !info.isFile()
            || (info.permissions()
                & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther
                   | QFileDevice::WriteOther | QFileDevice::ExeOther)))
        {
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            return false;
        }
        const auto bytes = file.read(96);
        if (bytes.isEmpty())
        {
            return true;
        }
        constexpr std::string_view pattern = "0000-00-00T00:00:00.000+00:00 ";
        if (bytes.size() < static_cast<qsizetype>(pattern.size()))
        {
            return false;
        }
        for (std::size_t i = 0; i < pattern.size(); ++i)
        {
            const auto c = bytes[static_cast<qsizetype>(i)];
            if (pattern[i] == '0' ? c < '0' || c > '9' : i == 23 ? c != '+' && c != '-' : c != pattern[i])
            {
                return false;
            }
        }
        const auto tail = bytes.mid(static_cast<qsizetype>(pattern.size()));
        return tail.startsWith("TRACE ") || tail.startsWith("DEBUG ") || tail.startsWith("INFO ")
            || tail.startsWith("WARNING ") || tail.startsWith("ERROR ") || tail.startsWith("CRITICAL ");
    }
} // namespace

class LogFile::Impl
{
public:
    QString _directory;

    QString _path(std::size_t index) const
    {
        return QDir(_directory)
            .filePath(index ? QStringLiteral("application.log.%1").arg(index) : QStringLiteral("application.log"));
    }

    bool _create()
    {
        QFile file(_path(0));
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        {
            return false;
        }
        return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
};

LogFile::LogFile() : _impl(std::make_unique<Impl>())
{
}
LogFile::~LogFile() = default;

bool LogFile::open(const std::filesystem::path& directory)
{
    _impl->_directory = fileName(std::filesystem::absolute(directory).lexically_normal());
    for (auto component = QFileInfo(_impl->_directory);;)
    {
        if (!component.exists() || component.isSymLink() || !component.isDir())
        {
            return false;
        }
        const auto parent = QFileInfo(component.absolutePath());
        if (parent.absoluteFilePath() == component.absoluteFilePath())
        {
            break;
        }
        component = parent;
    }
    for (std::size_t i = 0; i <= 10; ++i)
    {
        if (!inspect(_impl->_path(i)))
        {
            return false;
        }
    }
    return QFileInfo::exists(_impl->_path(0)) || _impl->_create();
}

bool LogFile::append(std::string_view bytes, std::size_t maximumBytes, std::size_t retainedFiles)
{
    if (bytes.size() > maximumBytes || !inspect(_impl->_path(0)) || !QFileInfo::exists(_impl->_path(0)))
    {
        return false;
    }
    if (static_cast<quint64>(QFileInfo(_impl->_path(0)).size()) + bytes.size() > maximumBytes)
    {
        for (std::size_t i = 1; i <= 10; ++i)
        {
            if (!inspect(_impl->_path(i)))
            {
                return false;
            }
        }
        for (std::size_t i = 10; i > 0; --i)
        {
            const auto path = _impl->_path(i);
            if (QFileInfo::exists(path)
                && !(i >= retainedFiles ? QFile::remove(path) : QFile::rename(path, _impl->_path(i + 1))))
            {
                return false;
            }
        }
        if (!(retainedFiles ? QFile::rename(_impl->_path(0), _impl->_path(1)) : QFile::remove(_impl->_path(0)))
            || !_impl->_create())
        {
            return false;
        }
    }
    QFile file(_impl->_path(0));
    return file.open(QIODevice::WriteOnly | QIODevice::Append)
        && file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size())
        && file.flush();
}
} // namespace TermasterLoggingDetail

#endif
