#include <QCoreApplication>
#include <QSemaphore>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <Termaster/Core/Terminal/TerminalSendQueue.h>
#include <Termaster/Core/Terminal/ThreadedSerialDevice.h>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace
{
struct TransportThreadLocalProbe
{
    std::atomic<bool>* completed = nullptr;

    ~TransportThreadLocalProbe()
    {
        if (completed)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            completed->store(true);
        }
    }
};
} // namespace

class WorkerDevice final : public QObject, public SerialDevice
{
public:
    struct Probe
    {
        WorkerDevice*     device            = nullptr;
        std::atomic<int>  wrongThread       = 0;
        std::atomic<int>  destroyed         = 0;
        std::atomic<bool> tlsInstalled      = false;
        std::atomic<bool> tlsCompleted      = false;
        bool              useObjectOverride = false;
        QObject*          objectOverride    = nullptr;
        QSemaphore        openEntered;
        QSemaphore        openRelease;
        QSemaphore        writeEntered;
        QSemaphore        writeRelease;
        QList<QByteArray> packets;
        QByteArray        stream;
        int               columns = 0;
        int               rows    = 0;
    };

    explicit WorkerDevice(Probe& probe, bool atomic = false, bool installThreadLocal = false) :
        _probe(probe), _owner(QThread::currentThread()), _atomic(atomic)
    {
        _probe.device = this;
        if (installThreadLocal)
        {
            thread_local TransportThreadLocalProbe tls;
            tls.completed = &_probe.tlsCompleted;
            _probe.tlsInstalled.store(true);
        }
        if (_owner == QCoreApplication::instance()->thread())
        {
            ++_probe.wrongThread;
        }
    }
    ~WorkerDevice() override
    {
        _check();
        ++_probe.destroyed;
    }
    QObject* object() override
    {
        _check();
        return _probe.useObjectOverride ? _probe.objectOverride : this;
    }
    State state() const override
    {
        _check();
        return _state;
    }
    void setCallbacks(Callbacks callbacks) override
    {
        _check();
        _callbacks = std::move(callbacks);
    }
    bool open(const QVariantMap& configuration, QString* error) override
    {
        _check();
        if (configuration.value("slow").toBool())
        {
            _probe.openEntered.release();
            _probe.openRelease.tryAcquire(1, 2000);
        }
        if (configuration.value("fail").toBool())
        {
            if (error)
            {
                *error = QStringLiteral("Synthetic open failure.");
            }
            return false;
        }
        _blocked      = configuration.value("blocked").toBool();
        _holdWrite    = configuration.value("holdWrite").toBool();
        _deferWritten = configuration.value("deferWritten").toBool();
        _state        = State::Open;
        if (_callbacks.stateChanged)
        {
            _callbacks.stateChanged(_state);
        }
        return true;
    }
    void close() override
    {
        _check();
        _state       = State::Closed;
        _outstanding = 0;
        if (_callbacks.stateChanged)
        {
            _callbacks.stateChanged(_state);
        }
    }
    qint64 write(const QByteArray& bytes, QString*) override
    {
        _check();
        if (_holdWrite)
        {
            _holdWrite = false;
            _probe.writeEntered.release();
            _probe.writeRelease.tryAcquire(1, 2000);
        }
        if (_blocked && !bytes.isEmpty())
        {
            return 0;
        }
        const qint64 accepted = _atomic ? bytes.size() : qMin<qint64>(3, bytes.size());
        const auto   part     = bytes.first(accepted);
        _probe.packets.append(part);
        _probe.stream += part;
        _outstanding  += accepted;
        if (!_deferWritten)
        {
            deliverWritten(accepted);
        }
        return accepted;
    }
    qint64 pendingBytes() const override
    {
        _check();
        return _outstanding;
    }
    bool atomicWrites() const override
    {
        _check();
        return _atomic;
    }
    bool autoReconnect() const override
    {
        _check();
        return false;
    }
    void resize(int columns, int rows) override
    {
        _check();
        _probe.columns = columns;
        _probe.rows    = rows;
    }
    void setBlocked(bool blocked)
    {
        _check();
        _blocked = blocked;
    }
    void deliverWritten(qint64 count)
    {
        _check();
        _outstanding        -= count;
        const auto callback  = _callbacks.written;
        if (callback)
        {
            callback(count);
        }
    }
    void reportError(const QString& message)
    {
        _check();
        const auto callback = _callbacks.error;
        if (callback)
        {
            callback(message);
        }
    }
    void receive(const QByteArray& bytes)
    {
        _check();
        const auto callback = _callbacks.received;
        if (callback)
        {
            callback(bytes);
        }
    }

private:
    Probe&    _probe;
    QThread*  _owner;
    Callbacks _callbacks;
    State     _state        = State::Closed;
    bool      _atomic       = false;
    bool      _blocked      = false;
    bool      _holdWrite    = false;
    bool      _deferWritten = false;
    qint64    _outstanding  = 0;

    void _check() const
    {
        if (QThread::currentThread() != _owner)
        {
            ++_probe.wrongThread;
        }
    }
};

class ThreadedSerialDeviceTests final : public QObject
{
    Q_OBJECT
private slots:
    void affinityPartialWritesAndShutdown()
    {
        WorkerDevice::Probe probe;
        int                 wrongCallbackThread = 0;
        QByteArray          received;
        qint64              written = 0;
        {
            ThreadedSerialDevice device([&] { return std::make_unique<WorkerDevice>(probe); });
            QVERIFY(!device.autoReconnect());
            SerialDevice::Callbacks callbacks;
            callbacks.received = [&](const QByteArray& bytes) {
                if (QThread::currentThread() != thread())
                {
                    ++wrongCallbackThread;
                }
                received += bytes;
            };
            callbacks.written = [&](qint64 count) {
                if (QThread::currentThread() != thread())
                {
                    ++wrongCallbackThread;
                }
                written += count;
            };
            callbacks.stateChanged = [&](SerialDevice::State) {
                if (QThread::currentThread() != thread())
                {
                    ++wrongCallbackThread;
                }
            };
            device.setCallbacks(callbacks);
            device.resize(123, 45);
            QVERIFY(device.open({}));
            QTRY_COMPARE(device.state(), SerialDevice::State::Open);
            const QByteArray bytes("ab\0cdef", 7);
            QCOMPARE(device.write(bytes), bytes.size());
            QTRY_COMPARE(written, bytes.size());
            QCOMPARE(device.pendingBytes(), 0);
            QMetaObject::
                invokeMethod(probe.device, [&] { probe.device->receive(bytes); }, Qt::BlockingQueuedConnection);
            QTRY_COMPARE(received, bytes);
            device.close();
        }
        QCOMPARE(probe.stream, QByteArray("ab\0cdef", 7));
        QCOMPARE(probe.columns, 123);
        QCOMPARE(probe.rows, 45);
        QCOMPARE(probe.destroyed.load(), 1);
        QCOMPARE(probe.wrongThread.load(), 0);
        QCOMPARE(wrongCallbackThread, 0);
    }

    void openIsAsynchronousAndFailureIsReported()
    {
        WorkerDevice::Probe     probe;
        ThreadedSerialDevice    device([&] { return std::make_unique<WorkerDevice>(probe); });
        QString                 error;
        SerialDevice::Callbacks callbacks;
        callbacks.error = [&](const QString& message) { error = message; };
        device.setCallbacks(callbacks);
        QVERIFY(device.open({
            {"slow", true},
            {"fail", true}
        }));
        QCOMPARE(device.state(), SerialDevice::State::Opening);
        QVERIFY(probe.openEntered.tryAcquire(1, 2000));
        bool ownerTurn = false;
        QTimer::singleShot(0, this, [&] { ownerTurn = true; });
        QTRY_VERIFY(ownerTurn);
        probe.openRelease.release();
        QTRY_COMPARE(device.state(), SerialDevice::State::Failed);
        QCOMPARE(error, QStringLiteral("Synthetic open failure."));
    }

    void boundedAdmissionAndCancellation()
    {
        WorkerDevice::Probe  probe;
        ThreadedSerialDevice device([&] { return std::make_unique<WorkerDevice>(probe); });
        QVERIFY(device.open({
            {"blocked", true}
        }));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        const QByteArray bytes(ThreadedSerialDevice::MaximumPendingBytes + 10, 'x');
        QCOMPARE(device.write(bytes), ThreadedSerialDevice::MaximumPendingBytes);
        QCOMPARE(device.pendingBytes(), ThreadedSerialDevice::MaximumPendingBytes);
        QCOMPARE(device.write(QByteArray("x")), 0);
        device.close();
        QCOMPARE(device.pendingBytes(), 0);
        QVERIFY(device.open({}));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QCOMPARE(device.write(QByteArray("new")), 3);
        QTRY_COMPARE(device.pendingBytes(), 0);
        QByteArray stream;
        QMetaObject::invokeMethod(probe.device, [&] { stream = probe.stream; }, Qt::BlockingQueuedConnection);
        QCOMPARE(stream, QByteArray("new"));
    }

    void sendQueueResetDiscardsWorkerAdmissionWithoutDisconnecting()
    {
        WorkerDevice::Probe  probe;
        ThreadedSerialDevice device([&] { return std::make_unique<WorkerDevice>(probe); });
        TerminalSendQueue    queue(device);
        QVERIFY(device.open({
            {"blocked", true}
        }));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QVERIFY(queue.enqueue("old"));
        queue.flush();
        QCOMPARE(queue.pendingBytes(), 0);
        QCOMPARE(device.pendingBytes(), 3);
        queue.reset();
        QTRY_COMPARE(device.pendingBytes(), 0);
        QCOMPARE(device.state(), SerialDevice::State::Open);
        QMetaObject::invokeMethod(probe.device, [&] { probe.device->setBlocked(false); }, Qt::BlockingQueuedConnection);
        QVERIFY(queue.enqueue("new"));
        queue.flush();
        QTRY_COMPARE(device.pendingBytes(), 0);
        QByteArray stream;
        QMetaObject::invokeMethod(probe.device, [&] { stream = probe.stream; }, Qt::BlockingQueuedConnection);
        QCOMPARE(stream, QByteArray("new"));
    }

    void discardDuringPartialWriteKeepsNativeCreditsSeparateFromNewQueue()
    {
        WorkerDevice::Probe  probe;
        ThreadedSerialDevice device([&] { return std::make_unique<WorkerDevice>(probe); });
        QVERIFY(device.open({
            {   "holdWrite", true},
            {"deferWritten", true}
        }));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QCOMPARE(device.write("oldOLD"), 6);
        QVERIFY(probe.writeEntered.tryAcquire(1, 2000));
        device.discardPendingOutput();
        QCOMPARE(device.write("NEW"), 3);
        probe.writeRelease.release();
        QByteArray stream;
        QMetaObject::invokeMethod(
            probe.device,
            [&] {
                stream = probe.stream;
                probe.device->setBlocked(true);
            },
            Qt::BlockingQueuedConnection);
        QCOMPARE(stream, QByteArray("oldNEW"));
        QCOMPARE(device.pendingBytes(), 6);
        QCOMPARE(device.state(), SerialDevice::State::Open);

        // Delayed progress for the accepted old prefix must not consume new queue admission.
        QCOMPARE(device.write("queued"), 6);
        QMetaObject::invokeMethod(probe.device, [&] { probe.device->deliverWritten(6); }, Qt::BlockingQueuedConnection);
        QCOMPARE(device.pendingBytes(), 6);
        device.discardPendingOutput();
        QTRY_COMPARE(device.pendingBytes(), 0);
        QMetaObject::invokeMethod(probe.device, [&] { probe.device->setBlocked(false); }, Qt::BlockingQueuedConnection);
        QCOMPARE(device.write("end"), 3);
        QMetaObject::invokeMethod(
            probe.device,
            [&] {
                stream = probe.stream;
                probe.device->deliverWritten(3);
            },
            Qt::BlockingQueuedConnection);
        QCOMPARE(stream, QByteArray("oldNEWend"));
        QCOMPARE(device.pendingBytes(), 0);
    }

    void atomicPacketsIncludeEmptyAndRejectAtomically()
    {
        WorkerDevice::Probe probe;
        {
            ThreadedSerialDevice device([&] { return std::make_unique<WorkerDevice>(probe, true); });
            QVERIFY(device.atomicWrites());
            QVERIFY(device.open({}));
            QTRY_COMPARE(device.state(), SerialDevice::State::Open);
            QCOMPARE(device.write(QByteArray()), 0);
            const QByteArray bytes("a\0b", 3);
            QCOMPARE(device.write(bytes), 3);
            QCOMPARE(device.write(QByteArray(ThreadedSerialDevice::MaximumPendingBytes + 1, 'x')), 0);
            QTRY_COMPARE(device.pendingBytes(), 0);
        }
        QCOMPARE(probe.packets.size(), 2);
        QCOMPARE(probe.packets.at(0), QByteArray());
        QCOMPARE(probe.packets.at(1), QByteArray("a\0b", 3));
        QCOMPARE(probe.wrongThread.load(), 0);
    }

    void emptyAtomicAdmissionHasPacketBound()
    {
        WorkerDevice::Probe  probe;
        ThreadedSerialDevice device([&] { return std::make_unique<WorkerDevice>(probe, true); });
        QVERIFY(device.open({}));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QSemaphore entered;
        QSemaphore release;
        QMetaObject::invokeMethod(
            probe.device,
            [&] {
                entered.release();
                release.tryAcquire(1, 2000);
            },
            Qt::QueuedConnection);
        QVERIFY(entered.tryAcquire(1, 2000));
        bool allAccepted = true;
        for (qsizetype index = 0; index < ThreadedSerialDevice::MaximumPendingPackets; ++index)
        {
            allAccepted = allAccepted && device.write(QByteArray()) == 0;
        }
        QString    error;
        const auto rejected = device.write(QByteArray(), &error);
        device.close();
        release.release();
        QVERIFY(allAccepted);
        QCOMPARE(rejected, -1);
        QVERIFY(error.contains("empty packet"));
        QCOMPARE(device.pendingBytes(), 0);
    }

    void receiveOverflowFailsAfterAcceptedPrefix()
    {
        WorkerDevice::Probe     probe;
        ThreadedSerialDevice    device([&] { return std::make_unique<WorkerDevice>(probe); });
        qint64                  received = 0;
        QString                 error;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray& bytes) { received += bytes.size(); };
        callbacks.error    = [&](const QString& message) { error = message; };
        device.setCallbacks(callbacks);
        QVERIFY(device.open({}));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QMetaObject::invokeMethod(
            probe.device,
            [&] {
                probe.device->receive(QByteArray(ThreadedSerialDevice::MaximumReceivedBytes, 'x'));
                probe.device->receive(QByteArray("overflow"));
            },
            Qt::BlockingQueuedConnection);
        QTRY_COMPARE(device.state(), SerialDevice::State::Failed);
        QCOMPARE(received, ThreadedSerialDevice::MaximumReceivedBytes);
        QVERIFY(error.contains("receive queue"));
    }

    void emptyReceiveFloodHasEventBound()
    {
        WorkerDevice::Probe     probe;
        ThreadedSerialDevice    device([&] { return std::make_unique<WorkerDevice>(probe); });
        qsizetype               received = 0;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray&) { ++received; };
        device.setCallbacks(callbacks);
        QVERIFY(device.open({}));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QMetaObject::invokeMethod(
            probe.device,
            [&] {
                for (int index = 0; index < 5000; ++index)
                {
                    probe.device->receive(QByteArray());
                }
            },
            Qt::BlockingQueuedConnection);
        QTRY_COMPARE(device.state(), SerialDevice::State::Failed);
        QCOMPARE(received, ThreadedSerialDevice::MaximumCallbackEvents);
    }

    void callbackCanDestroyFacade()
    {
        WorkerDevice::Probe probe;
        auto device = std::make_unique<ThreadedSerialDevice>([&] { return std::make_unique<WorkerDevice>(probe); });
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray&) { device.reset(); };
        device->setCallbacks(callbacks);
        QVERIFY(device->open({}));
        QTRY_COMPARE(device->state(), SerialDevice::State::Open);
        QMetaObject::invokeMethod(probe.device, [&] { probe.device->receive("stop"); }, Qt::BlockingQueuedConnection);
        QTRY_VERIFY(!device);
        QCOMPARE(probe.destroyed.load(), 1);
        QCOMPARE(probe.wrongThread.load(), 0);
    }

    void borrowedReceiveStorageIsCopiedBeforeCallbackReturns()
    {
        WorkerDevice::Probe     probe;
        ThreadedSerialDevice    device([&] { return std::make_unique<WorkerDevice>(probe); });
        QByteArray              received;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray& bytes) { received += bytes; };
        device.setCallbacks(callbacks);
        QVERIFY(device.open({}));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QMetaObject::invokeMethod(
            probe.device,
            [&] {
                QByteArray storage("a\0b", 3);
                probe.device->receive(QByteArray::fromRawData(storage.constData(), storage.size()));
                storage.fill('x');
            },
            Qt::BlockingQueuedConnection);
        QTRY_COMPARE(received, QByteArray("a\0b", 3));
    }

    void closeDiscardsStaleCallbacksAndReopenPreservesNewBytes()
    {
        WorkerDevice::Probe     probe;
        ThreadedSerialDevice    device([&] { return std::make_unique<WorkerDevice>(probe); });
        QByteArray              received;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray& bytes) { received += bytes; };
        device.setCallbacks(callbacks);
        QVERIFY(device.open({}));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QMetaObject::invokeMethod(probe.device, [&] { probe.device->receive("stale"); }, Qt::BlockingQueuedConnection);
        device.close();
        QVERIFY(device.open({}));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QMetaObject::invokeMethod(probe.device, [&] { probe.device->receive("new"); }, Qt::BlockingQueuedConnection);
        QTRY_COMPARE(received, QByteArray("new"));
    }

    void borrowedErrorStorageIsCopiedAndBounded()
    {
        WorkerDevice::Probe     probe;
        ThreadedSerialDevice    device([&] { return std::make_unique<WorkerDevice>(probe); });
        QStringList             errors;
        SerialDevice::Callbacks callbacks;
        callbacks.error = [&](const QString& error) { errors.append(error); };
        device.setCallbacks(callbacks);
        QVERIFY(device.open({}));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QMetaObject::invokeMethod(
            probe.device,
            [&] {
                QString storage = QStringLiteral("Synthetic borrowed failure.");
                storage.detach();
                probe.device->reportError(QString::fromRawData(storage.constData(), storage.size()));
                storage.fill('x');
                probe.device->reportError(QString(5000, 'y'));
            },
            Qt::BlockingQueuedConnection);
        QTRY_COMPARE(errors.size(), 2);
        QCOMPARE(errors.at(0), QStringLiteral("Synthetic borrowed failure."));
        QCOMPARE(errors.at(1), QString(4096, 'y'));
    }

    void invalidBackendObjectRejectsBootstrap()
    {
        for (QObject* object : QList<QObject*> {nullptr, this})
        {
            WorkerDevice::Probe probe;
            probe.useObjectOverride = true;
            probe.objectOverride    = object;
            bool threw              = false;
            try
            {
                ThreadedSerialDevice device([&] { return std::make_unique<WorkerDevice>(probe); });
            }
            catch (const std::runtime_error&)
            {
                threw = true;
            }
            QVERIFY(threw);
            QCOMPARE(probe.destroyed.load(), 1);
            QCOMPARE(probe.wrongThread.load(), 0);
        }
    }

    void destructionWaitsForNativeThreadLocalTeardown()
    {
        WorkerDevice::Probe probe;
        {
            ThreadedSerialDevice device([&] { return std::make_unique<WorkerDevice>(probe, false, true); });
            QVERIFY(probe.tlsInstalled.load());
        }
        QVERIFY(probe.tlsCompleted.load());
        QCOMPARE(probe.destroyed.load(), 1);
    }

    void failedBootstrapWaitsForNativeThreadLocalTeardown()
    {
        WorkerDevice::Probe probe;
        probe.useObjectOverride = true;
        bool threw              = false;
        try
        {
            ThreadedSerialDevice device([&] { return std::make_unique<WorkerDevice>(probe, false, true); });
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        QVERIFY(threw);
        QVERIFY(probe.tlsInstalled.load());
        QVERIFY(probe.tlsCompleted.load());
        QCOMPARE(probe.destroyed.load(), 1);
    }

    void nullFactoryFailsBeforeMaterialization()
    {
        bool threw = false;
        try
        {
            ThreadedSerialDevice device([] { return std::unique_ptr<SerialDevice>(); });
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        QVERIFY(threw);
    }
};

QTEST_GUILESS_MAIN(ThreadedSerialDeviceTests)
#include "ThreadedSerialDeviceTests.moc"
