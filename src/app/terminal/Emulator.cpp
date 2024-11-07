#include "Emulator.h"

#include <QApplication>

Emulator::Emulator(QObject* parent) : QObject {parent}
{
}

void Emulator::setTerminalWidget(TerminalWidget* widget)
{
    if (_widget)
    {
        disconnect(_widget, &TerminalWidget::outputKey, this, &Emulator::inputKey);
    }

    _widget = widget;
    connect(_widget, &TerminalWidget::outputKey, this, &Emulator::inputKey);
}

void Emulator::_putCharacter(QChar ch)
{
    auto code = ch.unicode();

    if (_isEscSequence)
    {

        if (code < 0x0020 || code > 0x7f)
        {
            qWarning() << QString("Character %1 can't be in ESC-sequence").arg(code);
            _isEscSequence = false;
        }
        else
        {
            _escSequence += ch;

            if (code >= 0x40 && code <= 0x7e)
            {
                _handleEsc(_escSequence);
                _isEscSequence = false;
            }
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
                _widget->shiftCursorPosition(-1, 0);
                // _cursor.linePos = _cursor.linePos > 0 ? _cursor.linePos - 1 : 0;
                break;
            }

            case QChar::LineFeed:
            {
                _widget->shiftCursorPosition(0, 1);

                // _cursor.linePos++;
                // if (_cursor.linePos > _activeLineLast)
                // {
                //     _lines.resize(_lines.count() + 1);
                //     _onTerminalResize();
                // }
                break;
            }

            case QChar::CarriageReturn:
            {
                _widget->setCursorPositionLine(0);
                break;
            }

            // delete
            case 0x007f:
            {
                _widget->deleteCharacter();
                break;
            }

            // start escape sequence
            case 0x001b:
            {
                _isEscSequence = true;
                break;
            }

            default:
            {
                if (ch >= QChar::Space)
                {
                    _widget->putCharacter(ch);
                }

                // _cursor.columnPos++;
            }
        }
    }
}

void Emulator::_handleEsc(QString esc)
{
    if (esc.length() < 2)
    {
        qWarning() << QString("ESC sequence is too short");
    }
    else
    {
        switch ((char)esc[1].unicode())
        {
            case '[':
                _handleEscCsi(esc.sliced(1));
                break;
        }
    }

    _isEscSequence = false;
}

void Emulator::_handleEscCsi(QString esc)
{
    auto finalCode = esc[-1].unicode();
    esc.resize(esc.length() - 1);

    switch (finalCode)
    {
        // cursor up
        case 'A':
            break;

        // cursor down
        case 'B':
            break;

        // cursor right
        case 'C':
            break;

        // cursor left
        case 'D':
            break;

        // cursor next line
        case 'E':
            break;

        // cursor prev line
        case 'F':
            break;

        // cursor horz absolute column
        case 'G':
            break;

        // cursor absolute position
        case 'H':
            break;

        // erase in display
        case 'J':
            break;

        // erase in line
        case 'K':
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
}

void Emulator::_handleEscCsiRendition(QString esc)
{
}

void Emulator::_handleEscCsiSpecKeys(QString esc)
{
    bool ok;
    uint code = esc.toUInt(&ok);

    if (!ok)
    {
        qWarning() << QString("Unexpected format of CSI spec key number '%1'").arg(esc);
    }
    else
    {
        static const std::array<Qt::Key, 40> keymap = []() {
            std::array<Qt::Key, 40> ret;
            ret[1]  = Qt::Key_Home;
            ret[2]  = Qt::Key_Insert;
            ret[3]  = Qt::Key_Delete;
            ret[4]  = Qt::Key_End;
            ret[5]  = Qt::Key_PageUp;
            ret[6]  = Qt::Key_PageDown;
            ret[7]  = Qt::Key_Home;
            ret[8]  = Qt::Key_End;
            ret[11] = Qt::Key_F1;
            ret[12] = Qt::Key_F2;
            ret[13] = Qt::Key_F3;
            ret[14] = Qt::Key_F4;
            ret[15] = Qt::Key_F5;
            ret[17] = Qt::Key_F6;
            ret[18] = Qt::Key_F7;
            ret[19] = Qt::Key_F8;
            ret[20] = Qt::Key_F9;
            ret[21] = Qt::Key_F10;
            ret[23] = Qt::Key_F11;
            ret[24] = Qt::Key_F12;
            ret[25] = Qt::Key_F13;
            ret[26] = Qt::Key_F14;
            ret[28] = Qt::Key_F15;
            ret[29] = Qt::Key_F16;
            ret[31] = Qt::Key_F17;
            ret[32] = Qt::Key_F18;
            ret[33] = Qt::Key_F19;
            ret[35] = Qt::Key_F20;
            return ret;
        }();

        if (code >= keymap.size())
        {
            qWarning() << QString("Keycode %1 is out of range").arg(code);
        }
        else
        {
            auto key = keymap[code];

            if (key == 0)
            {
                qWarning() << QString("Keycode %1 is not mapped").arg(code);
            }
            else
            {
                // TODO: implement
            }
        }
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
}

void Emulator::inputKey(Qt::Key key)
{
}
