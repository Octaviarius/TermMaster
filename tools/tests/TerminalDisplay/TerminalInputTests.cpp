#include <QKeyEvent>
#include <QTest>
#include <Termaster/Core/terminal/TerminalInput.h>
#include <Termaster/Core/terminal/TerminalScreen.h>

class TerminalInputTests : public QObject
{
    Q_OBJECT
private slots:
    void legacy();
    void cursorAndFunctions();
    void pasteAndFocus();
    void kitty();
    void mouse();
    void vt52AndBackarrow();
    void kittyEventScope();
    void modifyOtherKeys();
};

void TerminalInputTests::legacy()
{
    TerminalScreen screen;
    const auto     key = [&](int code, Qt::KeyboardModifiers mods = Qt::NoModifier, QString text = {}) {
        return TerminalInput::key(QKeyEvent(QEvent::KeyPress, code, mods, text), screen);
    };
    QCOMPARE(key(Qt::Key_Space, Qt::ControlModifier), QByteArray(1, '\0'));
    QCOMPARE(key(Qt::Key_A, Qt::ControlModifier), QByteArray(1, '\x01'));
    QCOMPARE(key(Qt::Key_BracketLeft, Qt::ControlModifier), QByteArray("\x1b"));
    QCOMPARE(key(Qt::Key_Slash, Qt::ControlModifier), QByteArray(1, '\x1f'));
    QCOMPARE(key(Qt::Key_Backspace, Qt::ControlModifier), QByteArray("\x08"));
    QCOMPARE(key(Qt::Key_A, Qt::AltModifier, QStringLiteral("a")),
             QByteArray("\x1b"
                        "a"));
    QCOMPARE(key(Qt::Key_Tab, Qt::ShiftModifier), QByteArray("\x1b[Z"));
    const QString unicode = QString::fromUtf8("€");
    QCOMPARE(key(Qt::Key_E, Qt::ControlModifier | Qt::AltModifier | Qt::GroupSwitchModifier, unicode),
             unicode.toUtf8());
    QCOMPARE(key(Qt::Key_E, Qt::ControlModifier | Qt::AltModifier, unicode), unicode.toUtf8());
    QCOMPARE(key(Qt::Key_Shift, Qt::ShiftModifier), QByteArray());
    QCOMPARE(key(Qt::Key_Return), QByteArray("\r"));
    screen.receive("\x1b[20h");
    QCOMPARE(key(Qt::Key_Return), QByteArray("\r\n"));
    QVERIFY(TerminalInput::key(QKeyEvent(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, "a"), screen, true).isEmpty());
}

void TerminalInputTests::cursorAndFunctions()
{
    TerminalScreen screen;
    const auto     key = [&](int code, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        return TerminalInput::key(QKeyEvent(QEvent::KeyPress, code, mods), screen);
    };
    QCOMPARE(key(Qt::Key_Up), QByteArray("\x1b[A"));
    screen.receive("\x1b[?1h");
    QCOMPARE(key(Qt::Key_Up), QByteArray("\x1bOA"));
    QCOMPARE(key(Qt::Key_Up, Qt::ControlModifier), QByteArray("\x1b[1;5A"));
    QCOMPARE(key(Qt::Key_F1), QByteArray("\x1bOP"));
    QCOMPARE(key(Qt::Key_F5, Qt::ShiftModifier | Qt::AltModifier), QByteArray("\x1b[15;4~"));
    QCOMPARE(key(Qt::Key_Insert), QByteArray("\x1b[2~"));
    for (int number = Qt::Key_F1; number <= Qt::Key_F35; ++number)
    {
        QVERIFY(!key(number).isEmpty());
    }
    screen.receive("\x1b=");
    QCOMPARE(key(Qt::Key_1, Qt::KeypadModifier), QByteArray("\x1bOq"));
    QCOMPARE(key(Qt::Key_Enter, Qt::KeypadModifier), QByteArray("\x1bOM"));
}

void TerminalInputTests::pasteAndFocus()
{
    TerminalScreen screen;
    QCOMPARE(TerminalInput::paste(QStringLiteral("a\nb"), screen), QByteArray("a\nb"));
    QVERIFY(TerminalInput::focus(true, screen).isEmpty());
    screen.receive("\x1b[?2004h\x1b[?1004h");
    QCOMPARE(TerminalInput::paste(QStringLiteral("a\nb"), screen), QByteArray("\x1b[200~a\nb\x1b[201~"));
    QCOMPARE(TerminalInput::focus(true, screen), QByteArray("\x1b[I"));
    QCOMPARE(TerminalInput::focus(false, screen), QByteArray("\x1b[O"));
    QVERIFY(TerminalInput::paste(QString(4 * 1024 * 1024 + 1, QLatin1Char('x')), screen).isEmpty());
    QVERIFY(TerminalInput::paste(QString(2 * 1024 * 1024, QChar(0x4e2d)), screen).isEmpty());
}

void TerminalInputTests::kitty()
{
    TerminalScreen screen;
    screen.receive("\x1b[>1u");
    QCOMPARE(TerminalInput::key(QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier), screen),
             QByteArray("\x1b[97;5u"));
    QCOMPARE(TerminalInput::key(QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier), screen), QByteArray("\r"));
    screen.receive("\x1b[=31u");
    QCOMPARE(TerminalInput::key(QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier, "A"), screen),
             QByteArray("\x1b[97:65;2:1;65u"));
    QCOMPARE(TerminalInput::key(QKeyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a", true), screen),
             QByteArray("\x1b[97;1:2;97u"));
    QCOMPARE(TerminalInput::key(QKeyEvent(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, "a"), screen, true),
             QByteArray("\x1b[97;1:3u"));
    QCOMPARE(TerminalInput::key(QKeyEvent(QEvent::KeyPress, Qt::Key_Up, Qt::ControlModifier), screen),
             QByteArray("\x1b[1;5:1A"));
    QCOMPARE(TerminalInput::key(QKeyEvent(QEvent::KeyPress, Qt::Key_F13, Qt::NoModifier), screen),
             QByteArray("\x1b[57376;1:1u"));
    QCOMPARE(TerminalInput::key(QKeyEvent(QEvent::KeyPress, Qt::Key_1, Qt::KeypadModifier, "1"), screen),
             QByteArray("\x1b[57400;1:1;49u"));
}

void TerminalInputTests::mouse()
{
    TerminalScreen screen;
    using Kind       = TerminalInput::MouseEventKind;
    const auto mouse = [&](Kind kind, int x = 0, int y = 0, Qt::MouseButtons buttons = Qt::LeftButton) {
        return TerminalInput::mouse(kind, Qt::LeftButton, buttons, x, y, Qt::ControlModifier, screen);
    };
    QVERIFY(mouse(Kind::Press).isEmpty());
    screen.receive("\x1b[?1000h\x1b[?1006h");
    QCOMPARE(mouse(Kind::Press, 2, 3), QByteArray("\x1b[<16;3;4M"));
    QCOMPARE(mouse(Kind::Release, 2, 3), QByteArray("\x1b[<16;3;4m"));
    QCOMPARE(mouse(Kind::WheelUp), QByteArray("\x1b[<80;1;1M"));
    QVERIFY(mouse(Kind::Move).isEmpty());
    screen.receive("\x1b[?1002h");
    QCOMPARE(mouse(Kind::Move), QByteArray("\x1b[<48;1;1M"));
    QVERIFY(mouse(Kind::Move, 0, 0, Qt::NoButton).isEmpty());
    screen.receive("\x1b[?1003h");
    QCOMPARE(mouse(Kind::Move, 0, 0, Qt::NoButton), QByteArray("\x1b[<51;1;1M"));
    screen.receive("\x1b[?1006l\x1b[?1015h");
    QCOMPARE(mouse(Kind::Press), QByteArray("\x1b[48;1;1M"));
    screen.receive("\x1b[?1015l");
    QCOMPARE(mouse(Kind::Press), QByteArray::fromHex("1b5b4d302121"));
    QVERIFY(mouse(Kind::Press, 223, 0).isEmpty());
    screen.receive("\x1b[?1005h");
    QVERIFY(!mouse(Kind::Press, 300, 300).isEmpty());
    QVERIFY(mouse(Kind::Press, 2015, 0).isEmpty());
    screen.receive("\x1b[?1005l\x1b[?1016h");
    QVERIFY(mouse(Kind::Press).isEmpty());
    QCOMPARE(TerminalInput::mouse(Kind::Press, Qt::LeftButton, Qt::LeftButton, 49, 69, Qt::NoModifier, screen, true),
             QByteArray("\x1b[<0;50;70M"));
}

void TerminalInputTests::vt52AndBackarrow()
{
    TerminalScreen screen;
    const auto     key = [&](int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        return TerminalInput::key(QKeyEvent(QEvent::KeyPress, code, modifiers), screen);
    };
    QCOMPARE(key(Qt::Key_Backspace), QByteArray("\x7f"));
    screen.receive("\x1b[?67h");
    QCOMPARE(key(Qt::Key_Backspace), QByteArray("\x08"));
    QCOMPARE(key(Qt::Key_Backspace, Qt::ControlModifier), QByteArray("\x08"));
    screen.receive("\x1b[?67l");
    QCOMPARE(key(Qt::Key_Backspace), QByteArray("\x7f"));
    QCOMPARE(key(Qt::Key_Backspace, Qt::ControlModifier), QByteArray("\x08"));
    screen.receive("\x1b[?2l");
    QVERIFY(!screen.mode(2));
    QCOMPARE(key(Qt::Key_Up), QByteArray::fromHex("1b41"));
    QCOMPARE(key(Qt::Key_Down), QByteArray::fromHex("1b42"));
    QCOMPARE(key(Qt::Key_Right), QByteArray::fromHex("1b43"));
    QCOMPARE(key(Qt::Key_Left), QByteArray::fromHex("1b44"));
    QCOMPARE(key(Qt::Key_Home), QByteArray::fromHex("1b48"));
    QCOMPARE(key(Qt::Key_F1), QByteArray::fromHex("1b50"));
    QCOMPARE(key(Qt::Key_F2), QByteArray::fromHex("1b51"));
    QCOMPARE(key(Qt::Key_F3), QByteArray::fromHex("1b52"));
    QCOMPARE(key(Qt::Key_F4), QByteArray::fromHex("1b53"));
    screen.receive("\x1b=");
    QCOMPARE(key(Qt::Key_1, Qt::KeypadModifier), QByteArray("\x1b?q"));
    QCOMPARE(key(Qt::Key_Enter, Qt::KeypadModifier), QByteArray("\x1b?M"));
    screen.receive("\x1b<");
    QVERIFY(screen.mode(2));
    QCOMPARE(key(Qt::Key_1, Qt::KeypadModifier), QByteArray("\x1bOq"));
}

void TerminalInputTests::kittyEventScope()
{
    TerminalScreen screen;
    screen.receive("\x1b[=2u");
    const QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
    const QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, "a");
    QCOMPARE(TerminalInput::key(press, screen), QByteArray("a"));
    QVERIFY(TerminalInput::key(release, screen, true).isEmpty());
    QCOMPARE(TerminalInput::key(QKeyEvent(QEvent::KeyRelease, Qt::Key_Up, Qt::NoModifier), screen, true),
             QByteArray("\x1b[1;1:3A"));
    screen.receive("\x1b[=10u");
    QCOMPARE(TerminalInput::key(press, screen), QByteArray("\x1b[97;1:1u"));
    QCOMPARE(TerminalInput::key(release, screen, true), QByteArray("\x1b[97;1:3u"));
}

void TerminalInputTests::modifyOtherKeys()
{
    TerminalScreen screen;
    const auto     key = [&](int code, Qt::KeyboardModifiers modifiers, QString text = {}) {
        return TerminalInput::key(QKeyEvent(QEvent::KeyPress, code, modifiers, text), screen);
    };
    screen.receive("\x1b[>4;2m");
    QCOMPARE(key(Qt::Key_A, Qt::ControlModifier), QByteArray("\x1b[27;5;97~"));
    QCOMPARE(key(Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier), QByteArray("\x1b[27;6;65~"));
    QCOMPARE(key(Qt::Key_A, Qt::ShiftModifier, "A"), QByteArray("\x1b[27;2;65~"));
    QCOMPARE(key(Qt::Key_Tab, Qt::ControlModifier), QByteArray("\x1b[27;5;9~"));
    QCOMPARE(key(Qt::Key_Space, Qt::ControlModifier), QByteArray("\x1b[27;5;32~"));
    QCOMPARE(key(Qt::Key_A, Qt::AltModifier, "a"), QByteArray("\x1b[27;3;97~"));
    QCOMPARE(key(Qt::Key_Up, Qt::ControlModifier), QByteArray("\x1b[1;5A"));
    QCOMPARE(key(Qt::Key_A, Qt::NoModifier, "a"), QByteArray("a"));
    const QString euro = QString::fromUtf8("€");
    QCOMPARE(key(Qt::Key_E, Qt::ControlModifier | Qt::AltModifier | Qt::GroupSwitchModifier, euro), euro.toUtf8());
    screen.receive("\x1b[>4;1m");
    QCOMPARE(key(Qt::Key_A, Qt::ControlModifier), QByteArray(1, '\x01'));
    QCOMPARE(key(Qt::Key_Space, Qt::ControlModifier), QByteArray(1, '\0'));
    QCOMPARE(key(Qt::Key_Period, Qt::ControlModifier), QByteArray("\x1b[27;5;46~"));
    screen.receive("\x1b[>4m");
    QCOMPARE(key(Qt::Key_A, Qt::ControlModifier), QByteArray(1, '\x01'));
    screen.receive("\x1b[>4;2m\x1b[>1u");
    QCOMPARE(key(Qt::Key_A, Qt::ControlModifier), QByteArray("\x1b[97;5u"));
}

QTEST_GUILESS_MAIN(TerminalInputTests)
#include "TerminalInputTests.moc"
