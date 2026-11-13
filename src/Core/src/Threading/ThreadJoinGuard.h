#pragma once

#include <QString>
#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

/** Retains an OS termination fence before provider code can create thread-local state.
 * Qt 6.12 may treat a late Windows wait() as complete before native TLS teardown.
 * Capture on the worker before factory execution; wait after QThread::wait() on the owner.
 */
class ThreadJoinGuard final
{
public:
    ThreadJoinGuard() = default;
    ~ThreadJoinGuard()
    {
#ifdef Q_OS_WIN
        if (_thread)
        {
            CloseHandle(_thread);
        }
#endif
    }
    Q_DISABLE_COPY_MOVE(ThreadJoinGuard)

    bool captureCurrentThread(QString* error = nullptr)
    {
#ifdef Q_OS_WIN
        if (_thread)
        {
            if (error)
            {
                *error = QString::fromLatin1("Worker thread handle was already retained");
            }
            return false;
        }
        if (!DuplicateHandle(GetCurrentProcess(),
                             GetCurrentThread(),
                             GetCurrentProcess(),
                             &_thread,
                             SYNCHRONIZE,
                             FALSE,
                             0))
        {
            if (error)
            {
                *error = QString::fromLatin1("Cannot retain worker thread handle: Windows error %1")
                             .arg(GetLastError());
            }
            return false;
        }
#else
        Q_UNUSED(error)
#endif
        return true;
    }

    bool waitAndClose(QString* error = nullptr)
    {
#ifdef Q_OS_WIN
        if (_thread)
        {
            const auto result = WaitForSingleObject(_thread, INFINITE);
            if (result != WAIT_OBJECT_0)
            {
                if (error)
                {
                    *error = QString::fromLatin1("Cannot join worker thread: Windows wait result %1, error %2")
                                 .arg(result)
                                 .arg(GetLastError());
                }
                return false;
            }
            CloseHandle(_thread);
            _thread = nullptr;
        }
#else
        Q_UNUSED(error)
#endif
        return true;
    }

#ifdef Q_OS_WIN
private:
    HANDLE _thread = nullptr;
#endif
};
