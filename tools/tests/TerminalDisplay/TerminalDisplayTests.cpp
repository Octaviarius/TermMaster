#include <QApplication>
#include <QClipboard>
#include <QInputMethodQueryEvent>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/Terminal/TerminalScreen.h>
#include <cmath>

class TerminalDisplayTests : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void committedLineCallbackUsesPlainSourceRowsAndDoesNotReplayHistory()
    {
        TerminalDisplay display;
        display.resize(400, 150);
        display.show();
        QCoreApplication::processEvents();
        const int rows = display.terminalSize().height();
        display.receive(QByteArray("old\r\n").repeated(rows + 2));
        QVERIFY(display.historyLineCount() > 0);
        QStringList committed;
        QList<bool> wrapped;
        display.setCommittedLineHandler([&](const QString& text, bool softWrapped) {
            committed.append(text);
            wrapped.append(softWrapped);
        });
        QVERIFY(committed.isEmpty());
        display.receive("\x1b[2J\x1b[H\x1b[31m" + QString::fromUtf8("é😀").toUtf8() + "\x1b[0m");
        display.receive(QByteArray("\r\n").repeated(rows));
        QVERIFY(!committed.isEmpty());
        QCOMPARE(committed.first().trimmed(), QString::fromUtf8("é😀"));
        QVERIFY(!committed.first().contains(QChar(0x1b)));
        QVERIFY(!wrapped.first());
        const int count = committed.size();
        display.setCommittedLineHandler({});
        display.receive(QByteArray("more\r\n").repeated(rows + 1));
        QCOMPARE(committed.size(), count);
    }

    Q_SLOT void visualWrapPreservesSourceSelectionAndMapsCursor()
    {
        TerminalDisplay display;
        display.setAppearance({
            {"wrapLines", false}
        });
        display.resize(803, 353);
        display.show();
        const QByteArray content = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnop";
        display.receive(content);
        const QString original = display.text();
        display.selectAll();
        const qreal cell = QFontMetricsF(display.font(), display.viewport()).horizontalAdvance(QLatin1Char('M'));
        display.resize(qCeil(cell * 12), 353);
        QCoreApplication::processEvents();
        QVERIFY(display.horizontalScrollBar()->maximum() > 0);
        display.copy();
        QCOMPARE(QApplication::clipboard()->text().trimmed(), QString::fromLatin1(content));
        QCOMPARE(display.text(), original);

        display.setAppearance({
            {"wrapLines", true}
        });
        QCoreApplication::processEvents();
        QCOMPARE(display.horizontalScrollBar()->maximum(), 0);
        QCOMPARE(display.text(), original);
        QVERIFY(display.hasSelection());
        display.selectAll();
        display.copy();
        QCOMPARE(QApplication::clipboard()->text().trimmed(), QString::fromLatin1(content));
        QInputMethodQueryEvent query(Qt::ImCursorRectangle);
        QApplication::sendEvent(&display, &query);
        const QRectF cursor = query.value(Qt::ImCursorRectangle).toRectF();
        // The VT model clamps its live cursor column on shrink. Its mapped
        // presentation must stay addressable with either native or fallback fonts.
        QVERIFY(cursor.top() >= 0);
        QVERIFY(cursor.bottom() <= display.viewport()->height());
        QVERIFY(cursor.left() >= 0);
        QVERIFY(cursor.left() < display.viewport()->width());

        display.resize(803, 353);
        QCoreApplication::processEvents();
        QCOMPARE(display.text(), original);
        display.copy();
        QCOMPARE(QApplication::clipboard()->text().trimmed(), QString::fromLatin1(content));
        QInputMethodQueryEvent wideQuery(Qt::ImCursorRectangle);
        QApplication::sendEvent(&display, &wideQuery);
        const QRectF wideCursor = wideQuery.value(Qt::ImCursorRectangle).toRectF();
        QVERIFY(wideCursor.top() <= cursor.top());
        QVERIFY(wideCursor.top() >= 0);
        QVERIFY(wideCursor.top() < display.viewport()->height());
    }

    Q_SLOT void selectingVisualContinuationCopiesSourceCells()
    {
        TerminalDisplay display;
        display.resize(803, 353);
        display.show();
        display.receive("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        const qreal cell   = QFontMetricsF(display.font(), display.viewport()).horizontalAdvance(QLatin1Char('M'));
        const int   height = qCeil(QFontMetricsF(display.font(), display.viewport()).height());
        display.resize(qCeil(cell * 10), 353);
        QCoreApplication::processEvents();
        const int columns = display.terminalSize().width();
        QTest::mousePress(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(0, height + 2));
        QTest::mouseMove(display.viewport(), QPoint(qCeil(cell * 3), height + 2));
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(qCeil(cell * 3), height + 2));
        display.copy();
        QCOMPARE(QApplication::clipboard()->text(),
                 QStringLiteral("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ").mid(columns, 3));
    }

    Q_SLOT void noWrapJoinsSoftRowsAndMapsHorizontalSelection()
    {
        TerminalDisplay display;
        const qreal     cell = QFontMetricsF(display.font(), display.viewport()).horizontalAdvance(QLatin1Char('M'));
        display.resize(qCeil(cell * 14), 353);
        display.show();
        display.clearFocus();
        const int        columns = display.terminalSize().width();
        const QByteArray content = QByteArray("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
                                       .left(columns * 3 + 5);
        display.receive(content);
        QVERIFY(display.screen().lines()[0].softWrapped);
        const auto capture = qEnvironmentVariable("TERMASTER_WRAP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(display.viewport()->grab().save(capture + "-wrapped.png"));
        }

        display.setAppearance({
            {"wrapLines", false}
        });
        QCoreApplication::processEvents();
        QVERIFY(display.horizontalScrollBar()->maximum() >= columns * 2);
        display.selectAll();
        display.copy();
        QCOMPARE(QApplication::clipboard()->text().trimmed(), QString::fromLatin1(content));
        QInputMethodQueryEvent query(Qt::ImCursorRectangle);
        QApplication::sendEvent(&display, &query);
        QVERIFY(query.value(Qt::ImCursorRectangle).toRectF().top() < qCeil(QFontMetricsF(display.font()).height()));
        display.horizontalScrollBar()->setValue(columns);
        QTest::mousePress(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(0, 2));
        QTest::mouseMove(display.viewport(), QPoint(qCeil(cell * 3), 2));
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(qCeil(cell * 3), 2));
        display.copy();
        QCOMPARE(QApplication::clipboard()->text(), QString::fromLatin1(content.mid(columns, 3)));
        if (!capture.isEmpty())
        {
            QVERIFY(display.viewport()->grab().save(capture + "-horizontal.png"));
        }
        display.setAppearance({
            {"wrapLines", true}
        });
        QCoreApplication::processEvents();
        QCOMPARE(display.horizontalScrollBar()->maximum(), 0);
        QVERIFY(display.hasSelection());
        if (!capture.isEmpty())
        {
            QVERIFY(display.viewport()->grab().save(capture + "-restored.png"));
        }
    }

    Q_SLOT void incomingBytesRetainImmutableVisualPrefix()
    {
        TerminalDisplay display;
        display.resize(803, 253);
        display.show();
        display.clearFocus();
        display.receive("\x1b[?25l");
        for (int line = 0; line < 500; ++line)
        {
            display.receive(QByteArray::number(line) + " history\r\n");
        }
        QVERIFY(display.screen().lines().size() > 500);
        display.verticalScrollBar()->setValue(0);
        const auto history = display.viewport()->grab().toImage();
        display.receive("x");
        QCOMPARE(display.viewport()->grab().toImage(), history);
        display.setAppearance({
            {"wrapLines", false}
        });
        display.receive("y");
        QCOMPARE(display.viewport()->grab().toImage(), history);
        display.resize(603, 253);
        QCoreApplication::processEvents();
        const auto resized = display.viewport()->grab().toImage();
        const int  width   = std::min(history.width(), resized.width());
        const int  height  = std::min(history.height(), resized.height());
        QCOMPARE(resized.copy(0, 0, width, height), history.copy(0, 0, width, height));
        display.setAppearance({
            {      "wrapLines", false},
            {"scrollbackLines",     1}
        });
        QVERIFY(!display.text().startsWith("0 history"));
        display.receive(QByteArray("trimmed\r\n").repeated(30));
        QVERIFY(display.text().contains("trimmed"));
        display.clear();
        display.receive("reset");
        QCOMPARE(display.text(), QStringLiteral("reset"));
        QVERIFY(display.viewport()->grab().toImage() != resized);
    }

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
    Q_SLOT void selectionSurvivesIncomingLinesAndResizeAndClearsOnTrim()
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
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("resize"));
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
    Q_SLOT void mouseReportsBlankCellsWideContinuationsAndExactPixels()
    {
        TerminalDisplay display;
        display.resize(500, 200);
        display.show();
        display.receive(QString::fromUtf8("日").toUtf8() + "\x1b[?1000h\x1b[?1006h");
        const qreal cell = QFontMetricsF(display.font(), display.viewport()).horizontalAdvance(QLatin1Char('M'));
        QSignalSpy  output(&display, &TerminalDisplay::outputBytes);
        QTest::mousePress(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(qRound(cell * 1.5), 5));
        QCOMPARE(output.first().at(0).toByteArray(), QByteArray("\x1b[<0;2;1M"));
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier);
        output.clear();
        QTest::mousePress(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(qRound(cell * 7.3), 5));
        QCOMPARE(output.first().at(0).toByteArray(), QByteArray("\x1b[<0;8;1M"));
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier);
        output.clear();
        display.receive("\x1b[?1006l\x1b[?1016h");
        QTest::mousePress(display.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(73, 5));
        QCOMPARE(output.first().at(0).toByteArray(), QByteArray("\x1b[<0;74;6M"));
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier);
    }

    Q_SLOT void doubleClickSelectsUnicodeWords_data()
    {
        QTest::addColumn<int>("column");
        QTest::addColumn<QString>("expected");
        QTest::newRow("ascii") << 2 << QStringLiteral("alpha");
        QTest::newRow("accent") << 7 << QString::fromUtf8("café");
        QTest::newRow("combining") << 11 << QString::fromUtf8("élan");
        QTest::newRow("apostrophe") << 19 << QStringLiteral("can't");
        QTest::newRow("underscore") << 25 << QStringLiteral("foo_bar");
        QTest::newRow("hyphen") << 31 << QStringLiteral("-");
        QTest::newRow("punctuation") << 34 << QStringLiteral("!");
        QTest::newRow("wide-leading-cell") << 36 << QString::fromUtf8("😀");
        QTest::newRow("wide-continuation-cell") << 37 << QString::fromUtf8("😀");
    }

    Q_SLOT void doubleClickSelectsUnicodeWords()
    {
        QFETCH(int, column);
        QFETCH(QString, expected);
        TerminalDisplay display;
        display.resize(1603, 353);
        display.show();
        display.receive(QString::fromUtf8("alpha café élan can't foo_bar x-y ! 😀 Z").toUtf8());
        const qreal  cell = QFontMetricsF(display.font(), display.viewport()).horizontalAdvance(QLatin1Char('M'));
        const QPoint point(qFloor((column + 0.25) * cell), 2);
        QTest::mouseDClick(display.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(display.hasSelection());
        display.copy();
        QCOMPARE(QApplication::clipboard()->text(), expected);
        const auto capture = qEnvironmentVariable("TERMASTER_WORD_SELECTION_CAPTURE");
        if (!capture.isEmpty() && (column == 11 || column == 37))
        {
            QVERIFY(
                display.viewport()->grab().save(capture + '-' + QString::fromLatin1(QTest::currentDataTag()) + ".png"));
        }
    }

    Q_SLOT void doubleClickWhitespaceClearsSelection()
    {
        TerminalDisplay display;
        display.resize(803, 353);
        display.show();
        display.receive("word   next");
        display.selectAll();
        const qreal  cell = QFontMetricsF(display.font(), display.viewport()).horizontalAdvance(QLatin1Char('M'));
        const QPoint point(qFloor(5.25 * cell), 2);
        QTest::mouseDClick(display.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(!display.hasSelection());
        QApplication::clipboard()->setText("unchanged");
        display.copy();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("unchanged"));
    }

    Q_SLOT void doubleClickJoinsSourceAndVisualWraps()
    {
        TerminalDisplay display;
        display.resize(1603, 353);
        display.show();
        const qreal cell   = QFontMetricsF(display.font(), display.viewport()).horizontalAdvance(QLatin1Char('M'));
        const int   height = qCeil(QFontMetricsF(display.font(), display.viewport()).height());
        display.resize(qCeil(cell * 15), 353);
        QCoreApplication::processEvents();
        const int        columns = display.terminalSize().width();
        const QByteArray word(columns * 2 + 3, 'w');
        display.receive(" " + word + "\r\nww");
        QVERIFY(display.screen().lines()[0].softWrapped);
        const auto select = [&](const QPoint& point) {
            QTest::mouseDClick(display.viewport(), Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier, point);
            display.copy();
        };
        select(QPoint(qFloor(cell * 2.25), height + 2));
        QCOMPARE(QApplication::clipboard()->text(), QString::fromLatin1(word));
        display.resize(qCeil(cell * (columns + 6)), 353);
        QCoreApplication::processEvents();
        select(QPoint(qFloor(cell * 2.25), height + 2));
        QCOMPARE(QApplication::clipboard()->text(), QString::fromLatin1(word));
        display.setAppearance({
            {"wrapLines", false}
        });
        QCoreApplication::processEvents();
        display.horizontalScrollBar()->setValue(columns);
        select(QPoint(qFloor(cell * 2.25), 2));
        QCOMPARE(QApplication::clipboard()->text(), QString::fromLatin1(word));

        display.clear();
        display.resize(1603, 353);
        display.receive(" " + word + " ");
        QVERIFY(!display.screen().lines()[0].softWrapped);
        display.setAppearance({
            {"wrapLines", true}
        });
        display.resize(qCeil(cell * 15), 353);
        QCoreApplication::processEvents();
        select(QPoint(qFloor(cell * 2.25), height + 2));
        QCOMPARE(QApplication::clipboard()->text(), QString::fromLatin1(word));
    }

    Q_SLOT void doubleClickRespectsMouseReportingAndShiftOverride()
    {
        TerminalDisplay display;
        display.resize(803, 353);
        display.show();
        display.receive("word\x1b[?1000h\x1b[?1006h");
        const qreal  cell = QFontMetricsF(display.font(), display.viewport()).horizontalAdvance(QLatin1Char('M'));
        const QPoint point(qFloor(cell * 1.25), 2);
        QSignalSpy   output(&display, &TerminalDisplay::outputBytes);
        QTest::mouseDClick(display.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(!display.hasSelection());
        QCOMPARE(output.size(), 2);
        QCOMPARE(output[0][1].value<TerminalDisplay::OutputKind>(), TerminalDisplay::OutputKind::Mouse);
        QVERIFY(output[0][0].toByteArray().endsWith('M'));
        QVERIFY(output[1][0].toByteArray().endsWith('m'));
        output.clear();
        QTest::mouseDClick(display.viewport(), Qt::LeftButton, Qt::ShiftModifier, point);
        QTest::mouseRelease(display.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(output.isEmpty());
        QVERIFY(display.hasSelection());
        display.copy();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("word"));
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
    Q_SLOT void partialRepaintMatchesFullRendering()
    {
        TerminalDisplay display;
        display.setAppearance({
            {"fontFamily", "Courier New"},
            {  "fontSize",            12}
        });
        display.resize(803, 353);
        display.show();
        display.clearFocus();
        display.receive(QString::fromUtf8("ASCII\r\n\x1b[1;31;44mStyled 日本 é 😀\x1b[0m\r\nlast").toUtf8());
        QCoreApplication::processEvents();
        const auto full = display.viewport()->grab().toImage();
        QImage     partial(full.size(), full.format());
        partial.setDevicePixelRatio(full.devicePixelRatio());
        partial.fill(Qt::transparent);
        const int   lineHeight = qCeil(QFontMetricsF(display.font(), display.viewport()).height());
        const QRect strip(0, lineHeight, display.viewport()->width(), lineHeight);
        display.viewport()->render(&partial, strip.topLeft(), QRegion(strip));
        const qreal dpr = full.devicePixelRatio();
        const QRect pixels(0, qRound(strip.top() * dpr), qRound(strip.width() * dpr), qRound(strip.height() * dpr));
        QCOMPARE(partial.copy(pixels), full.copy(pixels));
    }
    Q_SLOT void synchronizedPresentationFinishesAfterTimeout()
    {
        TerminalDisplay display;
        display.resize(500, 200);
        display.show();
        display.clearFocus();
        display.receive("\x1b[?25lbefore\x1b[?2026h\r\x1b[31mafter ");
        QTest::qWait(200);
        const auto timedOut = display.viewport()->grab().toImage();
        display.receive("\x1b[?2026l");
        QCOMPARE(display.viewport()->grab().toImage(), timedOut);
        display.setAppearance({
            {"background", "#8015191f"}
        });
        display.resize(550, 200);
        QCoreApplication::processEvents();
        const auto translucent = display.viewport()->grab().toImage();
        display.viewport()->update();
        QCoreApplication::processEvents();
        QCOMPARE(display.viewport()->grab().toImage(), translucent);
    }
    Q_SLOT void resizeRetainsStyledSpacesAndSharedShapes()
    {
        TerminalDisplay display;
        display.resize(803, 353);
        display.show();
        display.clearFocus();
        const auto content = QString::fromUtf8("\x1b[?25lRepeated ASCII 日本 é 😀\r\n"
                                               "\x1b[31mRepeated ASCII 日本 é 😀\r\n"
                                               "\x1b[32;44m    \x1b[7m    \x1b[0m\r\n"
                                               "\x1b[4;31m    \x1b[0m \x1b[9m    \x1b[0m \x1b[53m    \x1b[0m\r\n"
                                               "\x1b[4:2m    \x1b[4:3m    \x1b[4:4m    \x1b[4:5m    \x1b[0m")
                                 .toUtf8();
        display.receive(content);
        QCoreApplication::processEvents();
        const auto original = display.viewport()->grab().toImage();
        const auto text     = display.text();
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            display.resize(853, 353);
            QCoreApplication::processEvents();
            display.resize(803, 353);
            QCoreApplication::processEvents();
            QCOMPARE(display.viewport()->grab().toImage(), original);
            QCOMPARE(display.text(), text);
        }
        display.clear();
        display.receive(content);
        QCOMPARE(display.viewport()->grab().toImage(), original);
        const auto capture = qEnvironmentVariable("TERMASTER_RESIZE_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(original.save(capture));
        }
    }
    Q_SLOT void fontAndDeviceEventsRefreshGeometryWithoutResize()
    {
        TerminalDisplay display;
        display.setAppearance({
            {"fontFamily", "Courier New"},
            {  "fontSize",            12}
        });
        display.resize(803, 353);
        display.show();
        QTest::qWait(20);
        display.clearFocus();
        display.receive(QString::fromUtf8("ASCII 日本 é 😀").toUtf8());
        display.selectAll();
        const auto pixelSize   = display.size();
        const auto initialGrid = display.terminalSize();
        QSignalSpy sizes(&display, &TerminalDisplay::terminalSizeChanged);

        auto font = display.font();
        font.setPointSizeF(18);
        display.setFont(font);
        QCOMPARE(display.size(), pixelSize);
        QVERIFY(display.terminalSize() != initialGrid);
        QVERIFY(display.hasSelection());
        QVERIFY(!sizes.isEmpty());
        const QFontMetricsF metrics(font, display.viewport());
        QCOMPARE(display.terminalSize().width(),
                 int(display.viewport()->width() / metrics.horizontalAdvance(QLatin1Char('M'))));
        QCOMPARE(display.terminalSize().height(), int(display.viewport()->height() / std::ceil(metrics.height())));

        display.selectAll();
        const auto grid   = display.terminalSize();
        const auto pixels = display.viewport()->grab().toImage();
        QEvent     deviceChange(QEvent::DevicePixelRatioChange);
        QCoreApplication::sendEvent(&display, &deviceChange);
        QCOMPARE(display.terminalSize(), grid);
        QVERIFY(display.hasSelection());
        QCOMPARE(display.viewport()->grab().toImage(), pixels);
        QEvent screenChange(QEvent::ScreenChangeInternal);
        QCoreApplication::sendEvent(&display, &screenChange);
        QCOMPARE(display.terminalSize(), grid);
        QVERIFY(display.hasSelection());
    }
    Q_SLOT void unchangedAppearanceAndSubcellResizePreserveSelection()
    {
        TerminalDisplay   display;
        const QVariantMap appearance {
            {"fontFamily", "Courier New"},
            {  "fontSize",            12}
        };
        display.setAppearance(appearance);
        display.resize(803, 353);
        display.show();
        QTest::qWait(20);
        display.clearFocus();
        display.receive(QString::fromUtf8("ASCII \x1b[1;31mstyled\x1b[0m 日本 é 😀").toUtf8());
        display.selectAll();
        QVERIFY(display.hasSelection());
        display.copy();
        const auto selectedText = QApplication::clipboard()->text();
        const auto before       = display.viewport()->grab().toImage();
        const auto revision     = display.screen().lines()[0].revision;
        const auto size         = display.terminalSize();

        display.setAppearance(appearance);
        QVERIFY(display.hasSelection());
        QCOMPARE(display.screen().lines()[0].revision, revision);
        QCOMPARE(display.viewport()->grab().toImage(), before);
        display.resize(display.width() + 1, display.height());
        QCoreApplication::processEvents();
        QCOMPARE(display.terminalSize(), size);
        QVERIFY(display.hasSelection());
        QCOMPARE(display.screen().lines()[0].revision, revision);
        display.copy();
        QCOMPARE(QApplication::clipboard()->text(), selectedText);

        display.setAppearance({
            {"fontFamily", "Courier New"},
            {  "fontSize",            14}
        });
        QVERIFY(!display.hasSelection());
        QVERIFY(display.terminalSize() != size);
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
