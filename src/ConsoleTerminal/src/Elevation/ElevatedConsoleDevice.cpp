#include "ElevatedConsoleDevice.h"

#include "ElevationProtocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <shellapi.h>
#include <windows.h>
#endif

namespace
{
constexpr qint64 QueueLimit = 1024 * 1024;
using Type                  = ConsoleHostProtocol::Type;

QByteArray randomToken()
{
    QByteArray bytes;
    for (int index = 0; index < 8; ++index)
    {
        const auto value = QRandomGenerator::system()->generate();
        bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
    }
    return bytes.toHex();
}

#ifdef Q_OS_WIN
QString windowsError(const QString& operation, DWORD code = GetLastError())
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

quint32 launchElevated(const QString& executable, const QStringList& arguments, void** process, QString* error)
{
    // Only generated pipe/nonce/PID tokens enter this command line, never shell configuration.
    const auto        parameters  = arguments.join(' ').toStdWString();
    const auto        path        = QDir::toNativeSeparators(executable).toStdWString();
    const HRESULT     initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFOW information {};
    information.cbSize       = sizeof(information);
    information.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    information.lpVerb       = L"runas";
    information.lpFile       = path.c_str();
    information.lpParameters = parameters.c_str();
    information.nShow        = SW_HIDE;
    const bool started       = ShellExecuteExW(&information);
    const auto code          = GetLastError();
    if (SUCCEEDED(initialized))
    {
        CoUninitialize();
    }
    if (!started)
    {
        *error = code == ERROR_CANCELLED ? QStringLiteral("Administrator consent was cancelled") :
                                           windowsError("Starting administrator console helper", code);
        return 0;
    }
    const auto pid = GetProcessId(information.hProcess);
    *process       = information.hProcess;
    if (!pid)
    {
        *error = windowsError("Identifying administrator console helper");
    }
    return pid;
}
#endif
} // namespace

ElevatedConsoleDevice::ElevatedConsoleDevice(Launcher launcher) : _launcher(std::move(launcher))
{
    _startup.setSingleShot(true);
    connect(&_startup, &QTimer::timeout, this, [this] { _fail("Administrator console helper startup timed out"); });
    // Accept and protocol callbacks may release the final device owner. Deliver them
    // after the native server/socket notification stack has unwound.
    connect(&_server, &QLocalServer::newConnection, this, [this] { _accept(); }, Qt::QueuedConnection);
}

ElevatedConsoleDevice::~ElevatedConsoleDevice()
{
    _callbacks = {};
    close();
}
QObject* ElevatedConsoleDevice::object()
{
    return this;
}
SerialDevice::State ElevatedConsoleDevice::state() const
{
    return _state;
}
void ElevatedConsoleDevice::setCallbacks(Callbacks callbacks)
{
    _callbacks = std::move(callbacks);
}
bool ElevatedConsoleDevice::autoReconnect() const
{
    return false;
}
qint64 ElevatedConsoleDevice::pendingBytes() const
{
    return _pending;
}

bool ElevatedConsoleDevice::open(const QVariantMap& configuration, QString* error)
{
    const auto lifetime = weak_from_this().lock();
    close();
    if (_state != State::Closed)
    {
        if (error)
        {
            *error = "Administrator console was reopened during closure";
        }
        return false;
    }
    const auto generation = _generation;
#ifndef Q_OS_WIN
    if (error)
    {
        *error = "Administrator consoles are supported only on Windows";
    }
    return false;
#else
    const auto executable = QDir(QCoreApplication::applicationDirPath()).filePath("Helpers/ConsoleHost.exe");
    if (!QFileInfo::exists(executable))
    {
        if (error)
        {
            *error = "Administrator console helper does not exist: " + executable;
        }
        return false;
    }
    _configuration = configuration;
    _configuration.insert("runAsAdministrator", false);
    _nonce          = randomToken();
    const auto pipe = "termaster-console-" + QString::fromLatin1(randomToken());
    _server.setSocketOptions(QLocalServer::WorldAccessOption);
    _server.setMaxPendingConnections(4);
    if (!_server.listen(pipe))
    {
        if (error)
        {
            *error = "Creating administrator console channel: " + _server.errorString();
        }
        _cleanup();
        return false;
    }
    _setState(State::Opening);
    if (_state != State::Opening || generation != _generation)
    {
        if (error)
        {
            *error = "Administrator console opening was cancelled";
        }
        return false;
    }
    const QStringList arguments {"--pipe",
                                 pipe,
                                 "--nonce",
                                 QString::fromLatin1(_nonce),
                                 "--parent",
                                 QString::number(QCoreApplication::applicationPid())};
    QString           message;
    void*             process = nullptr;
    const auto        pid     = _launcher ? _launcher(executable, arguments, &message) :
                                            launchElevated(executable, arguments, &process, &message);
    if (generation != _generation)
    {
        if (process)
        {
            WaitForSingleObject(static_cast<HANDLE>(process), 5000);
            CloseHandle(static_cast<HANDLE>(process));
        }
        if (error)
        {
            *error = "Administrator console opening was cancelled";
        }
        return false;
    }
    _pid     = pid;
    _process = process;
    if (!_pid)
    {
        _cleanup();
        _setState(State::Failed);
        if (error)
        {
            *error = message.isEmpty() ? "Starting administrator console helper failed" : message;
        }
        return false;
    }
    if (_state != State::Opening || generation != _generation)
    {
        _cleanup();
        if (error)
        {
            *error = "Administrator console opening was cancelled";
        }
        return false;
    }
    if (_launcher)
    {
        _process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, _pid);
    }
    if (!_process)
    {
        message = windowsError("Owning administrator console helper");
        _cleanup();
        _setState(State::Failed);
        if (error)
        {
            *error = message;
        }
        return false;
    }
    // Consent can take arbitrarily long. Only helper startup after consent is timed.
    _startup.start(10000);
    _accept();
    return true;
#endif
}

void ElevatedConsoleDevice::_accept()
{
    const auto lifetime = weak_from_this().lock();
    if (!_pid)
    {
        return;
    }
    while (_server.hasPendingConnections())
    {
        auto* candidate = _server.nextPendingConnection();
        bool  valid     = false;
#ifdef Q_OS_WIN
        ULONG client = 0;
        valid = _pid && GetNamedPipeClientProcessId(reinterpret_cast<HANDLE>(candidate->socketDescriptor()), &client)
             && client == _pid;
#endif
        if (_socket || !valid)
        {
            candidate->abort();
            candidate->deleteLater();
            if (++_attempts > 8)
            {
                _fail("Too many unauthenticated administrator console connections");
                return;
            }
            continue;
        }
        _socket = candidate;
        candidate->setParent(this);
        candidate->setReadBufferSize(ConsoleHostProtocol::MaxFrameSize + 4);
        const auto generation = _generation;
        connect(
            candidate,
            &QLocalSocket::readyRead,
            this,
            [this, generation] {
                if (generation == _generation)
                {
                    _read();
                }
            },
            Qt::QueuedConnection);
        connect(
            candidate,
            &QLocalSocket::disconnected,
            this,
            [this, generation] {
                const auto lifetime = weak_from_this().lock();
                if (generation == _generation && (_state == State::Open || _state == State::Opening))
                {
                    // Drain complete final frames before diagnosing an unexpected close.
                    _read();
                    if (generation == _generation && (_state == State::Open || _state == State::Opening))
                    {
                        _fail("Administrator console channel disconnected unexpectedly");
                    }
                }
            },
            Qt::QueuedConnection);
        _read();
    }
}

void ElevatedConsoleDevice::_read()
{
    const auto lifetime = weak_from_this().lock();
    if (_reading || !_socket)
    {
        return;
    }
    const auto generation  = _generation;
    _reading               = true;
    auto* channel          = _socket;
    _buffer               += channel->readAll();
    QString    error;
    Type       type;
    QByteArray payload;
    while (generation == _generation && _socket == channel && ConsoleHostProtocol::take(_buffer, type, payload, &error))
    {
        if (!_authenticated)
        {
            if (type != Type::Hello || payload != _nonce)
            {
                error = "Administrator console authentication failed";
                break;
            }
            _authenticated = true;
            const QJsonObject start {
                {"configuration", QJsonObject::fromVariantMap(_configuration)},
                {      "columns",                                    _columns},
                {         "rows",                                       _rows}
            };
            _configuration.clear();
            channel->write(
                ConsoleHostProtocol::frame(Type::Start, QJsonDocument(start).toJson(QJsonDocument::Compact)));
            continue;
        }
        if (type == Type::Ready && _state == State::Opening && payload.isEmpty())
        {
            _startup.stop();
            _server.close();
            _setState(State::Open);
        }
        else if (type == Type::Output && _state == State::Open)
        {
            const auto received = _callbacks.received;
            if (received)
            {
                received(payload);
            }
        }
        else if (type == Type::Written && _state == State::Open)
        {
            bool       ok    = false;
            const auto count = payload.toLongLong(&ok);
            if (!ok || count <= 0 || count > _pending)
            {
                error = "Invalid administrator console write acknowledgement";
                break;
            }
            _pending           -= count;
            const auto written  = _callbacks.written;
            if (written)
            {
                written(count);
            }
        }
        else if (type == Type::Exit && _state == State::Open)
        {
            bool       ok   = false;
            const auto code = payload.toInt(&ok);
            if (!ok)
            {
                error = "Invalid administrator console exit status";
                break;
            }
            const auto exited = _callbacks.exited;
            _cleanup();
            const auto closedGeneration = _generation;
            _setState(State::Closed);
            if (_generation == closedGeneration && exited)
            {
                exited(code);
            }
        }
        else if (type == Type::Failure)
        {
            error = QString::fromUtf8(payload);
            if (error.isEmpty())
            {
                error = "Administrator console helper failed";
            }
            break;
        }
        else
        {
            error = "Unexpected administrator console protocol message";
            break;
        }
    }
    _reading = false;
    if (generation == _generation && !error.isEmpty())
    {
        _fail(error);
    }
    if (_socket && (generation != _generation || _socket != channel))
    {
        QTimer::singleShot(0, this, [this] { _read(); });
    }
}

void ElevatedConsoleDevice::_cleanup()
{
    ++_generation;
    _startup.stop();
    _server.close();
    if (_socket)
    {
        auto* socket = std::exchange(_socket, nullptr);
        socket->disconnect(this);
        if (_authenticated && socket->state() == QLocalSocket::ConnectedState)
        {
            socket->write(ConsoleHostProtocol::frame(Type::Close));
            socket->flush();
            socket->waitForBytesWritten(250);
        }
        socket->abort();
        // Protocol callbacks run outside native socket notifications. The pipe is
        // already closed; retaining child ownership also drains deferred destruction
        // before the device's plugin can unload.
        socket->deleteLater();
    }
#ifdef Q_OS_WIN
    if (_process)
    {
        WaitForSingleObject(static_cast<HANDLE>(_process), 5000);
        CloseHandle(static_cast<HANDLE>(_process));
    }
#endif
    _process = nullptr;
    _pid     = 0;
    _pending = 0;
    _buffer.clear();
    _nonce.clear();
    _configuration.clear();
    _authenticated = false;
    _attempts      = 0;
}

void ElevatedConsoleDevice::close()
{
    const auto lifetime   = weak_from_this().lock();
    const auto generation = _generation;
    if (_state == State::Open || _state == State::Opening)
    {
        _setState(State::Closing);
    }
    if (generation != _generation)
    {
        return;
    }
    _cleanup();
    _setState(State::Closed);
}

qint64 ElevatedConsoleDevice::write(const QByteArray& data, QString* error)
{
    const auto lifetime = weak_from_this().lock();
    if (_state != State::Open || !_socket)
    {
        if (error)
        {
            *error = "Administrator console is not open";
        }
        return -1;
    }
    const auto accepted = std::min<qint64>(data.size(), QueueLimit - _pending);
    for (qint64 offset = 0; offset < accepted; offset += 65536)
    {
        const auto chunk = data.mid(offset, std::min<qint64>(65536, accepted - offset));
        if (_socket->write(ConsoleHostProtocol::frame(Type::Input, chunk)) < 0)
        {
            _fail("Writing administrator console channel failed");
            return -1;
        }
        _pending += chunk.size();
    }
    return accepted;
}

void ElevatedConsoleDevice::resize(int columns, int rows)
{
    const auto lifetime = weak_from_this().lock();
    _columns            = std::clamp(columns, 1, 32767);
    _rows               = std::clamp(rows, 1, 32767);
    if (_state == State::Open && _socket)
    {
        if (_socket->bytesToWrite() > 2 * QueueLimit)
        {
            _fail("Administrator console control queue limit exceeded");
            return;
        }
        const QJsonObject size {
            {"columns", _columns},
            {   "rows",    _rows}
        };
        _socket->write(ConsoleHostProtocol::frame(Type::Resize, QJsonDocument(size).toJson(QJsonDocument::Compact)));
    }
}

void ElevatedConsoleDevice::_setState(State state)
{
    if (_state == state)
    {
        return;
    }
    _state             = state;
    const auto changed = _callbacks.stateChanged;
    if (changed)
    {
        changed(state);
    }
}

void ElevatedConsoleDevice::_fail(const QString& message)
{
    const auto lifetime = weak_from_this().lock();
    const auto failed   = _callbacks.error;
    _cleanup();
    const auto generation = _generation;
    _setState(State::Failed);
    if (_generation == generation && failed)
    {
        failed(message);
    }
}
