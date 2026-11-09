#include "Gui/Serial/PortSelector.h"
#include "SerialByteDevice.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QElapsedTimer>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <atomic>
#include <thread>

class FakeTransport final : public QIODevice
{
public:
    qint64     allowance = 0;
    QByteArray output;
    QByteArray input;
    bool       failWrites = false;
    bool       open(OpenMode mode) override
    {
        setOpenMode(mode);
        return true;
    }
    qint64 bytesAvailable() const override
    {
        return input.size() + QIODevice::bytesAvailable();
    }
    void receive(const QByteArray& bytes)
    {
        input.append(bytes);
        emit readyRead();
    }
    void writable(qint64 count)
    {
        allowance = count;
        emit bytesWritten(0);
    }

protected:
    qint64 readData(char* data, qint64 size) override
    {
        const auto count = qMin(size, static_cast<qint64>(input.size()));
        memcpy(data, input.constData(), static_cast<size_t>(count));
        input.remove(0, count);
        return count;
    }
    qint64 writeData(const char* data, qint64 size) override
    {
        if (failWrites)
        {
            setErrorString("Synthetic write failure");
            return -1;
        }
        const auto count = qMin(size, allowance);
        output.append(data, count);
        allowance -= count;
        return count;
    }
};
class SerialDeviceTests final : public QObject
{
    Q_OBJECT
private slots:
    void validation()
    {
        QString error;
        QVERIFY(!SerialByteDevice::validate({}, &error));
        const QVariantMap valid {
            {"portName", "synthetic"}
        };
        QVERIFY(SerialByteDevice::validate(valid, &error));
        for (const auto& item : QList<QPair<QString, QVariant>> {
                 {   "baudRate", "115200"},
                 {   "baudRate",       -1},
                 {   "dataBits",        9},
                 {     "parity",    "Bad"},
                 {"flowControl",    "Bad"},
                 {   "stopBits",        1},
                 {        "rts",  "false"}
        })
        {
            auto invalid = valid;
            invalid.insert(item.first, item.second);
            QVERIFY(!SerialByteDevice::validate(invalid, &error));
        }
    }
    void partialWritesAndBounds()
    {
        auto             fake      = std::make_unique<FakeTransport>();
        auto*            transport = fake.get();
        SerialByteDevice device(std::move(fake));
        QVERIFY(device.open({
            {"portName", "synthetic"}
        }));
        transport->allowance = 2;
        const QByteArray bytes("a\0bcd", 5);
        QCOMPARE(device.write(bytes), 5);
        QCOMPARE(transport->output, bytes.left(2));
        QCOMPARE(device.pendingBytes(), 3);
        QCOMPARE(device.write(QByteArray(1024 * 1024, 'x')), 1024 * 1024 - 3);
        QCOMPARE(device.pendingBytes(), 1024 * 1024);
        QCOMPARE(device.write("blocked"), 0);
        transport->writable(3);
        QCOMPARE(transport->output, bytes);
        QCOMPARE(device.pendingBytes(), 1024 * 1024 - 3);
        device.close();
        QCOMPARE(device.pendingBytes(), 0);
        QVERIFY(!transport->isOpen());
        QVERIFY(device.open({
            {"portName", "synthetic"}
        }));
        transport->writable(20);
        QCOMPARE(transport->output, bytes);
    }
    void bytesAndErrors()
    {
        auto             fake      = std::make_unique<FakeTransport>();
        auto*            transport = fake.get();
        SerialByteDevice device(std::move(fake));
        QByteArray       received;
        QString          error;
        device.setCallbacks({[&received](const QByteArray& bytes) { received += bytes; },
                             {},
                             {},
                             [&error](const QString& text) { error = text; }});
        QVERIFY(device.open({
            {"portName", "synthetic"}
        }));
        const QByteArray bytes("\0\xff\xc3", 3);
        transport->receive(bytes.left(1));
        transport->receive(bytes.mid(1));
        QCOMPARE(received, bytes);
        transport->failWrites = true;
        QCOMPARE(device.write("test"), -1);
        QCOMPARE(device.state(), SerialDevice::State::Failed);
        QVERIFY(!transport->isOpen());
        QCOMPARE(error, "Synthetic write failure");
        transport->failWrites = false;
        QVERIFY(device.open({
            {"portName", "synthetic"}
        }));
        device.transportError("Device removed");
        QCOMPARE(device.state(), SerialDevice::State::Failed);
        QCOMPARE(error, "Device removed");
        QVERIFY(!transport->isOpen());
    }
    void synchronousOpenErrorCannotReopen()
    {
        SerialByteDevice* pointer   = nullptr;
        auto              fake      = std::make_unique<FakeTransport>();
        auto*             transport = fake.get();
        SerialByteDevice  device(std::move(fake), [&pointer](QIODevice&, const QVariantMap&, QString*) {
            pointer->transportError("Synchronous configuration error");
            return true;
        });
        pointer = &device;
        QString error;
        QVERIFY(!device.open(
            {
                {"portName", "synthetic"}
        },
            &error));
        QCOMPARE(device.state(), SerialDevice::State::Failed);
        QCOMPARE(error, "Synchronous configuration error");
        QVERIFY(!transport->isOpen());
    }
    void backgroundInventoryIsResponsiveAndDrained()
    {
        QSemaphore        gate;
        std::atomic<int>  calls     = 0;
        std::atomic<bool> completed = false;
        auto              selector  = std::make_unique<PortSelector>(QVariantMap {}, nullptr, [&] {
            ++calls;
            gate.tryAcquire(1, 2000);
            completed = true;
            return QList<PortModel::Port> {};
        });
        QTRY_COMPARE(calls.load(), 1);
        bool responsive = false;
        QTimer::singleShot(0, [&responsive] { responsive = true; });
        QTRY_VERIFY(responsive);
        for (int i = 0; i < 10; ++i)
        {
            selector->refresh();
        }
        QCOMPARE(calls.load(), 1);
        QVERIFY(!completed.load());
        std::thread   releaser([&gate] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            gate.release();
        });
        QElapsedTimer elapsed;
        elapsed.start();
        selector.reset();
        releaser.join();
        QVERIFY(completed.load());
        QVERIFY(elapsed.elapsed() >= 50);
        QCOMPARE(calls.load(), 1);
    }
    void richSelectionKeepsRawAddress()
    {
        PortSelector selector({}, nullptr, [] {
            return QList<PortModel::Port> {
                {"COM7", "synthetic", "Readable USB device", "Vendor", "SERIAL-7", {}},
                {"COM8", "synthetic",                    {},       {},         {}, {}}
            };
        });
        auto*        combo = selector.findChild<QComboBox*>();
        QTRY_COMPARE(combo->count(), 2);
        int changes = 0;
        selector.setChangedCallback([&changes] { ++changes; });
        combo->setCurrentIndex(combo->findData("COM7", Qt::EditRole));
        QCOMPARE(selector.value().toString(), "COM7");
        QCOMPARE(combo->lineEdit()->text(), "COM7");
        QCOMPARE(changes, 1);
        QCOMPARE(combo->currentData(Qt::DisplayRole).toString(), QString::fromUtf8("COM7 — Readable USB device"));
        QCOMPARE(combo->currentData(PortModel::SerialRole).toString(), "Serial: SERIAL-7");
        selector.setValue("COM8");
        QCOMPARE(combo->currentData(Qt::DisplayRole).toString(), "COM8");
        QCOMPARE(combo->currentData(PortModel::SerialRole).toString(), QString());
        selector.setValue("unavailable-port");
        QCOMPARE(selector.value().toString(), "unavailable-port");
        QVERIFY(combo->currentData(Qt::DisplayRole).toString().contains("Unavailable"));
        QCOMPARE(changes, 1);
        combo->setEditText("arbitrary manual address");
        QCOMPARE(selector.value().toString(), "arbitrary manual address");
        QCOMPARE(changes, 2);
        selector.resize(600, selector.sizeHint().height());
        selector.show();
        selector.clearFocus();
        const QString capture = qEnvironmentVariable("TERMASTER_SERIAL_SELECTOR_CAPTURE");
        if (!capture.isEmpty())
        {
            selector.setValue("COM7");
            combo->clearFocus();
            combo->lineEdit()->clearFocus();
            QTest::qWait(50);
            QVERIFY(selector.grab().save(capture));
            combo->showPopup();
            QTest::qWait(50);
            QVERIFY(combo->view()->window()->grab().save(capture + ".popup.png"));
            QPixmap overview(1280, 800);
            overview.fill(selector.palette().color(QPalette::Window));
            QPainter painter(&overview);
            painter.drawPixmap(24, 24, selector.grab());
            painter.drawPixmap(24, 300, combo->view()->window()->grab());
            painter.end();
            QVERIFY(overview.save(capture + ".overview.png"));
            combo->hidePopup();
        }
    }
    void stablePortInventory()
    {
        PortModel              model;
        QList<PortModel::Port> ports {
            {"COM10", "location10", "USB", "vendor", "serial", "VID 0000"},
            { "COM2",  "location2", "PCI",       {},       {},         {}}
        };
        model.setPorts(ports, {});
        QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QString::fromUtf8("COM2 — PCI"));
        QCOMPARE(model.data(model.index(1), Qt::DisplayRole).toString(), QString::fromUtf8("COM10 — USB"));
        QCOMPARE(model.data(model.index(1), Qt::EditRole).toString(), "COM10");
        QCOMPARE(model.data(model.index(1), PortModel::SerialRole).toString(), "Serial: serial");
        QCOMPARE(model.data(model.index(0), PortModel::SerialRole).toString(), QString());
        QVERIFY(!qvariant_cast<QIcon>(model.data(model.index(1), Qt::DecorationRole)).isNull());
        QMutex       mutex;
        PortSelector selector({}, nullptr, [&ports, &mutex] {
            QMutexLocker lock(&mutex);
            return ports;
        });
        int          changes = 0;
        selector.setChangedCallback([&changes] { ++changes; });
        QCOMPARE(selector.value().toString(), QString());
        selector.setValue("COM10");
        QTRY_VERIFY(selector.findChild<QLabel*>("serialPortDetails")->text().contains("VID 0000"));
        auto*      combo = selector.findChild<QComboBox*>();
        QSignalSpy resets(combo->model(), &QAbstractItemModel::modelReset);
        selector.refresh();
        QTest::qWait(100);
        QCOMPARE(resets.count(), 0);
        {
            QMutexLocker lock(&mutex);
            ports.clear();
        }
        // Background polling detects removal without a manual refresh.
        QTRY_VERIFY(selector.findChild<QLabel*>("serialPortDetails")->text().contains("Unavailable"));
        QCOMPARE(selector.value().toString(), "COM10");
        {
            QMutexLocker lock(&mutex);
            ports.append({"COM10", "returned-location", "returned", {}, {}, {}});
        }
        QTRY_VERIFY(selector.findChild<QLabel*>("serialPortDetails")->text().contains("returned-location"));
        QCOMPARE(selector.value().toString(), "COM10");
        combo->setEditText("typed-port");
        combo->lineEdit()->setSelection(2, 4);
        QCOMPARE(changes, 1);
        selector.refresh();
        QTest::qWait(100);
        QCOMPARE(selector.value().toString(), "typed-port");
        QCOMPARE(combo->lineEdit()->selectionStart(), 2);
        QCOMPARE(combo->lineEdit()->selectedText(), QString("ped-"));
        QCOMPARE(changes, 1);
        {
            QMutexLocker lock(&mutex);
            ports.append({"typed-port", "typed-location", "typed-present", {}, {}, {}});
        }
        QTRY_VERIFY(selector.findChild<QLabel*>("serialPortDetails")->text().contains("typed-location"));
        QCOMPARE(selector.value().toString(), "typed-port");
        QCOMPARE(changes, 1);
        {
            QMutexLocker lock(&mutex);
            ports.clear();
        }
        QTRY_VERIFY(selector.findChild<QLabel*>("serialPortDetails")->text().contains("Unavailable"));
        QCOMPARE(selector.value().toString(), "typed-port");
        QCOMPARE(changes, 1);
    }
};
QTEST_MAIN(SerialDeviceTests)
#include "SerialDeviceTests.moc"
