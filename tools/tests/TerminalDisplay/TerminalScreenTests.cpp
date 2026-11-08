#include <QtTest>
#include <Termaster/Core/Terminal/TerminalScreen.h>

class TerminalScreenTests : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void cursorShapeAndBlink()
    {
        TerminalScreen screen;
        QVERIFY(screen.mode(12));
        screen.receive("\x1b[2 q");
        QCOMPARE(screen.cursorShape(), 2);
        QVERIFY(!screen.mode(12));
        screen.receive("\x1b[?12h");
        QVERIFY(screen.mode(12));
        screen.receive("\x1b[5 q");
        QCOMPARE(screen.cursorShape(), 5);
        QVERIFY(screen.mode(12));
        screen.receive("\x1b[?12l");
        QVERIFY(!screen.mode(12));
    }
    Q_SLOT void chunkInvariant()
    {
        const QByteArray stream = QString::fromUtf8("A日本é😀").toUtf8() + "\x1b[38:2::12:34:56mZ\x1b[0m\r\nsecond";
        TerminalScreen   expected;
        expected.receive(stream);
        for (int split = 0; split <= stream.size(); ++split)
        {
            TerminalScreen actual;
            actual.receive(stream.first(split));
            actual.receive(stream.sliced(split));
            QCOMPARE(actual.text(), expected.text());
            QCOMPARE(actual.cursorColumn(), expected.cursorColumn());
        }
        QCOMPARE(expected.lines()[0].cells[1].width, 2);
        QCOMPARE(expected.lines()[0].cells[2].width, 0);
        QCOMPARE(expected.lines()[0].cells[8].foreground, QColor(12, 34, 56));
    }
    Q_SLOT void scrollRegionsAndHistory()
    {
        TerminalScreen screen;
        screen.setLimits(6, 4, 2);
        screen.receive("one\r\ntwo\r\nthree\r\nfour");
        screen.receive("\x1b[2;3r\x1b[3;1H\n");
        QCOMPARE(screen.lines().size(), 4);
        QVERIFY(screen.text().startsWith("one\nthree\n\nfour"));
        screen.receive("\x1b[r\x1b[4;1H\n");
        QCOMPARE(screen.lines().size(), 5);
        screen.receive("\x1b[3J");
        QCOMPARE(screen.lines().size(), 4);
    }
    Q_SLOT void alternateAndSavedState()
    {
        TerminalScreen screen;
        screen.setLimits(10, 3, 5);
        screen.receive("primary\x1b[31m\x1b[?1049hother\x1b[?1049lX");
        QCOMPARE(screen.text(), QStringLiteral("primaryX"));
        QVERIFY(!screen.alternateScreen());
        QCOMPARE(screen.lines()[0].cells[7].foreground, QColor("#cd3131"));
        screen.receive("\x1b[?2004h\x1b[?1h\x1b[?1006h\x1b[?1003h");
        QVERIFY(screen.bracketedPaste());
        QVERIFY(screen.applicationCursorKeys());
        QCOMPARE(screen.mouseTracking(), 1003);
        QCOMPARE(screen.mouseEncoding(), 1006);
    }
    Q_SLOT void editsRepairWideCells()
    {
        TerminalScreen screen;
        screen.setLimits(8, 2, 0);
        screen.receive(QString::fromUtf8("A日B").toUtf8());
        screen.receive("\x1b[1;3HX");
        QCOMPARE(screen.text(), QStringLiteral("A XB"));
        for (const auto& line : screen.lines())
        {
            for (int c = 0; c < line.cells.size(); ++c)
            {
                if (line.cells[c].width == 0)
                {
                    QVERIFY(c > 0 && line.cells[c - 1].width == 2);
                }
                if (line.cells[c].width == 2)
                {
                    QVERIFY(c + 1 < line.cells.size() && line.cells[c + 1].width == 0);
                }
            }
        }
        screen.receive("\x1b[1;1H\x1b[2@\x1b[2P");
        QCOMPARE(screen.text(), QStringLiteral("A XB"));
    }
    Q_SLOT void reportsAndHostMetadata()
    {
        TerminalScreen screen;
        screen.setLimits(20, 4, 0);
        screen.receive("\x1b[2;3H\x1b[6n\x1b[5n\x1b[18t");
        QCOMPARE(screen.takeOutput(), QByteArray("\x1b[2;3R\x1b[0n\x1b[8;4;20t"));
        screen.receive("\x1b]2;title\x07\x1b]7;file:///tmp\x1b\\\x1b]8;;https://example.test\x1b\\X");
        QCOMPARE(screen.title(), QStringLiteral("title"));
        QCOMPARE(screen.currentDirectory(), QStringLiteral("file:///tmp"));
        QCOMPARE(screen.lines()[1].cells[2].hyperlink, QStringLiteral("https://example.test"));
        screen.receive("\x1b]52;c;aGVsbG8=\x07");
        QVERIFY(screen.takeOutput().isEmpty());
    }
    Q_SLOT void localEchoDoesNotExecuteProtocol()
    {
        TerminalScreen screen;
        screen.echo("\x1b[6n\x1b]2;hostile\x07");
        QVERIFY(screen.takeOutput().isEmpty());
        QVERIFY(screen.title().isEmpty());
        QVERIFY(screen.text().contains("[6n]2;hostile"));
        screen.clear();
        const QByteArray euro = QString::fromUtf8("€").toUtf8();
        screen.echo(euro.first(1));
        screen.receive("X");
        screen.echo(euro.sliced(1));
        QCOMPARE(screen.text(), QString::fromUtf8("X€"));
    }
    Q_SLOT void protectedAndRectangularEdits()
    {
        TerminalScreen screen;
        screen.setLimits(6, 3, 0);
        screen.receive("\x1b[1\"qA\x1b[0mB\x1b[0\"qC\x1b[1;1H\x1b[?2K");
        QCOMPARE(screen.text(), QStringLiteral("AB"));
        screen.receive("\x1b[2K");
        QVERIFY(screen.text().isEmpty());
        screen.receive("\x1b[88;1;2;2;4$x");
        QCOMPARE(screen.text(), QStringLiteral(" XXX\n XXX"));
        screen.receive("\x1b[1;2;2;4;1;2;3;1$v");
        QCOMPARE(screen.lines()[2].cells[4].text, QStringLiteral("X"));
        screen.receive("\x1b[1;1;3;6$z");
        QVERIFY(screen.text().isEmpty());
        screen.advanceGraphics(2, 1);
        QCOMPARE(screen.cursorColumn(), 2);
        QCOMPARE(screen.cursorLine(), 1);
    }
    Q_SLOT void vt52AddressAndReturn()
    {
        TerminalScreen screen;
        screen.setLimits(8, 3, 0);
        screen.receive("\x1b[?2l\x1bY");
        screen.receive("!");
        screen.receive("#X\x1bZ");
        QCOMPARE(screen.lines()[1].cells[3].text, QStringLiteral("X"));
        QCOMPARE(screen.takeOutput(), QByteArray("\x1b/Z"));
        screen.receive("\x1b<\x1b[1;1HY");
        QCOMPARE(screen.lines()[0].cells[0].text, QStringLiteral("Y"));
    }
    Q_SLOT void variationSelectorRepairsAdjacentWideGlyph()
    {
        TerminalScreen screen;
        screen.setLimits(8, 2, 0);
        screen.receive(QString::fromUtf8("♥日").toUtf8());
        screen.receive("\x1b[1;2H");
        screen.receive(QString::fromUtf8("️").toUtf8());
        QCOMPARE(screen.lines()[0].cells[0].width, 2);
        QCOMPARE(screen.lines()[0].cells[1].width, 0);
        QCOMPARE(screen.lines()[0].cells[2].width, 1);
        QCOMPARE(screen.lines()[0].cells[2].text, QStringLiteral(" "));
        screen.setLimits(9, 3, 0);
        QCOMPARE(screen.lines()[0].cells[0].width, 2);
        QCOMPARE(screen.lines()[0].cells[1].width, 0);
    }
    Q_SLOT void resizeHiddenPrimaryAndTheme()
    {
        TerminalScreen screen;
        screen.setLimits(4, 4, 4);
        screen.receive("A\r\nB\r\nC\r\nD\x1b[1;1H\x1b[?1049h");
        screen.setLimits(4, 2, 1);
        screen.setColors(QColor("#123456"), QColor("#654321"));
        screen.receive("\x1b[?1049l");
        QCOMPARE(screen.text(), QStringLiteral("A\nB"));
        QCOMPARE(screen.cursorLine(), 0);
        QCOMPARE(screen.lines()[0].cells[0].foreground, QColor("#123456"));
        screen.receive("X");
        QCOMPARE(screen.lines()[0].cells[0].background, QColor("#654321"));
        for (int i = 0; i < 20; ++i)
        {
            screen.receive("\r\nL");
        }
        screen.receive("\x1b[?1049h");
        screen.setLimits(8, 2, 0);
        screen.receive("\x1b[?1049l");
        QCOMPARE(screen.lines().size(), 2);
        QVERIFY(screen.cursorLine() < 2);
    }
    Q_SLOT void idempotentGeometryPreservesMarginsAndRevisions()
    {
        TerminalScreen screen;
        screen.setLimits(6, 4, 2);
        screen.receive("A\r\nB\r\nC\r\nD\x1b[2;3r\x1b[3;1H");
        quint64 revision = screen.lines()[0].revision;
        screen.setLimits(6, 4, 2);
        QCOMPARE(screen.lines()[0].revision, revision);
        screen.receive("\n");
        screen.setLimits(6, 4, 1);
        screen.receive("\n");
        QCOMPARE(screen.lines()[0].cells[0].text, QStringLiteral("A"));
        QCOMPARE(screen.lines()[3].cells[0].text, QStringLiteral("D"));
        QCOMPARE(screen.lines().size(), 4);
    }
    Q_SLOT void namespacesAndResetGeneration()
    {
        TerminalScreen screen;
        screen.receive("AB\x1b[>2J\x1b[>4;2mC");
        QCOMPARE(screen.text(), QStringLiteral("ABC"));
        QCOMPARE(screen.lines()[0].cells[2].attributes, quint32(0));
        quint64 reset = screen.resetRevision();
        screen.receive("\x1b"
                       "c");
        QVERIFY(screen.resetRevision() > reset);
        screen.receive("\x1b[31;1m\x1bP$qm\x1b\\");
        QCOMPARE(screen.takeOutput(), QByteArray("\x1bP0$r\x1b\\"));
        screen.setLimits(3, 2, 0);
        screen.receive("\x1b#8");
        QCOMPARE(screen.text(), QStringLiteral("EEE\nEEE"));
    }
    Q_SLOT void malformedUtf8EveryBoundary()
    {
        const QList<QByteArray> fixtures = {
            QByteArray::fromHex("e228a1f0288c28"),
            QByteArray::fromHex("c080eda080f4908080f5808080"),
            QByteArray::fromHex("efbbbf41f48fbfbf"),
            QByteArray::fromHex("e282") + "\x1b[mZ",
            QByteArray::fromHex("e2") + "\x1b[31m" + QByteArray::fromHex("82ac"),
            QByteArray::fromHex("f09f") + "\rX",
        };
        for (const QByteArray& fixture : fixtures)
        {
            TerminalScreen whole;
            whole.receive(fixture);
            for (qsizetype split = 0; split <= fixture.size(); ++split)
            {
                TerminalScreen chunked;
                chunked.receive(fixture.first(split));
                chunked.receive(fixture.sliced(split));
                QCOMPARE(chunked.text(), whole.text());
            }
            TerminalScreen singleBytes;
            for (char byte : fixture)
            {
                singleBytes.receive(QByteArray(1, byte));
            }
            QCOMPARE(singleBytes.text(), whole.text());
        }
        TerminalScreen maximum;
        maximum.receive(QByteArray::fromHex("efbbbf41f48fbfbf"));
        const char32_t scalar = 0x10ffff;
        QCOMPARE(maximum.text(), QStringLiteral("A") + QString::fromUcs4(&scalar, 1));
        TerminalScreen interrupted;
        interrupted.receive(QByteArray::fromHex("e282"));
        interrupted.receive("\x1b");
        QCOMPARE(interrupted.text(), QString(QChar::ReplacementCharacter));
        interrupted.receive("[mZ");
        QCOMPARE(interrupted.text(), QString(QChar::ReplacementCharacter) + "Z");
    }
    Q_SLOT void modifyOtherKeysAndBackarrowModes()
    {
        TerminalScreen screen;
        screen.receive("\x1b[>4;2m\x1b[?67h");
        QCOMPARE(screen.modifyOtherKeys(), 2);
        QVERIFY(screen.mode(67));
        screen.receive("\x1b[>4;3m\x1b[>5;1m");
        QCOMPARE(screen.modifyOtherKeys(), 2);
        screen.receive("\x1b[>4m\x1b[?67l");
        QCOMPARE(screen.modifyOtherKeys(), 0);
        QVERIFY(!screen.mode(67));
        screen.receive("\x1b[>4;1m");
        QCOMPARE(screen.modifyOtherKeys(), 1);
        screen.clear();
        QCOMPARE(screen.modifyOtherKeys(), 0);
    }
    Q_SLOT void alternateResizeDoesNotDiscardPrimaryAnchors()
    {
        TerminalScreen screen;
        screen.setLimits(4, 4, 4);
        screen.receive("A\r\nB\r\nC\r\nD\x1b[?1049h1\r\n2\r\n3\r\n4");
        screen.setLimits(4, 2, 4);
        QCOMPARE(screen.discardedLines(), quint64(0));
        screen.setLimits(4, 2, 0);
        QCOMPARE(screen.discardedLines(), quint64(2));
        screen.receive("\x1b[?1049l");
        QCOMPARE(screen.text(), QStringLiteral("C\nD"));
    }
    Q_SLOT void mutablePaletteAndDefaultColors()
    {
        TerminalScreen screen;
        screen.setLimits(12, 2, 2);
        screen.setColors(QColor("#abcdef"), QColor("#102030"));
        screen.receive("\x1b[31mA\x1b[38;2;80;90;100mB\x1b[0mC\x1b[31m\x1b[?1049h");
        screen.receive("\x1b]4;1;#112233;2;rgb:f/0/a\x07\x1b]10;rgb:1/2/3\x07\x1b]11;#445566\x07");
        screen.receive("\x1b[?1049lD");
        QCOMPARE(screen.lines()[0].cells[0].foreground, QColor("#112233"));
        QCOMPARE(screen.lines()[0].cells[0].foregroundIndex, 1);
        QCOMPARE(screen.lines()[0].cells[1].foreground, QColor(80, 90, 100));
        QCOMPARE(screen.lines()[0].cells[1].foregroundIndex, -1);
        QCOMPARE(screen.lines()[0].cells[2].foreground, QColor("#112233"));
        QCOMPARE(screen.lines()[0].cells[3].foreground, QColor("#112233"));
        QCOMPARE(screen.lines()[0].cells[2].background, QColor("#445566"));
        screen.receive("\x1b]10;?\x07\x1b]4;2;?\x07");
        QCOMPARE(screen.takeOutput(), QByteArray("\x1b]10;rgb:1111/2222/3333\x1b\\\x1b]4;2;rgb:ffff/0000/aaaa\x1b\\"));
        quint64 revision = screen.lines()[0].revision;
        screen.receive("\x1b]4;1;red;999;#ffffff;2;rgb:gg/0/0\x07");
        QCOMPARE(screen.lines()[0].revision, revision);
        screen.receive("\x1b]104;1\x07\x1b]110\x07\x1b]111\x07");
        QCOMPARE(screen.lines()[0].cells[0].foreground, QColor("#cd3131"));
        QCOMPARE(screen.lines()[0].cells[1].foreground, QColor(80, 90, 100));
        QCOMPARE(screen.lines()[0].cells[2].foreground, QColor("#abcdef"));
        QCOMPARE(screen.lines()[0].cells[2].background, QColor("#102030"));
        screen.receive("\x1b[32mE\x1b]104\x07");
        QCOMPARE(screen.lines()[0].cells[4].foreground, QColor("#0dbc79"));
    }
    Q_SLOT void repliesSurviveRisAndPreserveCallbackOrder()
    {
        TerminalScreen queued;
        queued.receive("\x1b[5n\x1b"
                       "c"
                       "\x1b[6n");
        QCOMPARE(queued.takeOutput(), QByteArray("\x1b[0n\x1b[1;1R"));
        const QByteArray stream = "\x1b[5n\x1b]999;extension\x07\x1b"
                                  "c"
                                  "\x1b[6n";
        for (qsizetype split = 0; split <= stream.size(); ++split)
        {
            TerminalScreen screen;
            QByteArray     output;
            screen.setReplyHandler([&](const QByteArray& reply) { output += reply; });
            screen.setExtensionHandler([&](const VtParser::Sequence& sequence) {
                if (sequence.kind == VtParser::Kind::Osc && sequence.data == "999;extension")
                {
                    output += "extension";
                }
            });
            screen.receive(stream.first(split));
            screen.receive(stream.sliced(split));
            QCOMPARE(output, QByteArray("\x1b[0nextension\x1b[1;1R"));
            QVERIFY(screen.takeOutput().isEmpty());
            screen.clear();
            screen.receive("\x1b[5n");
            QVERIFY(output.endsWith("\x1b[0n"));
        }
    }
    Q_SLOT void combiningUtf16LimitIsStrict()
    {
        TerminalScreen screen;
        screen.receive((QStringLiteral("a") + QString(254, QChar(0x0301))).toUtf8());
        QCOMPARE(screen.lines()[0].cells[0].text.size(), qsizetype(255));
        const char32_t modifier = 0x1f3fb;
        screen.receive(QString::fromUcs4(&modifier, 1).toUtf8());
        QCOMPARE(screen.lines()[0].cells[0].text.size(), qsizetype(255));
        screen.receive(QString(QChar(0x0301)).toUtf8());
        QCOMPARE(screen.lines()[0].cells[0].text.size(), qsizetype(256));
        screen.receive(QString(QChar(0x0301)).toUtf8());
        QCOMPARE(screen.lines()[0].cells[0].text.size(), qsizetype(256));
    }
    Q_SLOT void boundsAndWrap()
    {
        TerminalScreen screen;
        screen.setLimits(3, 2, 2);
        screen.receive("abc");
        screen.setLimits(3, 2, 2);
        screen.receive("d");
        QCOMPARE(screen.text(), QStringLiteral("abc\nd"));
        for (int i = 0; i < 100; ++i)
        {
            screen.receive("\r\nabc");
        }
        QVERIFY(screen.lines().size() <= 4);
        QVERIFY(screen.discardedLines() > 0);
        screen.receive("\x1b[99999999;99999999H\x1b[99999999P");
        QVERIFY(screen.cursorColumn() < 3);
        QVERIFY(screen.cursorLine() < screen.lines().size());
    }
};
QTEST_GUILESS_MAIN(TerminalScreenTests)
#include "TerminalScreenTests.moc"
