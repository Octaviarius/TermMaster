#include "Emulator.h"

#include <QApplication>
#include <QKeyEvent>
#include <QRegularExpression>
#include <core/utils.h>

Emulator::Emulator(QObject* parent) : QObject {parent}, _widget(nullptr), _outputNewLine("\r\n")
{
}

void Emulator::setTermWidget(TermWidget* widget)
{
    if (_widget)
    {
        disconnect(_widget, &TermWidget::outputKey, this, &Emulator::inputKey);
    }

    _widget = widget;
    connect(_widget, &TermWidget::outputKey, this, &Emulator::inputKey);
}

void Emulator::setEchoMode(bool enable)
{
    _echoMode = enable;
}

bool Emulator::echoMode() const
{
    return _echoMode;
}

void Emulator::setOutputNewLine(QString syms)
{
    _outputNewLine = syms;
}

QString Emulator::outputNewLine() const
{
    return _outputNewLine;
}

void Emulator::_putCharacter(QChar ch)
{
    auto code = ch.unicode();

    if (_escSequence.length())
    {
        if (code < 0x0020 || code > 0x7f)
        {
            qWarning() << QString("Character %1 can't be in ESC-sequence").arg(code);
            _escSequence.clear();
        }
        else
        {
            _escSequence += ch;
            _handleEsc(_escSequence.sliced(1));
        }
    }
    else
    {
        switch (code)
        {
            // bell
            case 0x0007:
            {
                QApplication::beep();
                break;
            }

            // backspace
            case 0x0008:
            {
                _widget->termModel()->cursor()->shift(0, -1);
                break;
            }

            case QChar::LineFeed:
            {
                _widget->termModel()->cursor()->lineFeed();
                break;
            }

            case QChar::CarriageReturn:
            {
                _widget->termModel()->cursor()->moveToColumn(0);
                break;
            }

            // delete
            case 0x007f:
            {
                _widget->termModel()->cursor()->replaceChar();
                break;
            }

            // start escape sequence
            case 0x001b:
            {
                _escSequence = ch;
                break;
            }

            default:
            {
                if (ch >= QChar::Space)
                {
                    _widget->termModel()->inputData(ch);
                }
            }
        }
    }
}

void Emulator::_escFinished()
{
    _escSequence.clear();
}

void Emulator::_handleEsc(QString esc)
{
    if (esc.length() >= 1)
    {
        auto f = esc.front().unicode();

        switch (f)
        {
            case 'N':
                _escFinished();
                break;

            case 'O':
                _escFinished();
                break;

            case 'P':
                _escFinished();
                break;

            case '[':
                _handleEscCsi(esc.sliced(1));
                break;

            case '\\':
                _escFinished();
                break;

            case ']':
                break;

            case 'X':
                _escFinished();
                break;

            case '^':
                _escFinished();
                break;

            case '_':
                _escFinished();
                break;

            default:
                qWarning() << QString("Unsupported ESC precode '%1'").arg(f);
                _escFinished();
        }
    }
}

void Emulator::_handleEscCsi(QString esc)
{
    if (esc.length() < 1)
    {
        return;
    }

    auto        f = esc.back().unicode();
    QStringList params;
    int         col;
    int         line;
    int         mode;
    bool        ok;

    if (f >= 0x40 && f <= 0x7f)
    {
        esc.resize(esc.length() - 1);

        switch (f)
        {
            // cursor up
            case 'A':
                line   = 1;
                params = esc.split(';');
                if (params.count() > 0)
                {
                    line = params[0].toInt(&ok);
                    if (!ok)
                    {
                        line = 1;
                    }
                }
                _widget->termModel()->cursor()->shift(-line, 0);
                break;

            // cursor down
            case 'B':
                line   = 1;
                params = esc.split(';');
                if (params.count() > 0)
                {
                    line = params[0].toInt(&ok);
                    if (!ok)
                    {
                        line = 1;
                    }
                }
                _widget->termModel()->cursor()->shift(line, 0);
                break;

            // cursor right
            case 'C':
                col    = 1;
                params = esc.split(';');
                if (params.count() > 0)
                {
                    col = params[0].toInt(&ok);
                    if (!ok)
                    {
                        col = 1;
                    }
                }
                _widget->termModel()->cursor()->shift(0, col);
                break;

            // cursor left
            case 'D':
                col    = 1;
                params = esc.split(';');
                if (params.count() > 0)
                {
                    col = params[0].toInt(&ok);
                    if (!ok)
                    {
                        col = 1;
                    }
                }
                _widget->termModel()->cursor()->shift(0, -col);
                break;

            // cursor next line
            case 'E':
                _widget->termModel()->cursor()->moveToNextLine();
                break;

            // cursor prev line
            case 'F':
                _widget->termModel()->cursor()->moveToPrevLine();
                break;

            // cursor horz absolute column
            case 'G':
                break;

            // cursor absolute position
            case 'H':
                line   = 1;
                col    = 1;
                params = esc.split(';');
                if (params.count() >= 1)
                {
                    line = params[0].toInt(&ok);
                    if (!ok)
                    {
                        line = 1;
                    }
                }
                if (params.count() >= 2)
                {
                    col = params[0].toInt(&ok);
                    if (!ok)
                    {
                        col = 1;
                    }
                }
                _widget->termModel()->cursor()->moveTo(line, col);
                break;

            // erase in display
            case 'J':
                if (esc.length() == 0)
                {
                    mode = 0;
                    ok   = true;
                }
                else
                {
                    mode = esc.toInt(&ok);
                }

                if (!ok)
                {
                    mode = 0;
                }

                switch (mode)
                {
                    case 0:
                        _widget->termModel()->cursor()->eraseBelow();
                        break;
                    case 1:
                        _widget->termModel()->cursor()->eraseAbove();
                        break;
                    case 2:
                    case 3:
                        _widget->termModel()->cursor()->eraseAll();
                        break;
                }
                break;

            // erase in line
            case 'K':
                if (esc.length() == 0)
                {
                    mode = 0;
                    ok   = true;
                }
                else
                {
                    mode = esc.toInt(&ok);
                }

                if (!ok)
                {
                    mode = 0;
                }

                switch (mode)
                {
                    case 0:
                        _widget->termModel()->cursor()->eraseLineAfter();
                        break;
                    case 1:
                        _widget->termModel()->cursor()->eraseLineBefore();
                        break;
                    case 2:
                        _widget->termModel()->cursor()->eraseLine();
                        break;
                }
                break;

            // scroll up
            case 'S':
                break;

            // scroll down
            case 'T':
                break;

            // horz/vert position
            case 'f':
                break;

            // graphic rendition
            case 'm':
                _handleEscCsiRendition(esc);
                break;

            // AUX port control
            case 'i':
                break;

            // device status report
            case 'n':
                break;

            // save cursor position
            case 's':
                break;

            // restore saved cursor position
            case 'u':
                break;

            // enable function
            case 'h':
                break;

            // disable function
            case 'l':
                break;

            // special keys
            case '~':
                _handleEscCsiSpecKeys(esc);
                break;
        }

        _escFinished();
    }
}

void Emulator::_handleEscCsiRendition(QString esc)
{
    auto params = esc.split(';');

    // make codes
    QList<int> codes(params.count());

    for (int i = 0; i < codes.count(); i++)
    {
        bool ok;
        codes[i] = params[i].toInt(&ok);
        if (!ok)
        {
            qWarning() << "Rendition param is NaN";
            return;
        }
    }

    auto cursor = _widget->termModel()->cursor();

    TermAttribute attrs = cursor->attrs();

    bool loop = true;
    for (int i = 0; loop && i < codes.count(); i++)
    {
        int code = codes[i];

        switch (code)
        {
            // all reset and return
            case 0:
                attrs = TermAttribute {0, 0, 0};
                break;

            case 1:
                attrs.setAttrs(TermAttribute::Attribute::Bold);
                break;
            case 2:
                attrs.setAttrs(TermAttribute::Attribute::Faint);
                break;
            case 3:
                attrs.setAttrs(TermAttribute::Attribute::Italic);
                break;
            case 4:
                attrs.setAttrs(TermAttribute::Attribute::Underline);
                break;
            case 5:
                attrs.setAttrs(TermAttribute::Attribute::Blink);
                break;
            case 6:
                attrs.setAttrs(TermAttribute::Attribute::FastBlink);
                break;
            case 7:
                attrs.setAttrs(TermAttribute::Attribute::Inversed);
                break;
            case 8:
                attrs.setAttrs(TermAttribute::Attribute::Hidden);
                break;
            case 9:
                attrs.setAttrs(TermAttribute::Attribute::Strikeout);
                break;

            case 21:
                attrs.clrAttrs(TermAttribute::Attribute::Bold);
                break;
            case 22:
                attrs.clrAttrs(TermAttribute::Attribute::Faint | TermAttribute::Attribute::Bold);
                break;
            case 23:
                attrs.clrAttrs(TermAttribute::Attribute::Italic);
                break;
            case 24:
                attrs.clrAttrs(TermAttribute::Attribute::Underline);
                break;
            case 25:
                attrs.clrAttrs(TermAttribute::Attribute::Blink | TermAttribute::Attribute::FastBlink);
                break;
            case 26:
                break;
            case 27:
                attrs.clrAttrs(TermAttribute::Attribute::Inversed);
                break;
            case 28:
                attrs.clrAttrs(TermAttribute::Attribute::Hidden);
                break;
            case 29:
                attrs.clrAttrs(TermAttribute::Attribute::Strikeout);
                break;

            // foreground colors
            case 30:
            case 31:
            case 32:
            case 33:
            case 34:
            case 35:
            case 36:
            case 37:
                attrs.foreground = code - 30;
                attrs.setAttrs(TermAttribute::Attribute::Foreground);
                break;
            case 39:
                attrs.clrAttrs(TermAttribute::Attribute::Foreground);
                break;

            // bright foreground colors
            case 90:
            case 91:
            case 92:
            case 93:
            case 94:
            case 95:
            case 96:
            case 97:
                attrs.foreground = code - 90 + 8;
                attrs.setAttrs(TermAttribute::Attribute::Foreground);
                break;

            // background colors
            case 40:
            case 41:
            case 42:
            case 43:
            case 44:
            case 45:
            case 46:
            case 47:
                attrs.background = code - 40;
                attrs.setAttrs(TermAttribute::Attribute::Background);
                break;
            case 49:
                attrs.clrAttrs(TermAttribute::Attribute::Background);
                break;

            // bright background colors
            case 100:
            case 101:
            case 102:
            case 103:
            case 104:
            case 105:
            case 106:
            case 107:
                attrs.background = code - 100 + 8;
                attrs.setAttrs(TermAttribute::Attribute::Background);
                break;

            // custom colors
            case 38:
            case 48:
            {
                bool isForeground = (code == 38);

                int remain = codes.length() - 1 - i;

                if (remain >= 2)
                {
                    code = codes[i + 1];

                    if (code == 5)
                    {
                        code = codes[i + 2];

                        if (isForeground)
                        {
                            attrs.setAttrs(TermAttribute::Attribute::Foreground);
                            attrs.foreground = code;
                        }
                        else
                        {
                            attrs.setAttrs(TermAttribute::Attribute::Background);
                            attrs.background = code;
                        }

                        i += 2;
                    }
                    else if (code == 2)
                    {
                        if (remain >= 4)
                        {
                            QColor color(codes[i + 2], codes[i + 3], codes[i + 4]);

                            // TODO: find the nearest ID
                            // if (isForeground)
                            // {
                            //     attrs.setAttrs(TermAttribute::Attribute::Foreground);
                            //     attrs.foreground = code;
                            // }
                            // else
                            // {
                            //     attrs.setAttrs(TermAttribute::Attribute::Background);
                            //     attrs.background = code;
                            // }

                            i += 4;
                        }
                    }
                    else
                    {
                        qWarning() << "Unexpected rendition set color code";
                        return;
                    }
                }
                break;
            }

            default:
                qWarning() << "Unexpected rendition code " << code;
                return;
        }
    }

    cursor->setAttrs(attrs);
}

void Emulator::_handleEscCsiSpecKeys(QString esc)
{
    if (esc.length() == 0)
    {
        return;
    }

    bool ok;

    auto params  = esc.split(';');
    uint keycode = params[0].toUInt(&ok);

    if (!ok)
    {
        qWarning() << QString("Unexpected format of EMU_CSI spec key number '%1'").arg(esc);
    }
    else
    {
        auto cursor = _widget->termModel()->cursor();

        switch (keycode)
        {
            case 1:
            case 7:
                cursor->moveToColumn(0);
                break;
            case 2:
                break;
            case 3:
                cursor->replaceChar();
                break;
            case 4:
            case 8:
                cursor->moveToColumn(_widget->termModel()->size().width() - 1);
                break;
            case 5:
                break;
            case 6:
                break;
        }

        // static const std::array<Qt::Key, 40> keymap = []() {
        //     std::array<Qt::Key, 40> ret;
        //     ret[1]  = Qt::Key_Home;
        //     ret[2]  = Qt::Key_Insert;
        //     ret[3]  = Qt::Key_Delete;
        //     ret[4]  = Qt::Key_End;
        //     ret[5]  = Qt::Key_PageUp;
        //     ret[6]  = Qt::Key_PageDown;
        //     ret[7]  = Qt::Key_Home;
        //     ret[8]  = Qt::Key_End;
        //     ret[11] = Qt::Key_F1;
        //     ret[12] = Qt::Key_F2;
        //     ret[13] = Qt::Key_F3;
        //     ret[14] = Qt::Key_F4;
        //     ret[15] = Qt::Key_F5;
        //     ret[17] = Qt::Key_F6;
        //     ret[18] = Qt::Key_F7;
        //     ret[19] = Qt::Key_F8;
        //     ret[20] = Qt::Key_F9;
        //     ret[21] = Qt::Key_F10;
        //     ret[23] = Qt::Key_F11;
        //     ret[24] = Qt::Key_F12;
        //     ret[25] = Qt::Key_F13;
        //     ret[26] = Qt::Key_F14;
        //     ret[28] = Qt::Key_F15;
        //     ret[29] = Qt::Key_F16;
        //     ret[31] = Qt::Key_F17;
        //     ret[32] = Qt::Key_F18;
        //     ret[33] = Qt::Key_F19;
        //     ret[35] = Qt::Key_F20;
        //     return ret;
        // }();

        // if (keycode >= keymap.size())
        // {
        //     qWarning() << QString("Keycode %1 is out of range").arg(keycode);
        // }
        // else
        // {
        //     auto key = keymap[keycode];

        // if (key == 0)
        // {
        //     qWarning() << QString("Keycode %1 is not mapped").arg(keycode);
        // }
        // else
        // {
        //     // TODO: implement
        // }
        // }
    }
}

void Emulator::inputData(QByteArray data)
{
    if (!_incompleteBytes.isEmpty())
    {
        data.insert(0, _incompleteBytes);
        _incompleteBytes.resize(0);
    }

    auto validLength = getValidUtf8Length(data);

    if (validLength != data.length())
    {
        _incompleteBytes += data.sliced(validLength);
    }

    auto text = QString::fromUtf8(data.data(), validLength);

    for (const auto& ch : text)
    {
        _putCharacter(ch);
    }

    _widget->termModel()->update();
}

void Emulator::inputKey(QKeyEvent* event)
{
    QString text;

    bool procText = false;

    if (!(event->modifiers() & ~Qt::KeyboardModifier::ShiftModifier))
    {
        text     = event->text();
        procText = text.length() > 0;
    }

    if (!procText)
    {
        switch (event->key())
        {
            case Qt::Key_Enter:
                text = _outputNewLine;
                break;

            case Qt::Key_Up:
                text = EMU_CSI("A");
                break;

            case Qt::Key_Down:
                text = EMU_CSI("B");
                break;

            case Qt::Key_Right:
                text = EMU_CSI("C");
                break;

            case Qt::Key_Left:
                text = EMU_CSI("D");
                break;

            case Qt::Key_Home:
                text = EMU_CSI_SPECKEY("1");
                break;
            case Qt::Key_Insert:
                text = EMU_CSI_SPECKEY("2");
                break;
            case Qt::Key_Delete:
                text = EMU_CSI_SPECKEY("3");
                break;
            case Qt::Key_End:
                text = EMU_CSI_SPECKEY("4");
                break;
            case Qt::Key_PageUp:
                text = EMU_CSI_SPECKEY("5");
                break;
            case Qt::Key_PageDown:
                text = EMU_CSI_SPECKEY("6");
                break;

            case Qt::Key_F1:
                text = EMU_CSI_SPECKEY("11");
                break;
            case Qt::Key_F2:
                text = EMU_CSI_SPECKEY("12");
                break;
            case Qt::Key_F3:
                text = EMU_CSI_SPECKEY("13");
                break;
            case Qt::Key_F4:
                text = EMU_CSI_SPECKEY("14");
                break;
            case Qt::Key_F5:
                text = EMU_CSI_SPECKEY("15");
                break;
            case Qt::Key_F6:
                text = EMU_CSI_SPECKEY("17");
                break;
            case Qt::Key_F7:
                text = EMU_CSI_SPECKEY("18");
                break;
            case Qt::Key_F8:
                text = EMU_CSI_SPECKEY("19");
                break;
            case Qt::Key_F9:
                text = EMU_CSI_SPECKEY("20");
                break;
            case Qt::Key_F10:
                text = EMU_CSI_SPECKEY("21");
                break;
            case Qt::Key_F11:
                text = EMU_CSI_SPECKEY("23");
                break;
            case Qt::Key_F12:
                text = EMU_CSI_SPECKEY("24");
                break;
            case Qt::Key_F13:
                text = EMU_CSI_SPECKEY("25");
                break;
            case Qt::Key_F14:
                text = EMU_CSI_SPECKEY("26");
                break;
            case Qt::Key_F15:
                text = EMU_CSI_SPECKEY("28");
                break;
            case Qt::Key_F16:
                text = EMU_CSI_SPECKEY("29");
                break;
            case Qt::Key_F17:
                text = EMU_CSI_SPECKEY("31");
                break;
            case Qt::Key_F18:
                text = EMU_CSI_SPECKEY("32");
                break;
            case Qt::Key_F19:
                text = EMU_CSI_SPECKEY("33");
                break;
            case Qt::Key_F20:
                text = EMU_CSI_SPECKEY("34");
                break;
        }
    }

    auto utf8 = text.toUtf8();

    emit outputData(utf8);

    if (_echoMode)
    {
        inputData(utf8);
    }
}
