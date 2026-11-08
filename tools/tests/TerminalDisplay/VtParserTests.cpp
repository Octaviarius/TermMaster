#include <QTest>
#include <Termaster/Core/terminal/VtParser.h>

class VtParserTests : public QObject
{
    Q_OBJECT
private slots:
    void splitBoundaries();
    void controlsAndCancellation();
    void boundedStrings();
    void boundedParameters();
    void eightBitControls();
    void applicationCommands();
    void stringEscapeRestart();
};

void VtParserTests::splitBoundaries()
{
    const QByteArray input = QByteArray::fromHex("e4b8ad")
                           + "\x1b[?38:2::1:2:3;4m\x1b(0\x1b]7;file:///tmp\x07"
                             "\x1bP1;2$qabc\007def\x1b\\Z";
    for (qsizetype split = 0; split <= input.size(); ++split)
    {
        VtParser                    parser;
        QVector<VtParser::Sequence> sequences;
        const auto                  collect = [&](const auto& sequence) {
            if (sequence.kind == VtParser::Kind::Print && !sequences.isEmpty()
                && sequences.last().kind == VtParser::Kind::Print)
            {
                sequences.last().data += sequence.data;
            }
            else
            {
                sequences.append(sequence);
            }
        };
        parser.receive(QByteArrayView(input).first(split), collect);
        parser.receive(QByteArrayView(input).sliced(split), collect);
        QCOMPARE(sequences.size(), 6);
        QCOMPARE(sequences[0].data, QByteArray::fromHex("e4b8ad"));
        QCOMPARE(sequences[1].kind, VtParser::Kind::Csi);
        QCOMPARE(sequences[1].privateMarkers, QByteArray("?"));
        QCOMPARE(sequences[1].parameters,
                 (QVector<QVector<int>> {
                     {38, 2, -1, 1, 2, 3},
                     {4}
        }));
        QCOMPARE(sequences[2].intermediates, QByteArray("("));
        QCOMPARE(sequences[2].command, '0');
        QCOMPARE(sequences[3].data, QByteArray("7;file:///tmp"));
        QCOMPARE(sequences[4].kind, VtParser::Kind::Dcs);
        QCOMPARE(sequences[4].intermediates, QByteArray("$"));
        QCOMPARE(sequences[4].command, 'q');
        QCOMPARE(sequences[4].data,
                 QByteArray("abc\x07"
                            "def"));
        QCOMPARE(sequences[5].data, QByteArray("Z"));
        QCOMPARE(parser.incompleteSequenceBytes(), 0);
    }
}

void VtParserTests::controlsAndCancellation()
{
    VtParser                    parser;
    QVector<VtParser::Sequence> sequences;
    const auto                  collect = [&](const auto& sequence) { sequences.append(sequence); };
    parser.receive("\x1b[1\n;2H\x1b[12\x18"
                   "a\x1b]bad\x1a"
                   "b\x1b[2\x1b[3m",
                   collect);
    QCOMPARE(sequences.size(), 7);
    QCOMPARE(sequences[0].kind, VtParser::Kind::Execute);
    QCOMPARE(sequences[0].command, '\n');
    QCOMPARE(sequences[1].parameters, (QVector<QVector<int>> {{1}, {2}}));
    QCOMPARE(sequences[3].data, QByteArray("a"));
    QCOMPARE(sequences[5].data, QByteArray("b"));
    QCOMPARE(sequences.last().parameters, (QVector<QVector<int>> {{3}}));
}

void VtParserTests::boundedStrings()
{
    for (const bool dcs : {false, true})
    {
        VtParser                    parser;
        QVector<VtParser::Sequence> sequences;
        const auto                  collect = [&](const auto& sequence) { sequences.append(sequence); };
        parser.receive(dcs ? QByteArrayView("\x1bPq") : QByteArrayView("\x1b]0;"), collect);
        const QByteArray block(4096, 'x');
        for (int i = 0; i < (dcs ? 1025 : 17); ++i)
        {
            parser.receive(block, collect);
            QVERIFY(parser.incompleteSequenceBytes() <= (dcs ? 4 * 1024 * 1024 : 64 * 1024));
        }
        QVERIFY(sequences.isEmpty());
        QCOMPARE(parser.discardedSequences(), 1);
        parser.receive("\x1b", collect);
        parser.receive("\\safe", collect);
        QCOMPARE(sequences.size(), 1);
        QCOMPARE(sequences[0].data, QByteArray("safe"));
    }
}

void VtParserTests::boundedParameters()
{
    for (const auto& parameters :
         {QByteArray(300, '1'), QByteArray(33, ';'), QByteArray(17, ':'), QByteArray("2147483648")})
    {
        VtParser                    parser;
        QVector<VtParser::Sequence> sequences;
        parser.receive(QByteArray("\x1b[") + parameters + "mOK",
                       [&](const auto& sequence) { sequences.append(sequence); });
        QCOMPARE(parser.discardedSequences(), 1);
        QCOMPARE(sequences.size(), 1);
        QCOMPARE(sequences[0].data, QByteArray("OK"));
    }
    VtParser                    parser;
    QVector<VtParser::Sequence> sequences;
    parser.receive(QByteArray("\x1bP") + QByteArray(300, '1') + "qhidden\x1b\\OK",
                   [&](const auto& sequence) { sequences.append(sequence); });
    QCOMPARE(sequences.size(), 1);
    QCOMPARE(sequences[0].data, QByteArray("OK"));
    parser.reset();
    QCOMPARE(parser.discardedSequences(), 0);
}

void VtParserTests::eightBitControls()
{
    VtParser                    parser;
    QVector<VtParser::Sequence> sequences;
    const auto                  collect = [&](const auto& sequence) { sequences.append(sequence); };
    const QByteArray            bytes   = QByteArray::fromHex("9b33316d");
    parser.receive(bytes, collect);
    QCOMPARE(sequences[0].kind, VtParser::Kind::Print);
    QCOMPARE(sequences[0].data, bytes);
    sequences.clear();
    parser.setEightBitControls(true);
    parser.receive(bytes, collect);
    QCOMPARE(sequences[0].kind, VtParser::Kind::Csi);
    QCOMPARE(sequences[0].command, 'm');
    sequences.clear();
    parser.receive(QByteArray::fromHex("9d303b7469746c659c"), collect);
    QCOMPARE(sequences[0].data, QByteArray("0;title"));
}

void VtParserTests::applicationCommands()
{
    VtParser                    parser;
    QVector<VtParser::Sequence> sequences;
    const auto                  collect = [&](const auto& sequence) { sequences.append(sequence); };
    parser.receive("\x1b_Ga=T;YWJj\x1b", collect);
    QVERIFY(sequences.isEmpty());
    parser.receive("\\\x1b^private\x1b\\done", collect);
    QCOMPARE(sequences.size(), 2);
    QCOMPARE(sequences[0].kind, VtParser::Kind::Apc);
    QCOMPARE(sequences[0].data, QByteArray("Ga=T;YWJj"));
    QCOMPARE(sequences[1].data, QByteArray("done"));
}

void VtParserTests::stringEscapeRestart()
{
    for (const QByteArray prefix : {QByteArray("\x1b]0;"), QByteArray("\x1bPq"), QByteArray("\x1b_G")})
    {
        for (const QByteArray ending : {QByteArray("\x1b[H"),
                                        QByteArray("\x1b"
                                                   "c")})
        {
            const QByteArray input = prefix + "unfinished" + ending;
            for (qsizetype split = 0; split <= input.size(); ++split)
            {
                VtParser                    parser;
                QVector<VtParser::Sequence> sequences;
                const auto                  collect = [&](const auto& sequence) { sequences.append(sequence); };
                parser.receive(QByteArrayView(input).first(split), collect);
                parser.receive(QByteArrayView(input).sliced(split), collect);
                QCOMPARE(sequences.size(), 1);
                QCOMPARE(sequences[0].kind, ending.endsWith('H') ? VtParser::Kind::Csi : VtParser::Kind::Escape);
                QCOMPARE(sequences[0].command, ending.endsWith('H') ? 'H' : 'c');
                QVERIFY(sequences[0].data.isEmpty());
            }
        }
    }
}

QTEST_APPLESS_MAIN(VtParserTests)
#include "VtParserTests.moc"
