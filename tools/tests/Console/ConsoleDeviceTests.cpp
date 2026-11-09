#include "ConsoleDevice.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class ConsoleDeviceTests final : public QObject
{
    Q_OBJECT
private:
    /** ConPTY can insert title/cursor controls between adjacent application writes. */
    static QByteArray _plain(const QByteArray& bytes)
    {
        static const QRegularExpression controls(
            QStringLiteral(R"(\x1b\[[0-?]*[ -/]*[@-~]|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\))"));
        auto text = QString::fromUtf8(bytes);
        text.remove(controls);
        return text.toUtf8();
    }

    static QString _child()
    {
#ifdef Q_OS_WIN
        return QDir(QCoreApplication::applicationDirPath()).filePath("ConsoleChild.exe");
#else
        return QDir(QCoreApplication::applicationDirPath()).filePath("ConsoleChild");
#endif
    }

    static QVariantMap _configuration(const QStringList& arguments = {})
    {
        return {
            {  "program",  _child()},
            {"arguments", arguments}
        };
    }

private slots:
    void validation()
    {
        QString error;
        QVERIFY(ConsoleDevice::validate({}, &error));
        QVERIFY(ConsoleDevice::validate(
            {
                {"arguments", QStringList {"one", "two"}}
        },
            &error));
        QVERIFY(!ConsoleDevice::validate(
            {
                {"arguments", QVariantList {42}}
        },
            &error));
        QVERIFY(!ConsoleDevice::validate(
            {
                {"arguments", "one"}
        },
            &error));
        QVERIFY(!ConsoleDevice::validate(
            {
                {"environment", QVariantMap {{"BAD=KEY", "value"}}}
        },
            &error));
        QVERIFY(!ConsoleDevice::validate(
            {
                {"program", QString(QChar::Null)}
        },
            &error));
        QVERIFY(!ConsoleDevice::validate(
            {
                {"program", QString(32768, 'x')}
        },
            &error));
        QVERIFY(!ConsoleDevice::validate(
            {
                {"workingDirectory", QString(32768, 'x')}
        },
            &error));
        QVERIFY(!ConsoleDevice::validate(
            {
                {"arguments", QStringList {QString(32768, 'x')}}
        },
            &error));
        QStringList excessiveCount;
        for (int index = 0; index < 257; ++index)
        {
            excessiveCount.append("x");
        }
        QVERIFY(!ConsoleDevice::validate(
            {
                {"arguments", excessiveCount}
        },
            &error));
        QStringList excessiveSize;
        for (int index = 0; index < 33; ++index)
        {
            excessiveSize.append(QString(32767, 'x'));
        }
        QVERIFY(!ConsoleDevice::validate(
            {
                {"arguments", excessiveSize}
        },
            &error));
        ConsoleDevice device;
        QVERIFY(!device.autoReconnect());
        QVERIFY(!device.open(
            {
                {"program", "termaster-nonexistent-test-program"}
        },
            &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(device.pendingBytes(), 0);
    }

    void terminalResizeEnvironmentAndExit()
    {
        ConsoleDevice           device;
        QByteArray              received;
        qint64                  written  = 0;
        int                     exitCode = -1;
        int                     exits    = 0;
        QString                 asynchronousError;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray& bytes) {
            QCOMPARE(device.state(), SerialDevice::State::Open);
            received += bytes;
        };
        callbacks.written = [&](qint64 count) { written += count; };
        callbacks.error   = [&](const QString& message) { asynchronousError = message; };
        callbacks.exited  = [&](int code) {
            exitCode = code;
            ++exits;
        };
        device.setCallbacks(callbacks);
        device.resize(101, 33);
        auto configuration = _configuration({"argument with spaces", "a\"b", "trailing\\"});
        configuration.insert("environment",
                             QVariantMap {
                                 {"TERMASTER_CONSOLE_TEST", QString::fromUtf8("value-\xE2\x82\xAC")}
        });
        QString error;
        QVERIFY2(device.open(configuration, &error), qPrintable(error));
        QTRY_VERIFY2_WITH_TIMEOUT(_plain(received).contains("READY TERMINAL=1"),
                                  received.left(2048).toHex().constData(),
                                  10000);
        QTRY_VERIFY_WITH_TIMEOUT(_plain(received).contains("SIZE=101,33"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(_plain(received).contains("ENV=value-\xE2\x82\xAC"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(_plain(received).contains("ARG=argument with spaces"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(_plain(received).contains("ARG=a\"b"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(_plain(received).contains("ARG=trailing\\"), 10000);
        device.resize(93, 27);
        QTest::qWait(50);
#ifdef Q_OS_WIN
        QCOMPARE(device.write("size\runicode\rhello\r"), 19);
#else
        QCOMPARE(device.write("size\nunicode\nhello\n"), 19);
#endif
        QTRY_VERIFY_WITH_TIMEOUT(_plain(received).contains("SIZE=93,27"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(_plain(received).contains("UNICODE=\xE2\x82\xAC"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(_plain(received).contains("ECHO=hello"), 10000);
#ifdef Q_OS_WIN
        QCOMPARE(device.write("exit\r"), 5);
#else
        QCOMPARE(device.write("exit\n"), 5);
#endif
        QTRY_COMPARE_WITH_TIMEOUT(exitCode, 17, 10000);
        QCOMPARE(exits, 1);
        QVERIFY(_plain(received).contains("FINAL_BYTES"));
        QCOMPARE(device.state(), SerialDevice::State::Closed);
        QVERIFY2(asynchronousError.isEmpty(), qPrintable(asynchronousError));
        QVERIFY(written >= 24);
        QVERIFY2(device.open(_configuration(), &error), qPrintable(error));
        device.close();
        QCOMPARE(exits, 1);
        QCOMPARE(device.state(), SerialDevice::State::Closed);
    }

    void boundedOutputAndCancellation()
    {
        ConsoleDevice           device;
        QByteArray              received;
        int                     exits = 0;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray& bytes) { received += bytes; };
        callbacks.exited   = [&](int) { ++exits; };
        device.setCallbacks(callbacks);
        QString error;
        QVERIFY2(device.open(_configuration({"--blocked"}), &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(_plain(received).contains("READY"), 10000);
        const auto accepted = device.write(QByteArray(2 * 1024 * 1024, 'x'));
        QCOMPARE(accepted, 1024 * 1024);
        QVERIFY(device.pendingBytes() <= 1024 * 1024);
        QElapsedTimer timer;
        timer.start();
        device.close();
        QVERIFY2(timer.elapsed() < 5000, "Closing a blocked writer did not cancel promptly");
        QCOMPARE(device.pendingBytes(), 0);
        QCOMPARE(exits, 0);
        QVERIFY2(device.open(_configuration(), &error), qPrintable(error));
        device.close();
    }

    void floodDrainsBeforeNaturalExit()
    {
        ConsoleDevice           device;
        QByteArray              tail;
        qsizetype               count    = 0;
        int                     exitCode = -1;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray& bytes) {
            QVERIFY(bytes.size() <= 1024 * 1024);
            count += bytes.size();
            tail  += bytes;
            tail   = tail.right(128);
        };
        callbacks.exited = [&](int code) { exitCode = code; };
        device.setCallbacks(callbacks);
        QString error;
        QVERIFY2(device.open(_configuration({"--flood"}), &error), qPrintable(error));
        // Deliberately withhold the GUI pump while native output exceeds the queue bound.
        QTest::qSleep(100);
        QTRY_COMPARE_WITH_TIMEOUT(exitCode, 23, 20000);
        QVERIFY(count > 2 * 1024 * 1024);
        QVERIFY(tail.contains("FLOOD_END"));
    }

#ifdef Q_OS_WIN
    void processTreeIsOwned()
    {
        ConsoleDevice           device;
        QByteArray              received;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray& bytes) { received += bytes; };
        device.setCallbacks(callbacks);
        QString error;
        QVERIFY2(device.open(_configuration({"--tree"}), &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(_plain(received).contains("CHILD_PID="), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(_plain(received).indexOf('\n', _plain(received).indexOf("CHILD_PID=")) >= 0, 10000);
        const auto plain = _plain(received);
        const auto start = plain.indexOf("CHILD_PID=") + 10;
        const auto pid   = plain.mid(start, plain.indexOf('\n', start) - start).trimmed().toULong();
        QVERIFY(pid > 0);
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        QVERIFY(process != nullptr);
        device.close();
        const auto wait = WaitForSingleObject(process, 5000);
        CloseHandle(process);
        QCOMPARE(wait, static_cast<DWORD>(WAIT_OBJECT_0));
    }

    void batchPathsWithSpaces()
    {
        QTemporaryDir directory(QDir::tempPath() + "/Termaster console test XXXXXX");
        QVERIFY(directory.isValid());
        for (const auto& extension : {QString("cmd"), QString("bat")})
        {
            const auto path = directory.filePath("script with spaces." + extension);
            QFile      file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("@echo off\r\necho BATCH_ARG=[%~1]\r\nexit /b 29\r\n");
            file.close();
            ConsoleDevice           device;
            QByteArray              received;
            int                     exitCode = -1;
            SerialDevice::Callbacks callbacks;
            callbacks.received = [&](const QByteArray& bytes) { received += bytes; };
            callbacks.exited   = [&](int code) { exitCode = code; };
            device.setCallbacks(callbacks);
            QString error;
            QVERIFY2(device.open(
                         {
                             {   "program",path                                           },
                             {"arguments",
                              QStringList {"value with spaces"}}
            },
                         &error),
                     qPrintable(error));
            QTRY_COMPARE_WITH_TIMEOUT(exitCode, 29, 10000);
            QVERIFY(_plain(received).contains("BATCH_ARG=[value with spaces]"));
            QVERIFY(!device.open(
                {
                    {   "program",path                                  },
                    {"arguments",
                     QStringList {"%PATH%"}}
            },
                &error));
        }
    }
#endif
};

QTEST_GUILESS_MAIN(ConsoleDeviceTests)
#include "ConsoleDeviceTests.moc"
