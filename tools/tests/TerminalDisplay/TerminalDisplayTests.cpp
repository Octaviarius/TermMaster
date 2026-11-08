#include <QApplication>
#include <QClipboard>
#include <QSignalSpy>
#include <QTest>
#include <Termaster/Core/gui/terminal/TerminalDisplay.h>
#include <Termaster/Core/terminal/TerminalScreen.h>

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
            QCOMPARE(screen.lines()[0].cells[3].foreground, QColor(QStringLiteral("#cd3131")));
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
        QSignalSpy input(&display, &TerminalDisplay::inputBytes);
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
