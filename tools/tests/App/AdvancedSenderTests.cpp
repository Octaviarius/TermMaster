#include "Terminal/AdvancedSender.h"

#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class SenderDevice final : public SerialDevice
{
public:
    State             currentState = State::Open;
    qint64            allowance    = 65536;
    QByteArray        accepted;
    QList<QByteArray> writes;

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
    qint64 write(const QByteArray& bytes, QString*) override
    {
        writes.append(bytes);
        const qint64 count = qMin<qint64>(allowance, bytes.size());
        accepted.append(bytes.first(count));
        return count;
    }
    qint64 pendingBytes() const override
    {
        return 0;
    }
};

class AdvancedSenderTests : public QObject
{
    Q_OBJECT
private:
    static bool _write(const QString& path, const QByteArray& bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
    }

private slots:
    void rawStreamingBeyondQueueLimit()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QByteArray bytes(TerminalSendQueue::MaximumPendingBytes + 65537, '\0');
        for (qsizetype index = 0; index < bytes.size(); ++index)
        {
            bytes[index] = static_cast<char>(index % 251);
        }
        const QString path = directory.filePath("Raw.bin");
        QVERIFY(_write(path, bytes));
        SenderDevice device;
        device.allowance = 7777;
        TerminalSendQueue queue(device);
        AdvancedSender    sender(queue, device);
        QVERIFY(sender.startFile(path, false, {}));
        QTRY_VERIFY_WITH_TIMEOUT(!sender.active(), 10000);
        QCOMPARE(device.accepted, bytes);
        QCOMPARE(queue.pendingBytes(), 0);
        for (const QByteArray& chunk : device.writes)
        {
            QVERIFY(chunk.size() <= 65536);
        }
    }

    void linesPreserveBytesAcrossReadBoundary()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("Lines.bin");
        QByteArray    first(65535, 'x');
        QByteArray    bytes = first + "\r\n\n";
        bytes.append(QByteArray("A\0B\rC\nlast", 10));
        QVERIFY(_write(path, bytes));
        SenderDevice device;
        device.allowance = TerminalSendQueue::MaximumPendingBytes;
        TerminalSendQueue queue(device);
        AdvancedSender    sender(queue, device);
        const QByteArray  terminator("\0!", 2);
        QVERIFY(sender.startFile(path, true, terminator));
        QTRY_VERIFY_WITH_TIMEOUT(!sender.active(), 5000);
        const QList<QByteArray> expected {first + terminator,
                                          terminator,
                                          QByteArray("A\0B", 3) + terminator,
                                          QByteArray("C") + terminator,
                                          QByteArray("last") + terminator};
        QCOMPARE(device.writes, expected);
        QByteArray joined;
        for (const QByteArray& line : expected)
        {
            joined.append(line);
        }
        QCOMPARE(device.accepted, joined);
    }

    void emptyLinesWithEmptyTerminator()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("EmptyLines.bin");
        QVERIFY(_write(path, "\n\nz"));
        SenderDevice      device;
        TerminalSendQueue queue(device);
        AdvancedSender    sender(queue, device);
        QVERIFY(sender.startFile(path, true, {}));
        QTRY_VERIFY_WITH_TIMEOUT(!sender.active(), 2000);
        QCOMPARE(device.writes, QList<QByteArray>({QByteArray(), QByteArray(), QByteArray("z")}));
    }

    void emptyFileAndLineLimit()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("Input.bin");
        QVERIFY(_write(path, {}));
        SenderDevice      device;
        TerminalSendQueue queue(device);
        AdvancedSender    sender(queue, device);
        QVERIFY(sender.startFile(path, true, "\r"));
        QTRY_VERIFY(!sender.active());
        QVERIFY(device.writes.isEmpty());
        QVERIFY(_write(path, QByteArray(TerminalSendQueue::MaximumPendingBytes, 'x')));
        QSignalSpy failures(&sender, &AdvancedSender::failed);
        QVERIFY(sender.startFile(path, true, "\r"));
        QTRY_COMPARE_WITH_TIMEOUT(failures.size(), 1, 10000);
        QVERIFY(!sender.active());
        QVERIFY(device.writes.isEmpty());
        QVERIFY(failures.first().first().toString().contains("4 MiB"));
    }

    void cancellationAndDisconnectNeverResumeFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("Input.bin");
        QVERIFY(_write(path, QByteArray(200000, 'x')));
        SenderDevice device;
        device.allowance = 0;
        TerminalSendQueue queue(device);
        AdvancedSender    sender(queue, device);
        sender.configure({
            {"disconnectedPolicy", "retain"}
        });
        QVERIFY(sender.startFile(path, false, {}));
        QTRY_COMPARE(queue.pendingSubmissions(), 1);
        sender.stop();
        QVERIFY(!sender.active());
        QCOMPARE(queue.pendingSubmissions(), 0);
        // A new send reuses the producer after the previous file has been closed.
        QVERIFY(sender.startFile(path, false, {}));
        QTRY_COMPARE(queue.pendingSubmissions(), 1);
        device.close();
        sender.connectionChanged();
        QVERIFY(!sender.active());
        QCOMPARE(queue.pendingSubmissions(), 0);
        device.currentState = SerialDevice::State::Open;
        device.allowance    = 65536;
        sender.connectionChanged();
        QTest::qWait(50);
        QVERIFY(device.accepted.isEmpty());
        QVERIFY(_write(path, "replacement"));
        QVERIFY(sender.startFile(path, false, {}));
        QTRY_VERIFY(!sender.active());
        QCOMPARE(device.accepted, QByteArray("replacement"));
    }

    void periodicBackpressureAndDisconnect()
    {
        SenderDevice device;
        device.allowance = 0;
        TerminalSendQueue queue(device);
        AdvancedSender    sender(queue, device);
        sender.configure({
            {"disconnectedPolicy", "retain"}
        });
        QVERIFY(!sender.startPeriodic("bad", 99));
        QVERIFY(sender.startPeriodic("ABC", 100));
        QTRY_COMPARE(queue.pendingSubmissions(), 1);
        QTest::qWait(250);
        QCOMPARE(queue.pendingSubmissions(), 1);
        QCOMPARE(queue.pendingBytes(), 3);
        device.allowance = 1;
        QTRY_COMPARE(device.accepted, QByteArray("ABC"));
        QTest::qWait(40);
        QCOMPARE(device.accepted, QByteArray("ABC"));
        device.allowance = 0;
        QTRY_COMPARE(queue.pendingSubmissions(), 1);
        device.close();
        sender.connectionChanged();
        QVERIFY(!sender.active());
        QCOMPARE(queue.pendingSubmissions(), 0);
        device.currentState = SerialDevice::State::Open;
        device.allowance    = 100;
        sender.connectionChanged();
        QTest::qWait(150);
        QCOMPARE(device.accepted, QByteArray("ABC"));
    }

    void queueChangedStopsFileDuringEnqueue()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("Reentrant.bin");
        QVERIFY(_write(path, QByteArray(200000, 'x')));
        SenderDevice      device;
        TerminalSendQueue queue(device);
        AdvancedSender    sender(queue, device);
        bool              stopped = false;
        connect(&queue, &TerminalSendQueue::changed, &sender, [&] {
            if (!stopped && queue.pendingSubmissions() > 0)
            {
                stopped = true;
                sender.stop();
            }
        });
        QVERIFY(sender.startFile(path, false, {}));
        QTRY_VERIFY(stopped);
        QVERIFY(!sender.active());
        QCOMPARE(queue.pendingSubmissions(), 0);
        QTest::qWait(50);
        QVERIFY(device.writes.isEmpty());
        QVERIFY(!sender.active());
        // The deferred producer must not revive after stop; its file is reusable.
        QVERIFY(_write(path, "replacement"));
        QVERIFY(sender.startFile(path, false, {}));
        QTRY_VERIFY(!sender.active());
        QCOMPARE(device.accepted, QByteArray("replacement"));
    }

    void queueChangedDeletesSenderDuringEnqueue()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("Reentrant.bin");
        QVERIFY(_write(path, QByteArray(200000, 'x')));
        SenderDevice device;
        device.allowance = 0;
        TerminalSendQueue        queue(device);
        QPointer<AdvancedSender> sender = new AdvancedSender(queue, device);
        connect(&queue, &TerminalSendQueue::changed, &queue, [&] {
            if (sender && queue.pendingSubmissions() > 0)
            {
                delete sender.data();
            }
        });
        QVERIFY(sender->startFile(path, false, {}));
        QTRY_VERIFY(sender.isNull());
        // Queue ownership is independent; clear accepted output as runtime teardown does.
        queue.reset();
        QTest::qWait(50);
        QVERIFY(device.accepted.isEmpty());
        QCOMPARE(queue.pendingSubmissions(), 0);
        QVERIFY(_write(path, "closed"));
    }

    void stateChangedStopsOrDeletesSender()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("Reentrant.bin");
        QVERIFY(_write(path, "file"));
        SenderDevice      device;
        TerminalSendQueue queue(device);
        AdvancedSender    sender(queue, device);
        bool              stopped = false;
        connect(&sender, &AdvancedSender::stateChanged, &sender, [&](bool active, const QString&) {
            if (active && !stopped)
            {
                stopped = true;
                sender.stop();
            }
        });
        QVERIFY(sender.startFile(path, false, {}));
        QVERIFY(stopped);
        QVERIFY(!sender.active());
        QTest::qWait(50);
        QVERIFY(device.writes.isEmpty());
        QPointer<AdvancedSender> deletedSender = new AdvancedSender(queue, device);
        connect(deletedSender.data(), &AdvancedSender::stateChanged, &queue, [&](bool active, const QString&) {
            if (active && deletedSender)
            {
                delete deletedSender.data();
            }
        });
        QVERIFY(deletedSender->startFile(path, false, {}));
        QVERIFY(deletedSender.isNull());
        QTest::qWait(50);
        QVERIFY(device.writes.isEmpty());
        QVERIFY(_write(path, "closed"));
    }

    void oversizedTerminatorRejectedBeforeOpening()
    {
        SenderDevice      device;
        TerminalSendQueue queue(device);
        AdvancedSender    sender(queue, device);
        QString           error;
        QVERIFY(!sender.startFile("nonexistent",
                                  true,
                                  QByteArray(TerminalSendQueue::MaximumPendingBytes + 1, 'x'),
                                  &error));
        QVERIFY(error.contains("terminator"));
        QVERIFY(!sender.active());
        QVERIFY(device.writes.isEmpty());
    }

    void disconnectReportsDiscardOnceAndKeepsUntouched()
    {
        SenderDevice device;
        device.allowance = 1;
        TerminalSendQueue queue(device);
        AdvancedSender    sender(queue, device);
        sender.configure({
            {"disconnectedPolicy", "retain"}
        });
        QSignalSpy failures(&sender, &AdvancedSender::failed);
        QVERIFY(sender.submit("abc"));
        QVERIFY(sender.submit("xyz"));
        queue.flush();
        QCOMPARE(device.accepted, QByteArray("a"));
        device.close();
        sender.connectionChanged();
        QCOMPARE(failures.count(), 0);
        QTRY_COMPARE(failures.count(), 1);
        QCOMPARE(failures.first().first().toString(),
                 QString("Disconnected: discarded 1 pending submissions (2 bytes)."));
        QCOMPARE(queue.pendingSubmissions(), 1);
        QCOMPARE(queue.pendingBytes(), 3);
        sender.connectionChanged();
        QCOMPARE(failures.count(), 1);
        device.currentState = SerialDevice::State::Open;
        device.allowance    = 100;
        sender.connectionChanged();
        QTRY_COMPARE(queue.pendingSubmissions(), 0);
        QCOMPARE(device.accepted, QByteArray("axyz"));
        QCOMPARE(failures.count(), 1);
    }

    void manualRetentionOnly()
    {
        SenderDevice      device;
        TerminalSendQueue queue(device);
        AdvancedSender    sender(queue, device);
        device.close();
        sender.connectionChanged();
        QVERIFY(!sender.submit("rejected"));
        sender.configure({
            {"disconnectedPolicy", "retain"}
        });
        QVERIFY(sender.submit(QByteArray("A\0B", 3)));
        QVERIFY(!sender.startPeriodic("periodic", 100));
        QVERIFY(!sender.startFile("unused", false, {}));
        QCOMPARE(queue.pendingSubmissions(), 1);
        device.currentState = SerialDevice::State::Open;
        sender.connectionChanged();
        QTRY_COMPARE(queue.pendingSubmissions(), 0);
        QCOMPARE(device.accepted, QByteArray("A\0B", 3));
        device.allowance = 0;
        QVERIFY(sender.submit("stop"));
        sender.stop();
        QVERIFY(!sender.active());
        QCOMPARE(queue.pendingSubmissions(), 0);
    }
};

QTEST_GUILESS_MAIN(AdvancedSenderTests)
#include "AdvancedSenderTests.moc"
