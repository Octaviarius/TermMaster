#include "../ConsoleDevice.h"
#include "ElevationProtocol.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QTimer>
#include <cmath>
#include <windows.h>

namespace
{
using Type                  = ConsoleHostProtocol::Type;
constexpr qint64 QueueLimit = 4 * 1024 * 1024;
constexpr qint64 InputLimit = 1024 * 1024;

bool geometry(const QJsonObject& object, int& columns, int& rows)
{
    const auto c = object.value("columns").toDouble(-1);
    const auto r = object.value("rows").toDouble(-1);
    if (!std::isfinite(c) || !std::isfinite(r) || c != std::floor(c) || r != std::floor(r) || c < 1 || c > 32767
        || r < 1 || r > 32767)
    {
        return false;
    }
    columns = static_cast<int>(c);
    rows    = static_cast<int>(r);
    return true;
}

class ConsoleHost final : public QObject
{
public:
    ConsoleHost(QString pipe, QString nonce, DWORD parentPid, QObject* parent = nullptr) :
        QObject(parent), _nonce(std::move(nonce)), _parentPid(parentPid)
    {
        _parentProcess = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
        _socket.setReadBufferSize(ConsoleHostProtocol::MaxFrameSize + 4);
        connect(&_socket, &QLocalSocket::connected, this, [this] {
            ULONG serverPid = 0;
            if (!_parentProcess
                || !GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(_socket.socketDescriptor()), &serverPid)
                || serverPid != _parentPid)
            {
                _stop();
                return;
            }
            _send(Type::Hello, _nonce.toUtf8());
        });
        connect(&_socket, &QLocalSocket::readyRead, this, [this] { _read(); });
        connect(&_socket, &QLocalSocket::disconnected, this, [this] { _stop(); });
        connect(&_socket, &QLocalSocket::errorOccurred, this, [this] { _stop(); });
        connect(&_socket, &QLocalSocket::bytesWritten, this, [this] {
            if (_finishing && !_socket.bytesToWrite())
            {
                QCoreApplication::quit();
            }
            else
            {
                _device.setReadPaused(_socket.bytesToWrite() >= 512 * 1024);
            }
        });
        SerialDevice::Callbacks callbacks;
        callbacks.received = [this](const QByteArray& bytes) {
            for (qsizetype offset = 0; offset < bytes.size() && !_finishing; offset += 64 * 1024)
            {
                _send(Type::Output, bytes.mid(offset, 64 * 1024));
            }
            _device.setReadPaused(_socket.bytesToWrite() >= 1024 * 1024);
        };
        callbacks.written = [this](qint64 count) {
            _send(Type::Written, QByteArray::number(count));
            _pumpInput();
        };
        callbacks.error  = [this](const QString& error) { _fail(error); };
        callbacks.exited = [this](int code) { _finish(Type::Exit, QByteArray::number(code)); };
        _device.setCallbacks(std::move(callbacks));
        _watch.setInterval(100);
        connect(&_watch, &QTimer::timeout, this, [this] {
            if (!_parentProcess || WaitForSingleObject(_parentProcess, 0) != WAIT_TIMEOUT)
            {
                _stop();
            }
            else
            {
                _pumpInput();
            }
        });
        _watch.start();
        QTimer::singleShot(10000, this, [this] {
            if (!_started)
            {
                _fail(QStringLiteral("Console host startup timed out"));
            }
        });
        _socket.connectToServer(pipe);
    }

    ~ConsoleHost() override
    {
        _device.setCallbacks({});
        _device.close();
        if (_parentProcess)
        {
            CloseHandle(_parentProcess);
        }
    }

private:
    ConsoleDevice _device;
    QLocalSocket  _socket;
    QTimer        _watch;
    QByteArray    _incoming;
    QByteArray    _pendingInput;
    QString       _nonce;
    DWORD         _parentPid;
    HANDLE        _parentProcess = nullptr;
    bool          _started       = false;
    bool          _finishing     = false;

    void _stop()
    {
        _finishing = true;
        _device.setCallbacks({});
        _device.close();
        _socket.abort();
        QCoreApplication::quit();
    }

    void _send(Type type, const QByteArray& payload = {})
    {
        const auto frame = ConsoleHostProtocol::frame(type, payload);
        const auto limit = _finishing ? QueueLimit : QueueLimit - 1024;
        if (frame.isEmpty() || _socket.bytesToWrite() + frame.size() > limit)
        {
            if (!_finishing)
            {
                _fail(QStringLiteral("Console host output queue limit exceeded"));
            }
            else
            {
                _stop();
            }
            return;
        }
        if (_socket.write(frame) != frame.size())
        {
            _stop();
        }
    }

    void _finish(Type type, const QByteArray& payload = {})
    {
        if (_finishing)
        {
            return;
        }
        _finishing = true;
        _device.setCallbacks({});
        _device.close();
        _send(type, payload);
        _socket.flush();
        if (!_socket.bytesToWrite())
        {
            QCoreApplication::quit();
        }
        QTimer::singleShot(2000, this, [] { QCoreApplication::quit(); });
    }

    void _fail(const QString& error)
    {
        _finish(Type::Failure, error.toUtf8());
    }

    void _pumpInput()
    {
        if (_finishing || _pendingInput.isEmpty())
        {
            return;
        }
        QString    error;
        const auto accepted = _device.write(_pendingInput, &error);
        if (accepted < 0)
        {
            _fail(error);
            return;
        }
        _pendingInput.remove(0, accepted);
    }

    void _read()
    {
        if (_finishing)
        {
            return;
        }
        _incoming.append(_socket.readAll());
        if (_incoming.size() > ConsoleHostProtocol::MaxFrameSize + 4)
        {
            _fail(QStringLiteral("Console host receive buffer limit exceeded"));
            return;
        }
        Type       type;
        QByteArray payload;
        QString    error;
        while (!_finishing && ConsoleHostProtocol::take(_incoming, type, payload, &error))
        {
            if (type == Type::Close && payload.isEmpty())
            {
                _finish(Type::Closed);
                return;
            }
            if (type == Type::Start && !_started)
            {
                _started = true;
                QJsonParseError parseError;
                const auto      document = QJsonDocument::fromJson(payload, &parseError);
                const auto      object   = document.object();
                int             columns  = 0;
                int             rows     = 0;
                if (parseError.error != QJsonParseError::NoError || !document.isObject()
                    || !object.value("configuration").isObject() || !geometry(object, columns, rows))
                {
                    _fail(QStringLiteral("Invalid console host start configuration"));
                    return;
                }
                auto configuration = object.value("configuration").toObject().toVariantMap();
                configuration.insert("runAsAdministrator", false);
                if (!ConsoleDevice::validate(configuration, &error))
                {
                    _fail(error);
                    return;
                }
                _device.resize(columns, rows);
                if (!_device.open(configuration, &error))
                {
                    _fail(error);
                    return;
                }
                _send(Type::Ready);
            }
            else if (_started && type == Type::Input && payload.size() <= 64 * 1024)
            {
                if (_pendingInput.size() + _device.pendingBytes() + payload.size() > InputLimit)
                {
                    _fail(QStringLiteral("Console host input queue limit exceeded"));
                    return;
                }
                _pendingInput.append(payload);
                _pumpInput();
            }
            else if (_started && type == Type::Resize)
            {
                QJsonParseError parseError;
                const auto      document = QJsonDocument::fromJson(payload, &parseError);
                int             columns  = 0;
                int             rows     = 0;
                if (parseError.error != QJsonParseError::NoError || !document.isObject()
                    || !geometry(document.object(), columns, rows))
                {
                    _fail(QStringLiteral("Invalid console host geometry"));
                    return;
                }
                _device.resize(columns, rows);
            }
            else
            {
                _fail(QStringLiteral("Unexpected console host message"));
                return;
            }
        }
        if (!error.isEmpty())
        {
            _fail(error);
        }
    }
};
} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication   application(argc, argv);
    QCommandLineParser parser;
    parser.addOption({"pipe", "Parent pipe", "name"});
    parser.addOption({"nonce", "Handshake nonce", "hex"});
    parser.addOption({"parent", "Parent process", "pid"});
    parser.addOption({"test-mode", "Run without elevation for synthetic verification"});
    if (!parser.parse(application.arguments()))
    {
        return 2;
    }
    const auto               nonce     = parser.value("nonce");
    const auto               pipe      = parser.value("pipe");
    bool                     validPid  = false;
    const auto               parentPid = parser.value("parent").toULongLong(&validPid);
    const QRegularExpression noncePattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    const QRegularExpression pipePattern(QStringLiteral("^termaster-console-[0-9a-fA-F]{64}$"));
    if (nonce.size() != 64 || pipe.size() != QStringLiteral("termaster-console-").size() + 64
        || !noncePattern.match(nonce).hasMatch() || !pipePattern.match(pipe).hasMatch() || !validPid || parentPid == 0
        || parentPid > MAXDWORD || !parser.positionalArguments().isEmpty())
    {
        return 2;
    }
    if (!parser.isSet("test-mode"))
    {
        HANDLE          token = nullptr;
        TOKEN_ELEVATION elevation {};
        DWORD           size = 0;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            return 3;
        }
        const auto success = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
        CloseHandle(token);
        if (!success || !elevation.TokenIsElevated)
        {
            return 3;
        }
    }
    ConsoleHost host(pipe, nonce, static_cast<DWORD>(parentPid));
    return application.exec();
}
