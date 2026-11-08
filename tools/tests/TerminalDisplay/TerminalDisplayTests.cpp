#include <QApplication>
#include <QClipboard>
#include <QSignalSpy>
#include <QTest>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/Terminal/TerminalScreen.h>

class TerminalDisplayTests : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void streamBoundaries()
    {
        const QByteArray input = QString::fromUtf8("A€😀").toUtf8() + "\x1b[31mred\x1b[0m";
        for (int split = 0; split <= input.size(); ++split)
        {
            TerminalScreen screen;
            screen.receive(input.left(split));
            screen.receive(input.mid(split));
            QCOMPARE(screen.text(), QString::fromUtf8("A€😀red"));
            QCOMPARE(screen.lines()[0].cells[4].foreground, QColor(QStringLiteral("#cd3131")));
        }
    }
    Q_SLOT void boundedAndControls()
    {
        TerminalScreen screen;
        screen.setLimits(8, 2, 3);
        screen.receive(QByteArray("abc\0def", 7));
        QCOMPARE(screen.text(), QStringLiteral("abcdef"));
        screen.receive("\rXY");
        QCOMPARE(screen.text(), QStringLiteral("XYcdef"));
        screen.receive("\x1b[2K\r\x1b[38;2;12;34;56mZ");
        QCOMPARE(screen.lines()[0].cells[0].foreground, QColor(12, 34, 56));
        screen.receive("\x1b]" + QByteArray(100000, 'a') + "\x07");
        QVERIFY(screen.text().size() < 20);
        for (int i = 0; i < 100; ++i)
        {
            screen.receive("line\n");
        }
        QVERIFY(screen.lines().size() <= 5);
        screen.clear();
        QCOMPARE(screen.text(), QString());
    }
    Q_SLOT void lineFeedCarriageReturnAndWrap()
    {
        TerminalScreen screen;
        screen.receive("abc\nX");
        QCOMPARE(screen.text(), QStringLiteral("abc\n   X"));
        screen.receive("\r\nY");
        QCOMPARE(screen.text(), QStringLiteral("abc\n   X\nY"));
        screen.clear();
        screen.setLimits(3, 3, 2);
        screen.receive("abc");
        screen.setLimits(3, 3, 2); // Geometry updates must preserve pending wrapping.
        screen.receive("d");
        QCOMPARE(screen.text(), QStringLiteral("abc\nd"));
    }
    Q_SLOT void malformedUtf8AndClearReset()
    {
        const QByteArray malformed = QByteArray::fromHex("e228a1f0288c28");
        TerminalScreen   whole;
        whole.receive(malformed);
        QVERIFY(whole.text().contains(QChar::ReplacementCharacter));
        for (int split = 0; split <= malformed.size(); ++split)
        {
            TerminalScreen chunked;
            chunked.receive(malformed.left(split));
            chunked.receive(malformed.mid(split));
            QCOMPARE(chunked.text(), whole.text());
        }
        TerminalScreen screen;
        screen.setColors(QColor("#123456"), QColor("#654321"));
        screen.receive("\x1b[31;44mred");
        screen.receive(QByteArray::fromHex("e282"));
        screen.clear();
        screen.receive("X");
        QCOMPARE(screen.text(), QStringLiteral("X"));
        QCOMPARE(screen.lines()[0].cells[0].foreground, QColor("#123456"));
        QCOMPARE(screen.lines()[0].cells[0].background, QColor("#654321"));
    }
    Q_SLOT void selectionSurvivesIncomingLinesAndClearsOnTrimOrResize()
    {
        TerminalDisplay display;
        display.resize(500, 150);
        display.setAppearance({
            {QStringLiteral("scrollbackLines"), 1}
        });
        display.show();
        QTest::qWait(20);
        display.receive("selected");
        QTest::mousePress(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(0, 5));
        QTest::mouseMove(display.viewport(), QPoint(300, 5));
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(300, 5));
        display.receive("\r\nnext");
        QTest::keyClick(&display, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("selected"));
        for (int i = 0; i < 30; ++i)
        {
            display.receive("\r\ntrim");
        }
        QApplication::clipboard()->setText("sentinel");
        QTest::keyClick(&display, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("sentinel"));
        display.clear();
        display.receive("resize");
        QTest::mousePress(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(0, 5));
        QTest::mouseMove(display.viewport(), QPoint(300, 5));
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(300, 5));
        display.resize(450, 150);
        QApplication::clipboard()->setText("resized");
        QTest::keyClick(&display, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("resized"));
    }
    Q_SLOT void repliesAndLocalEchoAreSeparate()
    {
        TerminalDisplay display;
        display.resize(800, 350);
        QSignalSpy output(&display, &TerminalDisplay::outputBytes);
        display.receive("abc\x1b[6n");
        QCOMPARE(output.size(), 1);
        QVERIFY(output[0][0].toByteArray().endsWith('R'));
        QCOMPARE(output[0][1].value<TerminalDisplay::OutputKind>(), TerminalDisplay::OutputKind::Reply);
        output.clear();
        display.localEcho("\x1b[6n");
        QVERIFY(output.isEmpty());
        QVERIFY(display.text().contains("[6n"));
    }
    Q_SLOT void responseOrderDoesNotDependOnChunks()
    {
        const QByteArray stream = "\x1b_Ga=q,f=24,s=1,v=1,i=7;AAAA\x1b\\"
                                  "\x1b[5n\x1b"
                                  "c\x1b[6n";
        QByteArray       expected;
        for (int split = 0; split <= stream.size(); ++split)
        {
            TerminalDisplay display;
            QByteArray      actual;
            connect(&display,
                    &TerminalDisplay::outputBytes,
                    &display,
                    [&actual](const QByteArray& bytes, TerminalDisplay::OutputKind kind, const QByteArray&) {
                        if (kind == TerminalDisplay::OutputKind::Reply)
                        {
                            actual += bytes;
                        }
                    });
            display.receive(stream.left(split));
            display.receive(stream.mid(split));
            if (split == 0)
            {
                expected = actual;
            }
            QCOMPARE(actual, expected);
        }
        QVERIFY(expected.startsWith("\x1b_Gi=7;OK\x1b\\"));
        QVERIFY(expected.contains("\x1b[0n"));
        QVERIFY(expected.endsWith("R"));
    }
    Q_SLOT void selectionOverridesMouseReporting()
    {
        TerminalDisplay display;
        display.resize(500, 200);
        display.show();
        display.receive("selected\x1b[?1000h\x1b[?1006h");
        QSignalSpy output(&display, &TerminalDisplay::outputBytes);
        QTest::mousePress(display.viewport(), Qt::LeftButton, Qt::ShiftModifier, QPoint(0, 5));
        QTest::mouseMove(display.viewport(), QPoint(150, 5));
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 5));
        QVERIFY(output.isEmpty());
        QVERIFY(display.hasSelection());
    }
    Q_SLOT void pasteAndEchoMetadata()
    {
        TerminalDisplay display;
        QSignalSpy      output(&display, &TerminalDisplay::outputBytes);
        display.receive("\x1b[?2004h");
        QApplication::clipboard()->setText("hello\nworld");
        display.paste();
        QCOMPARE(output.size(), 1);
        QCOMPARE(output[0][0].toByteArray(), QByteArray("\x1b[200~hello\nworld\x1b[201~"));
        QCOMPARE(output[0][2].toByteArray(), QByteArray("hello\r\nworld"));
        output.clear();
        QTest::keyClick(&display, Qt::Key_Up);
        QCOMPARE(output.size(), 1);
        QVERIFY(output[0][2].toByteArray().isEmpty());
        output.clear();
        QTest::keyClick(&display, Qt::Key_Return);
        QCOMPARE(output[0][2].toByteArray(), QByteArray("\r\n"));
    }
    Q_SLOT void graphicsAndResetRendering()
    {
        TerminalDisplay display;
        display.resize(400, 240);
        display.show();
        display.receive("\x1b_Ga=T,f=24,s=1,v=1,i=1,c=4,r=3,C=1;/wAA\x1b\\");
        QTest::qWait(20);
        auto pixel = display.viewport()->grab().toImage().pixelColor(4, 4);
        QCOMPARE(pixel, QColor(Qt::red));
        display.receive("\x1b"
                        "c");
        QTest::qWait(20);
        pixel = display.viewport()->grab().toImage().pixelColor(4, 4);
        QCOMPARE(pixel, QColor("#15191f"));
        display.receive("\x1b_Ga=T,f=24,s=1,v=1,i=2,c=4,r=3,C=1,z=-1;/wAA\x1b\\");
        QTest::qWait(20);
        pixel = display.viewport()->grab().toImage().pixelColor(4, 4);
        QCOMPARE(pixel, QColor(Qt::red));
    }
    Q_SLOT void keyboardAndAppearance()
    {
        TerminalDisplay display;
        display.resize(800, 350);
        display.setAppearance({
            {       QStringLiteral("fontSize"), 14},
            {QStringLiteral("scrollbackLines"), 50}
        });
        display.show();
        display.setFocus();
        QSignalSpy input(&display, &TerminalDisplay::outputBytes);
        QTest::keyClick(&display, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(&display, Qt::Key_Up);
        QTest::keyClick(&display, Qt::Key_Return);
        QCOMPARE(input.size(), 3);
        QCOMPARE(input[0][0].toByteArray(), QByteArray(1, '\1'));
        QCOMPARE(input[1][0].toByteArray(), QByteArray("\x1b[A"));
        QCOMPARE(input[2][0].toByteArray(), QByteArray("\r"));
        display.receive("Terminal stream\r\n\x1b[32mGreen\x1b[0m \x1b[38;2;255;160;30mRGB\x1b[0m\r\n");
        display.receive(QString::fromUtf8("Unicode: € 😀 日本語").toUtf8());
        QTest::qWait(50);
        const QString capture = qEnvironmentVariable("TERMASTER_TERMINAL_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(display.grab().save(capture));
        }
        QVERIFY(display.text().contains(QString::fromUtf8("Unicode: € 😀 日本語")));
        display.clear();
        QCOMPARE(display.text(), QString());
    }
};
QTEST_MAIN(TerminalDisplayTests)
#include "TerminalDisplayTests.moc"
