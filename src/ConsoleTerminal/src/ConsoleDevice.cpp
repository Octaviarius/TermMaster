#include "ConsoleDevice.h"

#include "ConsoleEnvironment.h"

#include <QLoggingCategory>
#if defined(Q_OS_WIN) && !defined(TERMASTER_CONSOLE_HOST)
#include "Elevation/ElevatedConsoleDevice.h"
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace
{
Q_LOGGING_CATEGORY(consoleLog, "termaster.transport.console")
Q_LOGGING_CATEGORY(consoleLogTrace, "termaster.transport.console.trace", QtInfoMsg)

constexpr qsizetype QueueLimit = 1024 * 1024;
constexpr qsizetype ChunkSize  = 16384;

bool reject(QString* error, const QString& message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

QString resolveProgram(const QString& program, const QProcessEnvironment& environment)
{
    if (program.contains('/') || program.contains('\\'))
    {
        return QFileInfo(program).absoluteFilePath();
    }
    QStringList suffixes {QString()};
#ifdef Q_OS_WIN
    if (QFileInfo(program).suffix().isEmpty())
    {
        suffixes = {".exe", ".com", ".cmd", ".bat", ".ps1"};
    }
#endif
    for (const auto& path : environment.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts))
    {
        for (const auto& suffix : suffixes)
        {
            const QFileInfo file(QDir(path).filePath(program + suffix));
            if (file.isFile() && file.isExecutable())
            {
                return file.absoluteFilePath();
            }
        }
    }
    return {};
}

QString defaultProgram(const QProcessEnvironment& environment)
{
#ifdef Q_OS_WIN
    auto program = resolveProgram("pwsh", environment);
    if (!program.isEmpty())
    {
        return program;
    }
    program = QDir(environment.value("ProgramFiles")).filePath("PowerShell/7/pwsh.exe");
    if (QFileInfo::exists(program))
    {
        return program;
    }
    const auto windows = environment.value("SystemRoot", "C:/Windows");
    program            = QDir(windows).filePath("System32/WindowsPowerShell/v1.0/powershell.exe");
    if (QFileInfo::exists(program))
    {
        return program;
    }
    return QDir(windows).filePath("System32/cmd.exe");
#else
    const auto shell = environment.value("SHELL");
    return shell.isEmpty() ? QStringLiteral("/bin/sh") : shell;
#endif
}

#ifdef Q_OS_WIN
QString nativeError(const QString& operation, DWORD code = GetLastError())
{
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr,
                   code,
                   0,
                   reinterpret_cast<wchar_t*>(&buffer),
                   0,
                   nullptr);
    const auto detail = buffer ? QString::fromWCharArray(buffer).trimmed() : QString::number(code);
    LocalFree(buffer);
    return operation + ": " + detail;
}

QString quoteArgument(const QString& argument)
{
    QString result  = "\"";
    int     slashes = 0;
    for (const auto character : argument)
    {
        if (character == '\\')
        {
            ++slashes;
            continue;
        }
        result += QString(slashes * (character == '"' ? 2 : 1), '\\');
        if (character == '"')
        {
            result += '\\';
        }
        result  += character;
        slashes  = 0;
    }
    result += QString(slashes * 2, '\\');
    return result + '"';
}
#endif
} // namespace

struct ConsoleDevice::Native
{
    std::mutex              mutex;
    std::condition_variable wake;
    QByteArray              input;
    QByteArray              output;
    qint64                  written   = 0;
    bool                    stopping  = false;
    bool                    exited    = false;
    bool                    readEnded = false;
    int                     exitCode  = 0;
    QString                 failure;
    int                     pendingColumns = 0;
    int                     pendingRows    = 0;
    std::thread             reader;
    std::thread             writer;
    std::thread             waiter;
#ifdef Q_OS_WIN
    HPCON  console       = nullptr;
    HANDLE inputPipe     = INVALID_HANDLE_VALUE;
    HANDLE outputPipe    = INVALID_HANDLE_VALUE;
    HANDLE process       = nullptr;
    HANDLE primaryThread = nullptr;
    HANDLE job           = nullptr;
#else
    int   master = -1;
    pid_t pid    = -1;
    bool  reaped = false;
#endif

    ~Native()
    {
        stop();
    }

    void fail(const QString& message)
    {
        std::lock_guard lock(mutex);
        if (!stopping && !exited && failure.isEmpty())
        {
            failure = message;
        }
    }

    void receive(const char* bytes, qsizetype count)
    {
        std::unique_lock lock(mutex);
        // During shutdown the reader keeps draining ConPTY to avoid ClosePseudoConsole deadlock.
        wake.wait(lock, [&] { return stopping || input.size() + count <= QueueLimit; });
        if (!stopping)
        {
            input.append(bytes, count);
        }
    }

    void stop()
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
#ifdef Q_OS_WIN
        if (job)
        {
            TerminateJobObject(job, 1);
        }
        else if (process)
        {
            TerminateProcess(process, 1);
        }
        if (writer.joinable())
        {
            CancelSynchronousIo(writer.native_handle());
        }
        // The waiter may already own ClosePseudoConsole. Keep its reader alive until
        // that call completes, otherwise cancelling ReadFile could deadlock shutdown.
        if (waiter.joinable())
        {
            waiter.join();
        }
        if (console)
        {
            ClosePseudoConsole(console);
            console = nullptr;
        }
        if (reader.joinable())
        {
            CancelSynchronousIo(reader.native_handle());
        }
#else
        if (pid > 0)
        {
            kill(-pid, SIGKILL);
            std::lock_guard lock(mutex);
            if (!reaped)
            {
                kill(pid, SIGKILL);
            }
        }
#endif
        if (writer.joinable())
        {
            writer.join();
        }
        if (reader.joinable())
        {
            reader.join();
        }
        if (waiter.joinable())
        {
            waiter.join();
        }
#ifdef Q_OS_WIN
        if (inputPipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(inputPipe);
        }
        inputPipe = INVALID_HANDLE_VALUE;
        if (outputPipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(outputPipe);
        }
        outputPipe = INVALID_HANDLE_VALUE;
        if (process)
        {
            CloseHandle(process);
        }
        process = nullptr;
        if (primaryThread)
        {
            CloseHandle(primaryThread);
        }
        primaryThread = nullptr;
        if (job)
        {
            CloseHandle(job);
        }
        job = nullptr;
#else
        if (master >= 0)
        {
            ::close(master);
        }
        master = -1;
        if (pid > 0 && !reaped)
        {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            {
            }
        }
        pid = -1;
#endif
    }

    bool start(QString                    program,
               QStringList                arguments,
               const QString&             directory,
               const QProcessEnvironment& environment,
               int                        columns,
               int                        rows,
               QString*                   error)
    {
#ifdef Q_OS_WIN
        QString    batchCommand;
        const auto extension = QFileInfo(program).suffix().toLower();
        if (extension == "cmd" || extension == "bat")
        {
            auto batchArguments = arguments;
            batchArguments.prepend(QDir::toNativeSeparators(program));
            // cmd has no general argv API. Reject values it cannot represent literally;
            // never interpolate percent expansions, quotes or command separators from argv.
            for (const auto& value : batchArguments)
            {
                if (value.contains('"') || value.contains('%') || value.contains('\r') || value.contains('\n'))
                {
                    return reject(
                        error,
                        "Batch script paths and arguments cannot contain quotes, percent signs or line breaks");
                }
                if (!batchCommand.isEmpty())
                {
                    batchCommand += ' ';
                }
                batchCommand += '"' + value + '"';
            }
            program = QDir(environment.value("SystemRoot", "C:/Windows")).filePath("System32/cmd.exe");
        }
        else if (extension == "ps1")
        {
            arguments.prepend(program);
            arguments.prepend("-File");
            auto shell = resolveProgram("pwsh", environment);
            if (shell.isEmpty())
            {
                shell = QDir(environment.value("SystemRoot", "C:/Windows"))
                            .filePath("System32/WindowsPowerShell/v1.0/powershell.exe");
            }
            program = shell;
        }
        HANDLE childInput  = INVALID_HANDLE_VALUE;
        HANDLE childOutput = INVALID_HANDLE_VALUE;
        if (!CreatePipe(&childInput, &inputPipe, nullptr, 0))
        {
            return reject(error, nativeError("Creating pseudoterminal input"));
        }
        if (!CreatePipe(&outputPipe, &childOutput, nullptr, 0))
        {
            CloseHandle(childInput);
            return reject(error, nativeError("Creating pseudoterminal output"));
        }
        const COORD size {static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
        const auto  result = CreatePseudoConsole(size, childInput, childOutput, 0, &console);
        CloseHandle(childInput);
        CloseHandle(childOutput);
        if (FAILED(result))
        {
            return reject(error, nativeError("Creating pseudoterminal", result));
        }

        SIZE_T attributeSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
        std::vector<unsigned char> attributeStorage(attributeSize);
        auto*                      attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize))
        {
            return reject(error, nativeError("Initializing process attributes"));
        }
        if (!UpdateProcThreadAttribute(attributes,
                                       0,
                                       PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                       console,
                                       sizeof(console),
                                       nullptr,
                                       nullptr))
        {
            const auto message = nativeError("Attaching pseudoterminal");
            DeleteProcThreadAttributeList(attributes);
            return reject(error, message);
        }
        QString command = quoteArgument(QDir::toNativeSeparators(program));
        if (!batchCommand.isEmpty())
        {
            command += " /d /v:off /s /c \"" + batchCommand + '"';
        }
        else
        {
            for (const auto& argument : arguments)
            {
                command += ' ' + quoteArgument(argument);
            }
        }
        if (command.size() >= 32767)
        {
            DeleteProcThreadAttributeList(attributes);
            return reject(error, "Windows shell command line exceeds 32766 characters");
        }
        auto commandBuffer = command.toStdWString();
        auto entries       = environment.toStringList();
        std::sort(entries.begin(), entries.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        std::wstring environmentBlock;
        for (const auto& entry : entries)
        {
            environmentBlock += entry.toStdWString();
            environmentBlock += L'\0';
        }
        environmentBlock += L'\0';
        if (entries.isEmpty())
        {
            environmentBlock += L'\0';
        }
        const auto     workingDirectory = QDir::toNativeSeparators(directory).toStdWString();
        STARTUPINFOEXW startup {};
        startup.StartupInfo.cb         = sizeof(startup);
        // Explicit null standard handles let console initialization bind ConPTY.
        // Otherwise Windows can duplicate the host's redirected test/log handles
        // even with bInheritHandles=FALSE, bypassing the pseudoterminal entirely.
        startup.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput  = nullptr;
        startup.StartupInfo.hStdOutput = nullptr;
        startup.StartupInfo.hStdError  = nullptr;
        startup.lpAttributeList        = attributes;
        PROCESS_INFORMATION information {};
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        {
            const auto message = nativeError("Preparing shell process ownership");
            DeleteProcThreadAttributeList(attributes);
            return reject(error, message);
        }
        const bool created       = CreateProcessW(reinterpret_cast<LPCWSTR>(program.utf16()),
                                                  commandBuffer.data(),
                                                  nullptr,
                                                  nullptr,
                                                  FALSE,
                                                  EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT
                                                      | CREATE_SUSPENDED,
                                                  environmentBlock.data(),
                                                  directory.isEmpty() ? nullptr : workingDirectory.c_str(),
                                                  &startup.StartupInfo,
                                                  &information);
        const auto creationError = GetLastError();
        DeleteProcThreadAttributeList(attributes);
        if (!created)
        {
            return reject(error, nativeError("Starting shell", creationError));
        }
        process       = information.hProcess;
        primaryThread = information.hThread;
        if (!AssignProcessToJobObject(job, process))
        {
            const auto message = nativeError("Owning shell process tree");
            TerminateProcess(process, 1);
            CloseHandle(std::exchange(primaryThread, nullptr));
            return reject(error, message);
        }
        // The reader starts before the child and remains independent while ConPTY closes.
        reader             = std::thread([this] {
            char  buffer[ChunkSize];
            DWORD count = 0;
            while (ReadFile(outputPipe, buffer, sizeof(buffer), &count, nullptr) && count)
            {
                receive(buffer, count);
            }
            const auto code = GetLastError();
            if (code != ERROR_BROKEN_PIPE && code != ERROR_OPERATION_ABORTED && code != ERROR_SUCCESS)
            {
                fail(nativeError("Reading pseudoterminal", code));
            }
            std::lock_guard lock(mutex);
            readEnded = true;
        });
        writer             = std::thread([this] {
            for (;;)
            {
                QByteArray bytes;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [&] { return stopping || !output.isEmpty(); });
                    if (stopping)
                    {
                        return;
                    }
                    bytes = output.left(ChunkSize);
                }
                DWORD count = 0;
                if (!WriteFile(inputPipe, bytes.constData(), static_cast<DWORD>(bytes.size()), &count, nullptr))
                {
                    fail(nativeError("Writing pseudoterminal"));
                    return;
                }
                {
                    std::lock_guard lock(mutex);
                    output.remove(0, count);
                    written += count;
                }
                if (!count)
                {
                    fail("Pseudoterminal accepted no output bytes");
                    return;
                }
            }
        });
        waiter             = std::thread([this] {
            DWORD waitResult = WAIT_TIMEOUT;
            while ((waitResult = WaitForSingleObject(process, 10)) == WAIT_TIMEOUT)
            {
                int columns = 0;
                int rows    = 0;
                {
                    std::lock_guard lock(mutex);
                    if (!stopping)
                    {
                        columns = std::exchange(pendingColumns, 0);
                        rows    = std::exchange(pendingRows, 0);
                    }
                }
                if (columns && rows)
                {
                    const COORD size {static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
                    const auto  result = ResizePseudoConsole(console, size);
                    if (FAILED(result))
                    {
                        fail(nativeError("Resizing pseudoterminal", result));
                    }
                }
            }
            if (waitResult != WAIT_OBJECT_0)
            {
                fail(nativeError("Waiting for shell process"));
                return;
            }
            DWORD code = 0;
            GetExitCodeProcess(process, &code);
            HPCON closingConsole = nullptr;
            {
                std::lock_guard lock(mutex);
                exitCode       = static_cast<int>(code);
                exited         = true;
                closingConsole = std::exchange(console, nullptr);
            }
            // A terminal endpoint owns the tree, including children left behind by its shell.
            TerminateJobObject(job, 1);
            if (closingConsole)
            {
                ClosePseudoConsole(closingConsole);
            }
        });
        const auto resumed = ResumeThread(primaryThread);
        CloseHandle(std::exchange(primaryThread, nullptr));
        if (resumed == static_cast<DWORD>(-1))
        {
            return reject(error, nativeError("Resuming shell"));
        }
        return true;
#else
        master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0)
        {
            return reject(error, QString("Creating pseudoterminal: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        }
        char slaveName[4096];
#ifdef Q_OS_LINUX
        if (ptsname_r(master, slaveName, sizeof(slaveName)) != 0)
        {
            return reject(error, "Resolving pseudoterminal slave failed");
        }
#else
        const auto* name = ptsname(master);
        if (!name || std::strlen(name) >= sizeof(slaveName))
        {
            return reject(error, "Resolving pseudoterminal slave failed");
        }
        std::strcpy(slaveName, name);
#endif
        const int slave = ::open(slaveName, O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (slave < 0)
        {
            return reject(error, "Opening pseudoterminal slave failed");
        }
        winsize size {};
        size.ws_col = static_cast<unsigned short>(columns);
        size.ws_row = static_cast<unsigned short>(rows);
        ioctl(slave, TIOCSWINSZ, &size);
        std::vector<QByteArray> argumentStorage;
        argumentStorage.push_back(QFile::encodeName(program));
        for (const auto& argument : arguments)
        {
            argumentStorage.push_back(argument.toUtf8());
        }
        std::vector<char*> argv;
        for (auto& argument : argumentStorage)
        {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);
        std::vector<QByteArray> environmentStorage;
        for (const auto& entry : environment.toStringList())
        {
            environmentStorage.push_back(entry.toUtf8());
        }
        std::vector<char*> envp;
        for (auto& entry : environmentStorage)
        {
            envp.push_back(entry.data());
        }
        envp.push_back(nullptr);
        const auto directoryBytes  = QFile::encodeName(directory);
        const bool changeDirectory = !directory.isEmpty();
        int        startupPipe[2];
        if (pipe(startupPipe) != 0)
        {
            ::close(slave);
            return reject(error, "Creating shell startup pipe failed");
        }
        fcntl(startupPipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(startupPipe[1], F_SETFD, FD_CLOEXEC);
        const char*  executable          = argumentStorage.front().constData();
        char* const* argumentPointers    = argv.data();
        char* const* environmentPointers = envp.data();
        const char*  directoryPointer    = directoryBytes.constData();
        pid                              = fork();
        if (pid == 0)
        {
            // Only async-signal-safe syscalls occur after fork in this multithreaded Qt process.
            ::close(startupPipe[0]);
            int failureCode = 0;
            if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0 || dup2(slave, STDIN_FILENO) < 0
                || dup2(slave, STDOUT_FILENO) < 0 || dup2(slave, STDERR_FILENO) < 0
                || (changeDirectory && chdir(directoryPointer) < 0))
            {
                failureCode = errno;
            }
            if (slave > STDERR_FILENO)
            {
                ::close(slave);
            }
            ::close(master);
            if (!failureCode)
            {
                execve(executable, argumentPointers, environmentPointers);
                failureCode = errno;
            }
            const auto ignored = ::write(startupPipe[1], &failureCode, sizeof(failureCode));
            (void)ignored;
            _exit(127);
        }
        ::close(slave);
        ::close(startupPipe[1]);
        if (pid < 0)
        {
            ::close(startupPipe[0]);
            return reject(error, "Forking shell failed");
        }
        int     failureCode  = 0;
        ssize_t startupCount = 0;
        do
        {
            startupCount = ::read(startupPipe[0], &failureCode, sizeof(failureCode));
        } while (startupCount < 0 && errno == EINTR);
        ::close(startupPipe[0]);
        if (startupCount != 0)
        {
            return reject(error, QString("Starting shell: %1").arg(QString::fromLocal8Bit(strerror(failureCode))));
        }
        reader = std::thread([this] {
            char buffer[ChunkSize];
            bool ended = false;
            for (;;)
            {
                bool hasOutput = false;
                bool canRead   = false;
                {
                    std::lock_guard lock(mutex);
                    if (stopping)
                    {
                        return;
                    }
                    hasOutput = !output.isEmpty();
                    canRead   = input.size() + ChunkSize <= QueueLimit;
                }
                pollfd descriptor {master, static_cast<short>((canRead ? POLLIN : 0) | (hasOutput ? POLLOUT : 0)), 0};
                const auto ready = poll(&descriptor, 1, 20);
                if (ready < 0 && errno != EINTR)
                {
                    fail(QString("Polling pseudoterminal: %1").arg(QString::fromLocal8Bit(strerror(errno))));
                    return;
                }
                if (canRead && (descriptor.revents & (POLLIN | POLLHUP)))
                {
                    const auto count = ::read(master, buffer, sizeof(buffer));
                    if (count > 0)
                    {
                        receive(buffer, count);
                    }
                    else if (count == 0 || (count < 0 && errno == EIO))
                    {
                        ended = true;
                    }
                    else if (errno != EAGAIN && errno != EINTR)
                    {
                        fail(QString("Reading pseudoterminal: %1").arg(QString::fromLocal8Bit(strerror(errno))));
                        return;
                    }
                }
                if (descriptor.revents & POLLOUT)
                {
                    std::lock_guard lock(mutex);
                    const auto      count = ::write(master, output.constData(), std::min(output.size(), ChunkSize));
                    if (count > 0)
                    {
                        output.remove(0, count);
                        written += count;
                    }
                    else if (count < 0 && errno != EAGAIN && errno != EINTR)
                    {
                        failure = QString("Writing pseudoterminal: %1").arg(QString::fromLocal8Bit(strerror(errno)));
                        return;
                    }
                }
                int        status = 0;
                const auto child  = waitpid(pid, &status, WNOHANG);
                {
                    std::lock_guard lock(mutex);
                    if (child == pid)
                    {
                        reaped   = true;
                        exited   = true;
                        exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                        kill(-pid, SIGKILL);
                    }
                    readEnded = ended;
                    if (exited && readEnded)
                    {
                        return;
                    }
                }
            }
        });
        return true;
#endif
    }
};

ConsoleDevice::ConsoleDevice()
{
    _timer.setInterval(5);
    connect(&_timer, &QTimer::timeout, this, [this] { _pump(); });
}

ConsoleDevice::~ConsoleDevice()
{
    _callbacks = {};
    if (_elevated)
    {
        _elevated->setCallbacks({});
    }
    close();
}

QObject* ConsoleDevice::object()
{
    return this;
}
SerialDevice::State ConsoleDevice::state() const
{
    return _elevated ? _elevated->state() : _state;
}
void ConsoleDevice::setCallbacks(Callbacks callbacks)
{
    _callbacks = std::move(callbacks);
    if (_elevated)
    {
        _elevated->setCallbacks(_callbacks);
    }
}
bool ConsoleDevice::autoReconnect() const
{
    return false;
}

bool ConsoleDevice::validate(const QVariantMap& configuration, QString* error)
{
    const auto administrator = configuration.value("runAsAdministrator");
    if (administrator.isValid() && administrator.metaType().id() != QMetaType::Bool)
    {
        return reject(error, "runAsAdministrator must be a boolean");
    }
    qsizetype aggregateSize = 0;
    for (const auto& key : {QStringLiteral("program"), QStringLiteral("workingDirectory")})
    {
        const auto value = configuration.value(key);
        if (value.isValid() && (value.metaType().id() != QMetaType::QString || value.toString().contains(QChar::Null)))
        {
            return reject(error, key + " must be a string without NUL characters");
        }
    }
    for (const auto& key : {QStringLiteral("program"), QStringLiteral("workingDirectory")})
    {
        const auto size = configuration.value(key).toString().size();
        if (size > 32767)
        {
            return reject(error, key + " exceeds 32767 characters");
        }
        aggregateSize += size;
    }
    const auto arguments = configuration.value("arguments");
    if (arguments.isValid())
    {
        if (arguments.metaType().id() != QMetaType::QStringList && arguments.metaType().id() != QMetaType::QVariantList)
        {
            return reject(error, "arguments must be a list of strings");
        }
        const auto list = arguments.toList();
        if (list.size() > 256)
        {
            return reject(error, "arguments exceeds 256 entries");
        }
        for (const auto& argument : list)
        {
            if (argument.metaType().id() != QMetaType::QString || argument.toString().contains(QChar::Null))
            {
                return reject(error, "arguments must contain strings without NUL characters");
            }
            if (argument.toString().size() > 32767)
            {
                return reject(error, "An argument exceeds 32767 characters");
            }
            aggregateSize += argument.toString().size() + 1;
            if (aggregateSize > 1024 * 1024)
            {
                return reject(error, "Shell configuration exceeds 1 Mi characters");
            }
        }
    }
    const auto environment = configuration.value("environment");
    if (environment.isValid() && environment.metaType().id() != QMetaType::QVariantMap)
    {
        return reject(error, "environment must be a map");
    }
    return ConsoleEnvironment::validateOverrides(environment.toMap(), error);
}

bool ConsoleDevice::open(const QVariantMap& configuration, QString* error)
{
    qCDebug(consoleLogTrace) << "Open requested";
    close();
    QString message;
    if (!validate(configuration, &message))
    {
        return reject(error, message);
    }
    _readPaused = false;
    if (configuration.value("runAsAdministrator", false).toBool())
    {
#if defined(Q_OS_WIN) && !defined(TERMASTER_CONSOLE_HOST)
        auto elevated = std::make_shared<ElevatedConsoleDevice>();
        elevated->setCallbacks(_callbacks);
        elevated->resize(_columns, _rows);
        _elevated = std::move(elevated);
        return _elevated->open(configuration, error);
#else
        return reject(error, "Administrator consoles are supported only by the Windows application");
#endif
    }
    auto environment = ConsoleEnvironment::userEnvironment(&message);
    if (!message.isEmpty())
    {
        return reject(error, message);
    }
    const auto overrides = configuration.value("environment").toMap();
#ifndef Q_OS_WIN
    if (!environment.contains("TERM") && !overrides.contains("TERM"))
    {
        environment.insert("TERM", "xterm-256color");
    }
#endif
    environment = ConsoleEnvironment::apply(environment, overrides, &message);
    if (!message.isEmpty())
    {
        return reject(error, message);
    }
    auto program = configuration.value("program").toString();
    if (program.isEmpty())
    {
        program = defaultProgram(environment);
    }
    program = resolveProgram(program, environment);
    if (program.isEmpty() || !QFileInfo(program).isFile())
    {
        return reject(error, "Shell executable does not exist or cannot be resolved");
    }
    const auto directory = configuration.value("workingDirectory").toString();
    if (!directory.isEmpty() && !QFileInfo(directory).isDir())
    {
        return reject(error, "Working directory does not exist");
    }
    _setState(State::Opening);
    auto native  = std::make_unique<Native>();
    bool started = false;
    try
    {
        started = native->start(program,
                                configuration.value("arguments").toStringList(),
                                directory,
                                environment,
                                _columns,
                                _rows,
                                &message);
    }
    catch (const std::exception&)
    {
        message = "Creating native terminal workers failed";
    }
    if (!started)
    {
        native.reset();
        _setState(State::Failed);
        return reject(error, message);
    }
    _native = std::move(native);
    _timer.start();
    _setState(State::Open);
    return true;
}

void ConsoleDevice::close()
{
    _timer.stop();
    if (_elevated)
    {
        auto elevated = std::move(_elevated);
        elevated->close();
        return;
    }
    if (_native)
    {
        _setState(State::Closing);
        _native.reset();
    }
    _setState(State::Closed);
}

qint64 ConsoleDevice::write(const QByteArray& data, QString* error)
{
    qCDebug(consoleLogTrace) << "Write requested bytes" << data.size();
    if (_elevated)
    {
        return _elevated->write(data, error);
    }
    if (!_native || _state != State::Open)
    {
        reject(error, "Console is not open");
        return -1;
    }
    qint64 accepted = 0;
    {
        std::lock_guard lock(_native->mutex);
        if (_native->stopping || _native->exited || !_native->failure.isEmpty())
        {
            reject(error, "Console process is no longer accepting input");
            return -1;
        }
        accepted = std::min(data.size(), QueueLimit - _native->output.size());
        _native->output.append(data.constData(), accepted);
    }
    _native->wake.notify_all();
    return accepted;
}

qint64 ConsoleDevice::pendingBytes() const
{
    if (_elevated)
    {
        return _elevated->pendingBytes();
    }
    if (!_native)
    {
        return 0;
    }
    std::lock_guard lock(_native->mutex);
    return _native->output.size();
}

void ConsoleDevice::resize(int columns, int rows)
{
    _columns = std::clamp(columns, 1, 32767);
    _rows    = std::clamp(rows, 1, 32767);
    if (_elevated)
    {
        _elevated->resize(_columns, _rows);
    }
    if (!_native)
    {
        return;
    }
#ifdef Q_OS_WIN
    std::lock_guard lock(_native->mutex);
    _native->pendingColumns = _columns;
    _native->pendingRows    = _rows;
#else
    winsize size {};
    size.ws_col = static_cast<unsigned short>(_columns);
    size.ws_row = static_cast<unsigned short>(_rows);
    if (ioctl(_native->master, TIOCSWINSZ, &size) != 0)
    {
        _native->fail(QString("Resizing pseudoterminal: %1").arg(QString::fromLocal8Bit(strerror(errno))));
    }
#endif
}

void ConsoleDevice::setReadPaused(bool paused)
{
    _readPaused = paused;
    if (!paused)
    {
        _pump();
    }
}

void ConsoleDevice::_setState(State state)
{
    qCDebug(consoleLogTrace) << "State transition requested" << int(_state) << int(state);
    if (_state == state)
    {
        return;
    }
    if (state == State::Failed)
    {
        qCWarning(consoleLog) << "Transport entered failed state";
    }
    else if (state == State::Open || state == State::Closed)
    {
        qCInfo(consoleLog) << "Transport state" << int(state);
    }
    _state = state;
    if (_callbacks.stateChanged)
    {
        _callbacks.stateChanged(state);
    }
}

void ConsoleDevice::_pump()
{
    if (_pumping || !_native)
    {
        return;
    }
    _pumping          = true;
    auto*      native = _native.get();
    QByteArray input;
    qint64     written = 0;
    QString    failure;
    bool       exited   = false;
    int        exitCode = 0;
    {
        std::lock_guard lock(native->mutex);
        if (!_readPaused)
        {
            input.swap(native->input);
        }
        written  = std::exchange(native->written, 0);
        failure  = native->failure;
        exited   = native->exited && native->readEnded && native->input.isEmpty();
        exitCode = native->exitCode;
    }
    native->wake.notify_all();
    if (!input.isEmpty() && _callbacks.received)
    {
        _callbacks.received(input);
    }
    if (_native.get() == native && written && _callbacks.written)
    {
        _callbacks.written(written);
    }
    if (_native.get() == native && (!failure.isEmpty() || exited))
    {
        // Natural exit is delivered only after EOF and the last GUI delivery. Explicit close
        // discards pending terminal output while still draining the native ConPTY pipe.
        native->stop();
        QByteArray finalInput;
        {
            std::lock_guard lock(native->mutex);
            finalInput.swap(native->input);
        }
        if (!finalInput.isEmpty() && _callbacks.received)
        {
            _callbacks.received(finalInput);
        }
        if (_native.get() == native)
        {
            _native.reset();
            _timer.stop();
            _setState(failure.isEmpty() ? State::Closed : State::Failed);
            if (!failure.isEmpty() && _callbacks.error)
            {
                _callbacks.error(failure);
            }
            else if (exited && _callbacks.exited)
            {
                _callbacks.exited(exitCode);
            }
        }
    }
    _pumping = false;
}
