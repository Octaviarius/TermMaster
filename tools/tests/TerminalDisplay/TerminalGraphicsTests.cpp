#include <QBuffer>
#include <QElapsedTimer>
#include <QTest>
#include <Termaster/Core/terminal/TerminalGraphics.h>

class TerminalGraphicsTests : public QObject
{
    Q_OBJECT
private:
    static void feed(TerminalGraphics& graphics, const QByteArray& bytes)
    {
        VtParser parser;
        for (char byte : bytes)
        {
            parser.receive(QByteArray(1, byte),
                           [&](const VtParser::Sequence& sequence) { graphics.receive(sequence, 2, 3, false); });
        }
    }
    Q_SLOT void sixelSplitAndPalette()
    {
        TerminalGraphics graphics;
        feed(graphics, "\x1bPq#2;2;100;0;0!3~-$#3;2;0;100;0@\x1b\\");
        QCOMPARE(graphics.placements().size(), 1);
        const auto& placement = graphics.placements().first();
        QCOMPARE(placement.image.size(), QSize(3, 12));
        QCOMPARE(placement.image.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(placement.image.pixelColor(0, 6), QColor(0, 255, 0));
        QCOMPARE(placement.pixelSize, QSize(3, 12));
        QCOMPARE(placement.column, 2);
        QCOMPARE(placement.row, 3);
        feed(graphics, "\x1bPq!999999999999~\x1b\\");
        QCOMPARE(graphics.placements().size(), 1);
    }
    Q_SLOT void kittyChunkAndPlacement()
    {
        TerminalGraphics graphics;
        feed(graphics, "\x1b_Ga=T,f=24,s=2,v=1,i=7,m=1;/wAA\x1b\\");
        QVERIFY(graphics.placements().isEmpty());
        feed(graphics, "\x1b_Gm=0;AP8A\x1b\\");
        QCOMPARE(graphics.placements().size(), 1);
        QCOMPARE(graphics.placements().first().image.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(graphics.placements().first().image.pixelColor(1, 0), QColor(0, 255, 0));
        QVERIFY(graphics.takeOutput().contains("i=7;OK"));
        feed(graphics, "\x1b_Ga=p,i=7,p=2,c=3,r=4;\x1b\\");
        QCOMPARE(graphics.placements().size(), 2);
        QCOMPARE(graphics.placements().last().columns, 3);
        QVERIFY(!graphics.placements().last().pixelSize.isValid());
        feed(graphics, "\x1b_Ga=d,d=i,i=7,p=2;\x1b\\");
        QCOMPARE(graphics.placements().size(), 1);
        feed(graphics, "\x1b_Ga=d,d=I,i=7;\x1b\\");
        QVERIFY(graphics.placements().isEmpty());
        feed(graphics, "\x1b_Ga=p,i=7;\x1b\\");
        QVERIFY(graphics.takeOutput().contains("ENOENT"));
    }
    Q_SLOT void naturalAndCellImageSizes()
    {
        TerminalGraphics graphics;
        feed(graphics, "\x1b_Ga=T,f=24,s=1,v=1,i=1;AAAA\x1b\\");
        QCOMPARE(graphics.placements().first().pixelSize, QSize(1, 1));
        feed(graphics, "\x1b_Ga=p,i=1,c=4;\x1b\\");
        QCOMPARE(graphics.placements().last().columns, 4);
        QCOMPARE(graphics.placements().last().rows, 2);
        QVERIFY(!graphics.placements().last().pixelSize.isValid());
        feed(graphics, "\x1b_Ga=p,i=1,r=3;\x1b\\");
        QCOMPARE(graphics.placements().last().columns, 6);
        QCOMPARE(graphics.placements().last().rows, 3);
    }
    Q_SLOT void pngQueryAndRejections()
    {
        QImage image(2, 2, QImage::Format_ARGB32);
        image.fill(QColor(20, 30, 40));
        QByteArray png;
        QBuffer    buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        TerminalGraphics graphics;
        feed(graphics, "\x1b_Ga=q,f=100,i=9;" + png.toBase64() + "\x1b\\");
        QVERIFY(graphics.takeOutput().contains("i=9;OK"));
        QVERIFY(graphics.placements().isEmpty());
        feed(graphics, "\x1b_Ga=T,f=100,i=9;" + png.toBase64() + "\x1b\\");
        QCOMPARE(graphics.placements().first().image.pixelColor(0, 0), QColor(20, 30, 40));
        feed(graphics, "\x1b_Gt=f,i=9;L2V0Yy9wYXNzd2Q=\x1b\\");
        QVERIFY(graphics.takeOutput().contains("ENOTSUPPORTED"));
        feed(graphics, "\x1b_Ga=T,f=24,s=4096,v=4096,i=10;AAAA\x1b\\");
        QVERIFY(graphics.takeOutput().contains("EINVAL"));
        feed(graphics, "\x1b_Ga=T,f=24,s=1,v=1,i=10;@@@@\x1b\\");
        QVERIFY(graphics.takeOutput().contains("EINVAL"));
        feed(graphics, "\x1b_Gq=2,t=f;AA==\x1b\\");
        QVERIFY(graphics.takeOutput().isEmpty());
    }
    Q_SLOT void synchronousReplyHandlerPersistsClear()
    {
        TerminalGraphics    graphics;
        QVector<QByteArray> replies;
        graphics.setReplyHandler([&](const QByteArray& reply) { replies.append(reply); });
        feed(graphics, "\x1b_Ga=p,i=5;\x1b\\");
        QCOMPARE(replies.size(), 1);
        QVERIFY(replies.first().contains("i=5;ENOENT"));
        QVERIFY(graphics.takeOutput().isEmpty());
        graphics.clear();
        feed(graphics, "\x1b_Ga=p,i=6;\x1b\\");
        QCOMPARE(replies.size(), 2);
        QVERIFY(replies.last().contains("i=6;ENOENT"));
        QVERIFY(graphics.takeOutput().isEmpty());
        graphics.setReplyHandler({});
        feed(graphics, "\x1b_Ga=p,i=7;\x1b\\");
        QCOMPARE(replies.size(), 2);
        QVERIFY(graphics.takeOutput().contains("i=7;ENOENT"));
    }
    Q_SLOT void compressedImagesAndLimits()
    {
        TerminalGraphics graphics;
        const QByteArray rgb        = QByteArray::fromHex("ff000000ff00");
        const QByteArray rgba       = QByteArray::fromHex("0a141e80");
        const auto       compressed = [](const QByteArray& bytes) { return qCompress(bytes).sliced(4); };
        feed(graphics, "\x1b_Ga=T,o=z,f=24,s=2,v=1,i=1;" + compressed(rgb).toBase64() + "\x1b\\");
        QCOMPARE(graphics.placements().size(), 1);
        QCOMPARE(graphics.placements().last().image.pixelColor(1, 0), QColor(0, 255, 0));
        feed(graphics, "\x1b_Ga=T,o=z,f=32,s=1,v=1,i=2;" + compressed(rgba).toBase64() + "\x1b\\");
        QCOMPARE(graphics.placements().size(), 2);
        QCOMPARE(graphics.placements().last().image.pixelColor(0, 0), QColor(10, 20, 30, 128));
        QImage image(2, 2, QImage::Format_ARGB32);
        image.fill(QColor(30, 40, 50));
        QByteArray png;
        QBuffer    buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        feed(graphics, "\x1b_Ga=T,o=z,f=100,i=3;" + compressed(png).toBase64() + "\x1b\\");
        QCOMPARE(graphics.placements().size(), 3);
        QCOMPARE(graphics.placements().last().image.pixelColor(0, 0), QColor(30, 40, 50));
        graphics.takeOutput();
        feed(graphics,
             "\x1b_Ga=T,o=z,f=24,s=1,v=1,i=4;" + compressed(QByteArray(1024 * 1024, 'a')).toBase64() + "\x1b\\");
        QVERIFY(graphics.takeOutput().contains("EINVAL"));
        QCOMPARE(graphics.placements().size(), 3);
        const QByteArray packed = compressed(rgb);
        for (const QByteArray invalid :
             {packed.chopped(1), packed + 'x', packed.left(packed.size() - 1) + char(packed.back() ^ 1)})
        {
            feed(graphics, "\x1b_Ga=T,o=z,f=24,s=2,v=1,i=4;" + invalid.toBase64() + "\x1b\\");
            QVERIFY(graphics.takeOutput().contains("EINVAL"));
        }
        feed(graphics, "\x1b_Ga=T,o=q,f=24,s=2,v=1,i=4;" + packed.toBase64() + "\x1b\\");
        QVERIFY(graphics.takeOutput().contains("ENOTSUPPORTED"));
    }
    Q_SLOT void boundedHeadersRepliesAndSixelWork()
    {
        TerminalGraphics   graphics;
        VtParser::Sequence sequence;
        sequence.kind = VtParser::Kind::Apc;
        sequence.data = "Ga=p,p=" + QByteArray(4 * 1024 * 1024, '9') + ";";
        graphics.receive(sequence, 0, 0, false);
        const QByteArray rejected = graphics.takeOutput();
        QVERIFY(rejected.size() < 512);
        QVERIFY(rejected.contains("EINVAL"));
        sequence.data = "Ga=p,i=1,p=2147483648;";
        graphics.receive(sequence, 0, 0, false);
        QVERIFY(graphics.takeOutput().contains("EINVAL"));
        sequence.data = "Ga=p,i=1;";
        for (int i = 0; i < 5000; ++i)
        {
            graphics.receive(sequence, 0, 0, false);
        }
        const QByteArray replies = graphics.takeOutput();
        QVERIFY(replies.size() <= 65536);
        QVERIFY(replies.endsWith("\x1b\\"));
        sequence.kind          = VtParser::Kind::Dcs;
        sequence.command       = 'q';
        sequence.intermediates = "$";
        sequence.data          = "~";
        graphics.receive(sequence, 0, 0, false);
        QVERIFY(graphics.placements().isEmpty());
        sequence.intermediates.clear();
        sequence.privateMarkers = "?";
        graphics.receive(sequence, 0, 0, false);
        QVERIFY(graphics.placements().isEmpty());
        sequence.privateMarkers.clear();
        sequence.data.clear();
        for (int i = 0; i < 2000; ++i)
        {
            sequence.data += "!4096~$";
        }
        QElapsedTimer timer;
        timer.start();
        graphics.receive(sequence, 0, 0, false);
        QVERIFY(timer.elapsed() < 3000);
        QVERIFY(graphics.placements().isEmpty());
    }
    Q_SLOT void boundedStorageAndPlacements()
    {
        TerminalGraphics graphics;
        for (int id = 1; id <= 65; ++id)
        {
            feed(graphics, "\x1b_Ga=t,f=24,s=1,v=1,i=" + QByteArray::number(id) + ";AAAA\x1b\\");
        }
        QVERIFY(graphics.takeOutput().contains("ENOSPC"));
        for (int i = 0; i < 129; ++i)
        {
            feed(graphics, "\x1b_Ga=p,i=1;\x1b\\");
        }
        QCOMPARE(graphics.placements().size(), 128);
        QVERIFY(graphics.takeOutput().contains("ENOENT"));
        graphics.trimHistory(100);
        QVERIFY(graphics.placements().isEmpty());
        graphics.clear();
        feed(graphics, "\x1bPq\"1;1;4096;4096~\x1b\\");
        QVERIFY(graphics.placements().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TerminalGraphicsTests)
#include "TerminalGraphicsTests.moc"
