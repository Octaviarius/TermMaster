#include "Elevation/ElevatedConsoleDevice.h"
#include "Elevation/ElevationProtocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTest>
#include <QTimer>
#include <utility>

class ConsoleElevationTests final : public QObject
{
    Q_OBJECT
private:
    QString _failure;
    void    _watch(SerialDevice& device, SerialDevice::Callbacks callbacks = {})
    {
        const auto original = callbacks.error;
        callbacks.error     = [this, original](const QString& message) {
            _failure = message;
            if (original)
            {
                original(message);
            }
        };
        device.setCallbacks(std::move(callbacks));
    }
    bool _waitOpen(SerialDevice& device)
    {
        QElapsedTimer timeout;
        timeout.start();
        while (device.state() == SerialDevice::State::Opening && timeout.elapsed() < 10000)
        {
            QTest::qWait(10);
        }
        return device.state() == SerialDevice::State::Open;
    }
    static QVariantMap _configuration(const QStringList& arguments = {})
    {
        return {
            {  "program", QDir(QCoreApplication::applicationDirPath()).filePath("ConsoleChild.exe")},
            {"arguments",                                                                 arguments}
        };
    }
    static ElevatedConsoleDevice::Launcher _launcher(QProcess& process, bool invalidHandshake = false)
    {
        return [&process, invalidHandshake](const QString& executable, const QStringList& arguments, QString* error) {
            if (process.state() != QProcess::NotRunning && !process.waitForFinished(5000))
            {
                *error = process.errorString();
                return quint32(0);
            }
            auto command = arguments;
            command.append(invalidHandshake ? "--fake-helper" : "--test-mode");
            if (!invalidHandshake)
            {
                // The UAC-launched helper must find its private Qt runtime without
                // inheriting the development console's Qt/MSVC PATH.
                auto environment = QProcessEnvironment::systemEnvironment();
                environment.remove("PATH");
                process.setProcessEnvironment(environment);
            }
            process.setProgram(invalidHandshake ? QCoreApplication::applicationFilePath() : executable);
            process.setArguments(command);
            process.start();
            if (!process.waitForStarted(5000))
            {
                *error = process.errorString();
                return quint32(0);
            }
            const auto pid = static_cast<quint32>(process.processId());
            // Exercise a helper connection arriving while the launcher has not yet
            // returned its PID to the proxy (ShellExecuteEx may dispatch events).
            QTest::qWait(150);
            return pid;
        };
    }
    static QByteArray _plain(const QByteArray& bytes)
    {
        static const QRegularExpression controls(
            QStringLiteral(R"(\x1b\[[0-?]*[ -/]*[@-~]|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\))"));
        auto text = QString::fromUtf8(bytes);
        text.remove(controls);
        return text.toUtf8();
    }

private slots:
    void init()
    {
        _failure.clear();
    }
    void lifecycle()
    {
        QProcess                helper;
        ElevatedConsoleDevice   device(_launcher(helper));
        QByteArray              output;
        QString                 failure;
        int                     exits   = 0;
        int                     code    = -1;
        qint64                  written = 0;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray& bytes) { output += bytes; };
        callbacks.error    = [&](const QString& message) { failure = message; };
        callbacks.written  = [&](qint64 bytes) { written += bytes; };
        callbacks.exited   = [&](int value) {
            QVERIFY(_plain(output).contains("FINAL_BYTES"));
            QCOMPARE(device.state(), SerialDevice::State::Closed);
            code = value;
            ++exits;
        };
        _watch(device, callbacks);
        device.resize(91, 31);
        QString error;
        QVERIFY2(device.open(_configuration(), &error), qPrintable(error));
        QVERIFY2(_waitOpen(device), qPrintable(_failure));
        QTRY_VERIFY_WITH_TIMEOUT(_plain(output).contains("READY TERMINAL=1"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(_plain(output).contains("SIZE=91,31"), 10000);
        QCOMPARE(device.write("unicode\r\n", &error), qint64(9));
        QTRY_VERIFY_WITH_TIMEOUT(_plain(output).contains(QByteArray("UNICODE=\xE2\x82\xAC")), 10000);
        device.resize(104, 37);
        QTest::qWait(150);
        QCOMPARE(device.write("size\r\n", &error), qint64(6));
        QTRY_VERIFY_WITH_TIMEOUT(_plain(output).contains("SIZE=104,37"), 10000);
        QCOMPARE(device.write("exit\r\n", &error), qint64(6));
        QTRY_COMPARE_WITH_TIMEOUT(exits, 1, 10000);
        QCOMPARE(code, 17);
        QCOMPARE(device.pendingBytes(), qint64(0));
        QVERIFY(written > 0);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        helper.waitForFinished(5000);

        output.clear();
        QVERIFY2(device.open(_configuration(), &error), qPrintable(error));
        QVERIFY2(_waitOpen(device), qPrintable(_failure));
        device.close();
        QCOMPARE(device.state(), SerialDevice::State::Closed);
        QCOMPARE(exits, 1);
        QVERIFY(helper.waitForFinished(5000) || helper.state() == QProcess::NotRunning);
    }

    void boundedInputAndCancellation()
    {
        QProcess              helper;
        ElevatedConsoleDevice device(_launcher(helper));
        QString               error;
        QVERIFY2(device.open(_configuration({"--blocked"}), &error), qPrintable(error));
        QVERIFY2(_waitOpen(device), qPrintable(_failure));
        const QByteArray bytes(2 * 1024 * 1024, 'x');
        const auto       accepted = device.write(bytes, &error);
        QCOMPARE(accepted, qint64(1024 * 1024));
        QCOMPARE(device.pendingBytes(), accepted);
        QCOMPARE(device.write("more", &error), qint64(0));
        QElapsedTimer elapsed;
        elapsed.start();
        device.close();
        QVERIFY(elapsed.elapsed() < 7000);
        QCOMPARE(device.pendingBytes(), qint64(0));
        QVERIFY(helper.waitForFinished(5000) || helper.state() == QProcess::NotRunning);
    }

    void outputBackpressure()
    {
        QProcess                helper;
        ElevatedConsoleDevice   device(_launcher(helper));
        QByteArray              output;
        int                     code = -1;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray& bytes) { output += bytes; };
        callbacks.exited   = [&](int value) { code = value; };
        _watch(device, callbacks);
        QString error;
        QVERIFY2(device.open(_configuration({"--flood"}), &error), qPrintable(error));
        // Stop parent event processing briefly so the helper must bound its output queue.
        QTest::qSleep(500);
        QTRY_COMPARE_WITH_TIMEOUT(code, 23, 15000);
        QVERIFY(_plain(output).contains("FLOOD_END"));
        QVERIFY(output.count('x') >= 2 * 1024 * 1024);
        helper.waitForFinished(5000);
    }

    void invalidHandshake()
    {
        QProcess                helper;
        ElevatedConsoleDevice   device(_launcher(helper, true));
        QString                 failure;
        SerialDevice::Callbacks callbacks;
        callbacks.error = [&](const QString& message) { failure = message; };
        _watch(device, callbacks);
        QString error;
        QVERIFY2(device.open(_configuration(), &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(device.state(), SerialDevice::State::Failed, 10000);
        QVERIFY(failure.contains("authentication"));
        helper.waitForFinished(5000);
    }

    void ownerReleaseInCallback_data()
    {
        QTest::addColumn<bool>("onReceived");
        QTest::newRow("open-state") << false;
        QTest::newRow("received") << true;
    }

    void ownerReleaseInCallback()
    {
        QFETCH(bool, onReceived);
        QProcess                                   replacementHelper;
        QByteArray                                 replacementOutput;
        QString                                    replacementError;
        QProcess                                   helper;
        auto                                       device = std::make_shared<ElevatedConsoleDevice>(_launcher(helper));
        const std::weak_ptr<ElevatedConsoleDevice> weak   = device;
        bool                                       released = false;
        auto                                       release  = [&] {
            device->close();
            device.reset();
            released = true;
            device   = std::make_shared<ElevatedConsoleDevice>(_launcher(replacementHelper));
            SerialDevice::Callbacks next;
            next.received = [&](const QByteArray& bytes) { replacementOutput += bytes; };
            _watch(*device, next);
            device->open(_configuration(), &replacementError);
        };
        SerialDevice::Callbacks callbacks;
        callbacks.stateChanged = [&](SerialDevice::State state) {
            if (!onReceived && state == SerialDevice::State::Open)
            {
                release();
            }
        };
        callbacks.received = [&](const QByteArray&) {
            if (onReceived)
            {
                release();
            }
        };
        _watch(*device, callbacks);
        QString error;
        QVERIFY2(device->open(_configuration(), &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(released, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(weak.expired(), 10000);
        QVERIFY2(replacementError.isEmpty(), qPrintable(replacementError));
        QVERIFY2(_waitOpen(*device), qPrintable(_failure));
        QTRY_VERIFY_WITH_TIMEOUT(_plain(replacementOutput).contains("READY TERMINAL=1"), 10000);
        device->close();
        QVERIFY(replacementHelper.waitForFinished(5000) || replacementHelper.state() == QProcess::NotRunning);
        QVERIFY(helper.waitForFinished(5000) || helper.state() == QProcess::NotRunning);
    }

    void reopenInClosedCallback()
    {
        QProcess                helper;
        auto                    device   = std::make_shared<ElevatedConsoleDevice>(_launcher(helper));
        bool                    reopened = false;
        QString                 failure;
        int                     staleExits = 0;
        QByteArray              output;
        SerialDevice::Callbacks callbacks;
        callbacks.exited       = [&](int) { ++staleExits; };
        callbacks.received     = [&](const QByteArray& bytes) { output += bytes; };
        callbacks.stateChanged = [&](SerialDevice::State state) {
            if (state != SerialDevice::State::Closed || reopened)
            {
                return;
            }
            reopened = true;
            SerialDevice::Callbacks next;
            next.received = [&](const QByteArray& bytes) { output += bytes; };
            next.error    = [&](const QString& message) { failure = message; };
            _watch(*device, next);
            QString error;
            if (!device->open(_configuration(), &error))
            {
                failure = error;
            }
        };
        _watch(*device, callbacks);
        QString error;
        QVERIFY2(device->open(_configuration(), &error), qPrintable(error));
        QVERIFY2(_waitOpen(*device), qPrintable(_failure));
        QCOMPARE(device->write("exit\r\n", &error), qint64(6));
        QTRY_VERIFY_WITH_TIMEOUT(reopened, 10000);
        QVERIFY2(_waitOpen(*device), qPrintable(_failure));
        QTRY_VERIFY_WITH_TIMEOUT(_plain(output).count("READY TERMINAL=1") == 2, 10000);
        QCOMPARE(staleExits, 0);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        device->close();
        QVERIFY(helper.waitForFinished(5000) || helper.state() == QProcess::NotRunning);
    }

    void cancelledLaunch()
    {
        ElevatedConsoleDevice device([](const QString&, const QStringList&, QString* error) {
            *error = "Administrator consent was cancelled";
            return quint32(0);
        });
        QString               error;
        QVERIFY(!device.open(_configuration(), &error));
        QVERIFY(error.contains("cancelled"));
        QCOMPARE(device.state(), SerialDevice::State::Failed);
        QCOMPARE(device.pendingBytes(), qint64(0));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const auto       arguments = application.arguments();
    if (arguments.contains("--fake-helper"))
    {
        QLocalSocket socket;
        const auto   pipeIndex = arguments.indexOf("--pipe");
        if (pipeIndex < 0 || pipeIndex + 1 >= arguments.size())
        {
            return 2;
        }
        socket.connectToServer(arguments.at(pipeIndex + 1));
        if (!socket.waitForConnected(5000))
        {
            return 3;
        }
        socket.write(ConsoleHostProtocol::frame(ConsoleHostProtocol::Type::Hello, "invalid nonce"));
        socket.waitForBytesWritten(1000);
        socket.waitForDisconnected(5000);
        return 0;
    }
    ConsoleElevationTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ConsoleElevationTests.moc"
