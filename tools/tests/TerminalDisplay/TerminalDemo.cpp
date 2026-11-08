#include <QApplication>
#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <Termaster/Core/gui/terminal/TerminalDisplay.h>

namespace
{
QByteArray showcase()
{
    QByteArray data  = "\x1b[2J\x1b[H\x1b[2;3H\x1b[1;38;2;245;165;55mTERMASTER / OWN VT ENGINE\x1b[0m";
    data            += "\x1b[4;3HQt Widgets + cached QTextLayout / synthetic stream / no device connection";
    data            += "\x1b[6;3HSGR: \x1b[1mBold\x1b[22m  \x1b[2mFaint\x1b[22m  \x1b[3mItalic\x1b[23m  ";
    data += "\x1b[4mUnderline\x1b[24m  \x1b[4:2mDouble\x1b[24m  \x1b[4:3mCurly\x1b[24m  \x1b[9mStrike\x1b[0m";
    data += "\x1b[8;3HANSI palette: ";
    for (int color = 0; color < 16; ++color)
    {
        data += "\x1b[48;5;" + QByteArray::number(color) + "m   ";
    }
    data += "\x1b[0m\x1b[10;3HRGB colors:   ";
    for (int step = 0; step < 48; ++step)
    {
        data += "\x1b[48;2;" + QByteArray::number(245 - step * 3) + ';' + QByteArray::number(75 + step * 3) + ';'
              + QByteArray::number(30 + step * 4) + "m ";
    }
    data += "\x1b[0m\x1b[12;3HUnicode: ";
    data += QString::fromUtf8("Русский  日本語  한글  é  😀  👩‍💻  🇺🇳").toUtf8();
    data += "\x1b[14;3HDEC graphics: \x1b(0lqqqqwqqqqk\x1b(B  \x1b[7m Inverse \x1b[0m";
    data += "\x1b[16;3HSixel / own decoder\x1b[16;46HKitty Graphics / PNG / direct transfer";
    data += "\x1b[18;3H\x1bPq\"1;1;160;60#1;2;95;60;20#2;2;20;65;90";
    for (int band = 0; band < 10; ++band)
    {
        data += band % 2 ? "#2!160~-" : "#1!160~-";
    }
    data += "\x1b\\";
    QImage image(180, 84, QImage::Format_ARGB32);
    image.fill(QColor("#243245"));
    QPainter painter(&image);
    for (int i = 0; i < 9; ++i)
    {
        painter.fillRect(8 + i * 19,
                         74 - (i + 1) * 7,
                         13,
                         (i + 1) * 7,
                         QColor(240 - i * 12, 130 + i * 10, 45 + i * 18));
    }
    painter.end();
    QByteArray png;
    QBuffer    buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    data += "\x1b[18;46H\x1b_Ga=T,f=100,i=7,c=26,r=6,C=1;" + png.toBase64() + "\x1b\\";
    data += "\x1b[26;3H\x1b]8;;https://example.com\x1b\\OSC 8 hyperlink\x1b]8;;\x1b\\";
    data += "  / cursor, regions, alternate screen and terminal replies";
    data += "\x1b[29;3HF1: alternate screen    Ctrl+Shift+C/V: copy/paste    Shift+mouse: local selection";
    data += "\x1b[31;3HThe widget is a stream endpoint; processing chains and transports stay outside.";
    data += "\x1b[34;3H\x1b[38;2;245;165;55m> \x1b[0m\x1b[?2004h";
    return data;
}
} // namespace

int main(int argc, char* argv[])
{
    QApplication    application(argc, argv);
    TerminalDisplay terminal;
    terminal.setWindowTitle("Termaster terminal demo - synthetic stream");
    terminal.resize(1280, 800);
    terminal.setAppearance({
        {"fontFamily", "Consolas"},
        {  "fontSize",         12}
    });
    bool alternate = false;
    QObject::connect(
        &terminal,
        &TerminalDisplay::outputBytes,
        &terminal,
        [&](const QByteArray& bytes, TerminalDisplay::OutputKind kind, const QByteArray& echo) {
            if (kind == TerminalDisplay::OutputKind::Keyboard && bytes == "\x1bOP")
            {
                alternate = !alternate;
                terminal.receive(
                    alternate ?
                        "\x1b[?1049h\x1b[2;3H\x1b[1;32mALTERNATE SCREEN\x1b[0m\r\n  F1 restores the primary grid." :
                        "\x1b[?1049l");
            }
            else if (kind == TerminalDisplay::OutputKind::Keyboard || kind == TerminalDisplay::OutputKind::Paste)
            {
                terminal.localEcho(echo);
            }
        });
    terminal.show();
    terminal.setFocus();
    QTimer::singleShot(50, &terminal, [&] { terminal.receive(showcase()); });
    const auto arguments = application.arguments();
    const int  capture   = arguments.indexOf("--capture");
    if (capture >= 0 && capture + 1 < arguments.size())
    {
        QTimer::singleShot(250, &terminal, [&] {
            application.exit(terminal.grab().save(arguments[capture + 1]) ? 0 : 2);
        });
    }
    return application.exec();
}
