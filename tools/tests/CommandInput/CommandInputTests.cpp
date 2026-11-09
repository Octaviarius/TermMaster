#include <QCoreApplication>
#include <QSignalSpy>
#include <QtTest>
#include <Termaster/Core/Terminal/CommandInput.h>
#include <Termaster/Core/Terminal/TerminalSendQueue.h>

class FakeDevice : public SerialDevice
{
public:
    std::function<void()> duringWrite;
    State                 currentState = State::Open;
    qint64                allowance    = 0;
    bool                  fail         = false;
    QByteArray            accepted;
    QList<QByteArray>     submissions;

    QObject* object() override
    {
        return nullptr;
    }
    State state() const override
    {
        return currentState;
    }
    void setCallbacks(Callbacks) override
    {
    }
    bool open(const QVariantMap&, QString*) override
    {
        currentState = State::Open;
        return true;
    }
    void close() override
    {
        currentState = State::Closed;
    }
    qint64 write(const QByteArray& data, QString* error) override
    {
        submissions.append(data);
        if (duringWrite)
        {
            duringWrite();
        }
        if (fail)
        {
            if (error)
            {
                *error = QStringLiteral("Synthetic write failure");
            }
            return -1;
        }
        const qint64 count = qMin<qint64>(allowance, data.size());
        accepted.append(data.first(count));
        return count;
    }
    qint64 pendingBytes() const override
    {
        return 0;
    }
};

class CommandInputTests : public QObject
{
    Q_OBJECT
private slots:
    void preparation()
    {
        QString       error;
        const QString unicode = QString::fromUtf8("\xE2\x82\xAC\xF0\x9F\x98\x80");
        QCOMPARE(*CommandInput::prepare(unicode, "crlf", {}, &error), QByteArray::fromHex("e282acf09f98800d0a"));
        QCOMPARE(*CommandInput::prepare("AT", "lf", {}), QByteArray("AT\n"));
        QCOMPARE(*CommandInput::prepare("AT", "cr", {}), QByteArray("AT\r"));
        QCOMPARE(*CommandInput::prepare("AT", "none", {}), QByteArray("AT"));
        QVERIFY(error.isEmpty());
        QCOMPARE(*CommandInput::prepare("41 54", "nul", {}), QByteArray("41 54\0", 6));
        QCOMPARE(*CommandInput::prepare(QString::fromLatin1("A\0B", 3), "none", {}), QByteArray("A\0B", 3));
        QCOMPARE(*CommandInput::prepare("", "custom", R"(\r\n\0\xFF\\)"), QByteArray::fromHex("0d0a00ff5c"));
        for (const QString custom : {QString("\\"), QString("\\q"), QString("\\x1"), QString("\\xGG")})
        {
            QVERIFY(!CommandInput::prepare("x", "custom", custom, &error));
        }
        QVERIFY(!CommandInput::prepare("x", "unknown", {}, &error));
        const QString maximum(CommandInput::MaximumBytes, QLatin1Char('x'));
        QVERIFY(CommandInput::prepare(maximum, "none", {}));
        QVERIFY(!CommandInput::prepare(maximum, "lf", {}, &error));
        QVERIFY(!CommandInput::prepare(QString(CommandInput::MaximumBytes / 2 + 1, QChar(0x20ac)), "none", {}, &error));
    }
    void partialWritesAndBoundaries()
    {
        FakeDevice        device;
        TerminalSendQueue queue(device);
        QVERIFY(queue.enqueue(QByteArray::fromHex("410042")));
        QVERIFY(queue.enqueue("CD"));
        QCOMPARE(queue.pendingBytes(), 5);
        queue.flush();
        QCOMPARE(queue.pendingBytes(), 5);
        device.allowance = 2;
        queue.flush();
        QCOMPARE(queue.pendingBytes(), 3);
        queue.flush();
        queue.flush();
        QCOMPARE(device.accepted, QByteArray::fromHex("4100424344"));
        QCOMPARE(device.submissions.last(), QByteArray("CD"));
        QCOMPARE(queue.pendingBytes(), 0);
    }
    void timerRetryAndDisconnect()
    {
        FakeDevice        device;
        TerminalSendQueue queue(device);
        QVERIFY(queue.enqueue("old"));
        QTRY_VERIFY(!device.submissions.isEmpty());
        device.allowance = 1;
        QTRY_COMPARE(queue.pendingBytes(), 0);
        QCOMPARE(device.accepted, QByteArray("old"));
        device.allowance = 0;
        QVERIFY(queue.enqueue("discard"));
        device.close();
        queue.reset();
        device.currentState = SerialDevice::State::Open;
        device.allowance    = 100;
        queue.flush();
        QCOMPARE(device.accepted, QByteArray("old"));
        device.close();
        QString error;
        QVERIFY(!queue.enqueue("closed", &error));
        QVERIFY(!error.isEmpty());
    }
    void emptyMessagesAndEntryLimit()
    {
        FakeDevice        device;
        TerminalSendQueue queue(device);
        QVERIFY(queue.enqueue(QByteArray()));
        queue.flush();
        QCOMPARE(device.submissions.size(), 1);
        QCOMPARE(device.submissions.first(), QByteArray());
        queue.flush();
        QCOMPARE(device.submissions.size(), 1);
        for (int index = 0; index < 4096; ++index)
        {
            QVERIFY(queue.enqueue(QByteArray()));
        }
        QVERIFY(!queue.enqueue(QByteArray()));
    }
    void reentrantResetAndDeletion()
    {
        FakeDevice        device;
        TerminalSendQueue queue(device);
        device.allowance = 100;
        QVERIFY(queue.enqueue("old"));
        device.duringWrite = [&] {
            queue.reset();
            QVERIFY(queue.enqueue("new"));
        };
        queue.flush();
        QCOMPARE(queue.pendingBytes(), 3);
        device.duringWrite = {};
        queue.flush();
        QCOMPARE(device.accepted, QByteArray("oldnew"));
        auto* deletedQueue = new TerminalSendQueue(device);
        QVERIFY(deletedQueue->enqueue("delete"));
        device.duringWrite = [&] {
            delete deletedQueue;
            deletedQueue = nullptr;
        };
        deletedQueue->flush();
        QVERIFY(!deletedQueue);
    }
    void limitsAndErrors()
    {
        FakeDevice        device;
        TerminalSendQueue queue(device);
        QString           error;
        QVERIFY(queue.enqueue(QByteArray(TerminalSendQueue::MaximumPendingBytes, 'x')));
        QVERIFY(!queue.enqueue("overflow", &error));
        QCOMPARE(queue.pendingBytes(), TerminalSendQueue::MaximumPendingBytes);
        queue.reset();
        QSignalSpy failures(&queue, &TerminalSendQueue::failed);
        device.fail = true;
        QVERIFY(queue.enqueue("failed"));
        queue.flush();
        QCOMPARE(failures.size(), 1);
        QCOMPARE(queue.pendingBytes(), 0);
        device.fail      = false;
        device.allowance = 100;
        QVERIFY(queue.enqueue("new"));
        queue.flush();
        QCOMPARE(device.accepted, QByteArray("new"));
    }
};
QTEST_GUILESS_MAIN(CommandInputTests)
#include "CommandInputTests.moc"
