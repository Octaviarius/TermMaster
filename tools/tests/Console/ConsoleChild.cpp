// Standalone fixture deliberately avoids Qt and the development toolchain's DLL PATH.
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace
{
void size()
{
    int columns = 0;
    int rows    = 0;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO information {};
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &information);
    columns = information.srWindow.Right - information.srWindow.Left + 1;
    rows    = information.srWindow.Bottom - information.srWindow.Top + 1;
#else
    winsize information {};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &information);
    columns = information.ws_col;
    rows    = information.ws_row;
#endif
    std::cout << "SIZE=" << columns << ',' << rows << '\n' << std::flush;
}

std::string environmentValue()
{
#ifdef _WIN32
    wchar_t    value[1024] {};
    const auto length = GetEnvironmentVariableW(L"TERMASTER_CONSOLE_TEST", value, 1024);
    if (!length || length >= 1024)
    {
        return {};
    }
    const auto  count = WideCharToMultiByte(CP_UTF8, 0, value, length, nullptr, 0, nullptr, nullptr);
    std::string utf8(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, length, utf8.data(), count, nullptr, nullptr);
    return utf8;
#else
    const auto* value = std::getenv("TERMASTER_CONSOLE_TEST");
    return value ? value : "";
#endif
}
} // namespace

int main(int argc, char** argv)
{
    bool terminal = false;
#ifdef _WIN32
    DWORD inputMode  = 0;
    DWORD outputMode = 0;
    terminal         = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &inputMode)
                    && GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &outputMode);
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), inputMode & ~ENABLE_ECHO_INPUT);
#else
    terminal = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && isatty(STDERR_FILENO);
    termios mode {};
    tcgetattr(STDIN_FILENO, &mode);
    mode.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &mode);
#endif
    std::cout << "READY TERMINAL=" << (terminal ? 1 : 0) << '\n' << std::flush;
    size();
    std::cout << "ENV=" << environmentValue() << '\n' << std::flush;
    for (int index = 1; index < argc; ++index)
    {
        std::cout << "ARG=" << argv[index] << '\n' << std::flush;
    }
    if (argc > 1 && std::string(argv[1]) == "--blocked")
    {
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    if (argc > 1 && std::string(argv[1]) == "--flood")
    {
        for (int index = 0; index < 2048; ++index)
        {
            std::cout << std::string(1024, 'x') << '\n';
        }
        std::cout << "FLOOD_END\n" << std::flush;
        return 23;
    }
#ifdef _WIN32
    if (argc > 1 && std::string(argv[1]) == "--tree")
    {
        wchar_t executable[32768] {};
        GetModuleFileNameW(nullptr, executable, 32768);
        std::wstring command = L"\"" + std::wstring(executable) + L"\" --blocked";
        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process {};
        if (!CreateProcessW(executable,
                            command.data(),
                            nullptr,
                            nullptr,
                            FALSE,
                            0,
                            nullptr,
                            nullptr,
                            &startup,
                            &process))
        {
            return 91;
        }
        std::cout << "CHILD_PID=" << process.dwProcessId << '\n' << std::flush;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#endif
    std::string line;
    while (std::getline(std::cin, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line == "exit")
        {
            std::cout << "FINAL_BYTES\n" << std::flush;
            return 17;
        }
        if (line == "size")
        {
            size();
        }
        else if (line == "unicode")
        {
            std::cout << "UNICODE=" << '\xE2' << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            std::cout << "\x82\xAC\n" << std::flush;
        }
        else
        {
            std::cout << "ECHO=" << line << '\n' << std::flush;
        }
    }
    return 0;
}
