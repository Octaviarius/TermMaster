#include "Elevation/ElevationProtocol.h"

#include <QTest>

class ConsoleHostProtocolTests final : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void chunkBoundariesAndBinaryPayload()
    {
        using Type = ConsoleHostProtocol::Type;
        const QByteArray payload("a\0b\xff", 4);
        const auto       frame = ConsoleHostProtocol::frame(Type::Input, payload);
        for (qsizetype split = 0; split < frame.size(); ++split)
        {
            QByteArray buffer = frame.left(split);
            QByteArray decoded;
            Type       type = Type::Hello;
            QString    error;
            QVERIFY(!ConsoleHostProtocol::take(buffer, type, decoded, &error));
            QVERIFY(error.isEmpty());
            QCOMPARE(buffer, frame.left(split));
            buffer += frame.mid(split);
            QVERIFY(ConsoleHostProtocol::take(buffer, type, decoded, &error));
            QCOMPARE(type, Type::Input);
            QCOMPARE(decoded, payload);
            QVERIFY(buffer.isEmpty());
        }
        auto       buffer = frame + ConsoleHostProtocol::frame(Type::Close);
        QByteArray decoded;
        Type       type = Type::Hello;
        QString    error;
        QVERIFY(ConsoleHostProtocol::take(buffer, type, decoded, &error));
        QCOMPARE(decoded, payload);
        QVERIFY(ConsoleHostProtocol::take(buffer, type, decoded, &error));
        QCOMPARE(type, Type::Close);
        QVERIFY(decoded.isEmpty());
    }
    Q_SLOT void malformedLengthsAndKinds()
    {
        for (const auto& bytes :
             {QByteArray::fromHex("00000000"), QByteArray::fromHex("ffffffff"), QByteArray::fromHex("0000000100")})
        {
            auto       buffer = bytes;
            QByteArray payload;
            auto       type = ConsoleHostProtocol::Type::Hello;
            QString    error;
            QVERIFY(!ConsoleHostProtocol::take(buffer, type, payload, &error));
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(ConsoleHostProtocol::frame(ConsoleHostProtocol::Type::Input,
                                           QByteArray(ConsoleHostProtocol::MaxFrameSize, 'x'))
                    .isEmpty());
    }
};
QTEST_GUILESS_MAIN(ConsoleHostProtocolTests)
#include "ConsoleHostProtocolTests.moc"
