#include "SocketDevice.h"

#include <QNetworkDatagram>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUdpSocket>
#include <memory>

class SocketDeviceTests : public QObject
{
    Q_OBJECT
private slots:
    void validation()
    {
        QVariantMap map {
            {"host", "localhost"},
            {"port",       12345}
        };
        QVERIFY(SocketDevice::validate(map));
        for (const auto& host : {"127.0.0.1", "::1", "terminal.example"})
        {
            map["host"] = host;
            QVERIFY(SocketDevice::validate(map));
        }
        for (const auto& host : {"", "tcp://localhost", "host:90", " localhost", "a b"})
        {
            map["host"] = host;
            QVERIFY(!SocketDevice::validate(map));
        }
        map["host"] = "localhost";
        for (const auto& value : {QVariant(0), QVariant(65536), QVariant("80"), QVariant(true), QVariant(80.0)})
        {
            map["port"] = value;
            QVERIFY(!SocketDevice::validate(map));
        }
        map["port"]        = 80;
        map["bindAddress"] = "localhost";
        QVERIFY(!SocketDevice::validate(map));
        map.remove("bindAddress");
        map["keepAlive"] = "true";
        QVERIFY(!SocketDevice::validate(map));
        map.remove("keepAlive");
        map["connectTimeoutMs"] = 99;
        QVERIFY(!SocketDevice::validate(map));
    }
    void tcpBytesCloseAndReopen()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        SocketDevice            device;
        QByteArray              received;
        qint64                  completed = 0;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray& bytes) { received += bytes; };
        callbacks.written  = [&](qint64 count) { completed += count; };
        device.setCallbacks(callbacks);
        const QVariantMap map {
            {"host",         "127.0.0.1"},
            {"port", server.serverPort()}
        };
        QVERIFY(device.open(map));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
        QByteArray                  bytes(300000, 'x');
        bytes[65535] = '\0';
        bytes[65536] = char(0xff);
        QCOMPARE(peer->write(bytes), qint64(bytes.size()));
        QTRY_COMPARE(received, bytes);
        QCOMPARE(device.write(bytes), qint64(bytes.size()));
        QByteArray output;
        QTRY_VERIFY(([&] {
            output += peer->readAll();
            return output.size() == bytes.size();
        })());
        QCOMPARE(output, bytes);
        QTRY_COMPARE(completed, qint64(bytes.size()));
        QCOMPARE(device.write(QByteArray(1024 * 1024 + 1, 'x')), qint64(-1));
        peer->write("tail", 4);
        peer->disconnectFromHost();
        QTRY_COMPARE(device.state(), SerialDevice::State::Closed);
        QCOMPARE(received, bytes + "tail");
        QVERIFY(device.open(map));
        device.close();
        QCOMPARE(device.state(), SerialDevice::State::Closed);
        QVERIFY(device.open(map));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        device.close();
        QTest::qWait(30);
        QCOMPARE(device.state(), SerialDevice::State::Closed);
        QCOMPARE(device.pendingBytes(), qint64(0));
    }
    void udpDatagrams()
    {
        QUdpSocket peer;
        QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        SocketDevice            device;
        QList<QByteArray>       received;
        SerialDevice::Callbacks callbacks;
        callbacks.received = [&](const QByteArray& bytes) { received.append(bytes); };
        device.setCallbacks(callbacks);
        QVERIFY(device.open({
            {   "protocol",            "UDP"},
            {       "host",      "127.0.0.1"},
            {       "port", peer.localPort()},
            {"bindAddress",      "127.0.0.1"}
        }));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        const QByteArray bytes("a\0b", 3);
        QCOMPARE(device.write(bytes), qint64(3));
        QTRY_VERIFY(peer.hasPendingDatagrams());
        const auto datagram = peer.receiveDatagram();
        QCOMPARE(datagram.data(), bytes);
        QCOMPARE(device.write({}), qint64(0));
        QTRY_VERIFY(peer.hasPendingDatagrams());
        QCOMPARE(peer.receiveDatagram().data(), QByteArray());
        QCOMPARE(device.write(QByteArray(65508, 'x')), qint64(-1));
        QCOMPARE(peer.writeDatagram(bytes, datagram.senderAddress(), datagram.senderPort()), qint64(3));
        QCOMPARE(peer.writeDatagram(QByteArray(), datagram.senderAddress(), datagram.senderPort()), qint64(0));
        QTRY_COMPARE(received.size(), 2);
        QCOMPARE(received[0], bytes);
        QCOMPARE(received[1], QByteArray());
        device.close();
        peer.writeDatagram("late", datagram.senderAddress(), datagram.senderPort());
        QTest::qWait(30);
        QCOMPARE(received.size(), 2);
    }
    void reopenFromWriteCallback()
    {
        QUdpSocket first;
        QUdpSocket second;
        QVERIFY(first.bind(QHostAddress::LocalHost, 0));
        QVERIFY(second.bind(QHostAddress::LocalHost, 0));
        SocketDevice device;
        QVariantMap  configuration {
            {"protocol",                  "UDP"},
            {    "host",            "127.0.0.1"},
            {    "port", int(first.localPort())}
        };
        bool                    reopened = false;
        SerialDevice::Callbacks callbacks;
        callbacks.written = [&](qint64) {
            if (reopened)
            {
                return;
            }
            reopened = true;
            configuration.insert("port", int(second.localPort()));
            QVERIFY(device.open(configuration));
            QCOMPARE(device.state(), SerialDevice::State::Open);
            QCOMPARE(device.write("new"), 3);
        };
        device.setCallbacks(callbacks);
        QVERIFY(device.open(configuration));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        device.write("old");
        QTRY_VERIFY(reopened);
        QTRY_VERIFY(second.hasPendingDatagrams());
        QCOMPARE(second.receiveDatagram().data(), QByteArray("new"));
        QTRY_COMPARE(device.pendingBytes(), qint64(0));
        device.close();
    }
    void ipv6Loopback()
    {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHostIPv6))
        {
            QSKIP("IPv6 loopback is unavailable on this host");
        }
        SocketDevice      device;
        const QVariantMap tcp {
            {   "protocol",                    "TCP"},
            {       "host",                    "::1"},
            {       "port", int(server.serverPort())},
            {"bindAddress",                    "::1"}
        };
        QString error;
        QVERIFY2(device.open(tcp, &error), qPrintable(error));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
        QCOMPARE(peer->peerAddress(), QHostAddress::LocalHostIPv6);
        QCOMPARE(device.write("ipv6"), 4);
        QTRY_VERIFY(peer->bytesAvailable() >= 4);
        QCOMPARE(peer->readAll(), QByteArray("ipv6"));
        device.close();
        QUdpSocket udp;
        QVERIFY(udp.bind(QHostAddress::LocalHostIPv6, 0));
        auto configuration = tcp;
        configuration.insert("protocol", "UDP");
        configuration.insert("port", int(udp.localPort()));
        QVERIFY2(device.open(configuration, &error), qPrintable(error));
        QTRY_COMPARE(device.state(), SerialDevice::State::Open);
        QCOMPARE(device.write("ipv6"), 4);
        QTRY_VERIFY(udp.hasPendingDatagrams());
        QCOMPARE(udp.receiveDatagram().data(), QByteArray("ipv6"));
        device.close();
    }
    void failureAndCallbackCancellation()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const auto port = server.serverPort();
        server.close();
        SocketDevice            device;
        QString                 error;
        SerialDevice::Callbacks callbacks;
        callbacks.error = [&](const QString& text) { error = text; };
        device.setCallbacks(callbacks);
        device.open({
            {"host", "127.0.0.1"},
            {"port",        port}
        });
        QTRY_COMPARE(device.state(), SerialDevice::State::Failed);
        QVERIFY(!error.isEmpty());
        callbacks.stateChanged = [&](SerialDevice::State state) {
            if (state == SerialDevice::State::Opening)
            {
                device.close();
            }
        };
        device.setCallbacks(callbacks);
        QVERIFY(!device.open({
            {"host", "127.0.0.1"},
            {"port",        port}
        }));
        QCOMPARE(device.state(), SerialDevice::State::Closed);
        QTest::qWait(30);
        QCOMPARE(device.state(), SerialDevice::State::Closed);
    }
};

QTEST_GUILESS_MAIN(SocketDeviceTests)
#include "SocketDeviceTests.moc"
