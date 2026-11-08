#include <QKeyEvent>
#include <Termaster/Core/terminal/TerminalInput.h>
#include <Termaster/Core/terminal/TerminalScreen.h>

namespace
{
int _modifiers(Qt::KeyboardModifiers modifiers)
{
    return 1 + (modifiers.testFlag(Qt::ShiftModifier) ? 1 : 0) + (modifiers.testFlag(Qt::AltModifier) ? 2 : 0)
         + (modifiers.testFlag(Qt::ControlModifier) ? 4 : 0) + (modifiers.testFlag(Qt::MetaModifier) ? 8 : 0);
}

struct KeyCode
{
    int  number = 0;
    char final  = 'u';
    bool ss3    = false;
};
KeyCode _functional(int key, bool kitty)
{
    switch (key)
    {
        case Qt::Key_Escape:
            return {27};
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return {13};
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            return {9};
        case Qt::Key_Backspace:
            return {127};
        case Qt::Key_Insert:
            return {2, '~'};
        case Qt::Key_Delete:
            return {3, '~'};
        case Qt::Key_PageUp:
            return {5, '~'};
        case Qt::Key_PageDown:
            return {6, '~'};
        case Qt::Key_Up:
            return {1, 'A', true};
        case Qt::Key_Down:
            return {1, 'B', true};
        case Qt::Key_Right:
            return {1, 'C', true};
        case Qt::Key_Left:
            return {1, 'D', true};
        case Qt::Key_Home:
            return {1, 'H', true};
        case Qt::Key_End:
            return {1, 'F', true};
        case Qt::Key_CapsLock:
            return {57358};
        case Qt::Key_ScrollLock:
            return {57359};
        case Qt::Key_NumLock:
            return {57360};
        case Qt::Key_Print:
            return {57361};
        case Qt::Key_Pause:
            return {57362};
        case Qt::Key_Menu:
            return {57363};
        case Qt::Key_Shift:
            return {57441};
        case Qt::Key_Control:
            return {57442};
        case Qt::Key_Alt:
            return {57443};
        case Qt::Key_Meta:
            return {57444};
        case Qt::Key_AltGr:
            return {57453};
        default:
            break;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F4)
    {
        if (kitty && key == Qt::Key_F3)
        {
            return {13, '~'};
        }
        return {1, static_cast<char>('P' + key - Qt::Key_F1), true};
    }
    constexpr int codes[] = {15, 17, 18, 19, 20, 21, 23, 24, 25, 26, 28, 29, 31, 32, 33, 34};
    if (key >= Qt::Key_F5 && key <= Qt::Key_F20)
    {
        return {codes[key - Qt::Key_F5], '~'};
    }
    if (key >= Qt::Key_F21 && key <= Qt::Key_F35)
    {
        return {57364 + key - Qt::Key_F1};
    }
    return {};
}

int _keypadCode(int key)
{
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
    {
        return 57399 + key - Qt::Key_0;
    }
    switch (key)
    {
        case Qt::Key_Period:
        case Qt::Key_Delete:
            return 57409;
        case Qt::Key_Slash:
            return 57410;
        case Qt::Key_Asterisk:
            return 57411;
        case Qt::Key_Minus:
            return 57412;
        case Qt::Key_Plus:
            return 57413;
        case Qt::Key_Enter:
        case Qt::Key_Return:
            return 57414;
        case Qt::Key_Equal:
            return 57415;
        case Qt::Key_Comma:
            return 57416;
        case Qt::Key_Left:
            return 57417;
        case Qt::Key_Right:
            return 57418;
        case Qt::Key_Up:
            return 57419;
        case Qt::Key_Down:
            return 57420;
        case Qt::Key_PageUp:
            return 57421;
        case Qt::Key_PageDown:
            return 57422;
        case Qt::Key_Home:
            return 57423;
        case Qt::Key_End:
            return 57424;
        case Qt::Key_Insert:
            return 57425;
        case Qt::Key_Clear:
            return 57427;
        default:
            return 0;
    }
}

QByteArray _csi(KeyCode code, int modifiers, QByteArray event = {}, QByteArray alternate = {}, QByteArray text = {})
{
    QByteArray result = "\x1b[";
    const bool letter = code.final != '~' && code.final != 'u';
    if (!letter || modifiers != 1 || !event.isEmpty() || !text.isEmpty())
    {
        result += QByteArray::number(code.number) + alternate;
    }
    if (modifiers != 1 || !event.isEmpty() || !text.isEmpty())
    {
        result += ';' + QByteArray::number(modifiers) + event;
    }
    if (!text.isEmpty())
    {
        result += ';' + text;
    }
    return result + code.final;
}
} // namespace

QByteArray TerminalInput::key(const QKeyEvent& event, const TerminalScreen& screen, bool release)
{
    auto       modifiers = event.modifiers();
    const bool group     = modifiers.testFlag(Qt::GroupSwitchModifier)
                        || (modifiers.testFlag(Qt::ControlModifier) && modifiers.testFlag(Qt::AltModifier)
                            && !event.text().isEmpty() && event.text().toUcs4().first() > 127);
    if (group)
    {
        modifiers &= ~(Qt::ControlModifier | Qt::AltModifier);
    }
    const int  key           = event.key();
    const bool ansi          = screen.mode(2);
    const int  flags         = ansi ? screen.kittyKeyboardFlags() : 0;
    const bool all           = flags & 8;
    const bool control       = modifiers.testFlag(Qt::ControlModifier);
    const bool alt           = modifiers.testFlag(Qt::AltModifier);
    const bool shift         = modifiers.testFlag(Qt::ShiftModifier);
    const bool specialLegacy = key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Tab
                            || key == Qt::Key_Backtab || key == Qt::Key_Backspace;
    const bool enhanced      = all || ((flags & 1) && !specialLegacy && (control || alt || key == Qt::Key_Escape));
    if (release && (!(flags & 2) || (!all && specialLegacy)))
    {
        return {};
    }
    KeyCode    code   = _functional(key, enhanced || flags);
    const bool keypad = modifiers.testFlag(Qt::KeypadModifier);
    if ((enhanced || all) && keypad && _keypadCode(key))
    {
        code = {_keypadCode(key)};
    }
    if (flags && key >= Qt::Key_F13 && key <= Qt::Key_F35)
    {
        code = {57364 + key - Qt::Key_F1};
    }
    const bool functional = code.number != 0;
    if (enhanced || (flags && functional && !specialLegacy))
    {
        if (!functional)
        {
            uint scalar = 0;
            if (key >= Qt::Key_A && key <= Qt::Key_Z)
            {
                scalar = 'a' + key - Qt::Key_A;
            }
            else if (key >= 0x20 && key <= 0x10ffff)
            {
                const char32_t unicode = static_cast<char32_t>(key);
                const auto     lower   = QString::fromUcs4(&unicode, 1).toLower().toUcs4();
                scalar                 = lower.isEmpty() ? static_cast<uint>(key) : lower.first();
            }
            else if (!event.text().isEmpty())
            {
                scalar = event.text().toUcs4().first();
            }
            if (!scalar)
            {
                return {};
            }
            code = {static_cast<int>(scalar)};
        }
        QByteArray alternate;
        const auto scalars = event.text().toUcs4();
        if ((flags & 4) && shift && !scalars.isEmpty() && scalars.first() >= 32 && code.final == 'u'
            && scalars.first() != static_cast<uint>(code.number))
        {
            alternate = ':' + QByteArray::number(scalars.first());
        }
        QByteArray associated;
        if ((flags & 16) && all && !release)
        {
            for (uint scalar : scalars)
            {
                if (scalar < 32 || (scalar >= 127 && scalar <= 159))
                {
                    continue;
                }
                if (!associated.isEmpty())
                {
                    associated += ':';
                }
                associated += QByteArray::number(scalar);
            }
        }
        QByteArray type;
        if (flags & 2)
        {
            type = ':' + QByteArray::number(release ? 3 : event.isAutoRepeat() ? 2 : 1);
        }
        return _csi(code, _modifiers(modifiers), type, alternate, associated);
    }
    if (release)
    {
        return {};
    }
    // xterm's ordinary-key encoding is independent of functional/keypad modes.
    // Kitty negotiation takes precedence above. Qt already resolves AltGr text.
    const int  otherKeys    = ansi && !flags ? screen.modifyOtherKeys() : 0;
    const bool ordinary     = key >= 0x20 && key <= 0x10ffff;
    const bool knownControl = (key >= Qt::Key_A && key <= Qt::Key_Z) || key == Qt::Key_Space || key == Qt::Key_At
                           || key == Qt::Key_BracketLeft || key == Qt::Key_Backslash || key == Qt::Key_BracketRight
                           || key == Qt::Key_AsciiCircum || key == Qt::Key_Underscore || key == Qt::Key_Slash
                           || key == Qt::Key_Question || (key >= Qt::Key_2 && key <= Qt::Key_8);
    const bool encodeOther  = !keypad && !group
                           && ((otherKeys == 2 && _modifiers(modifiers) != 1
                                && (ordinary || specialLegacy || key == Qt::Key_Escape))
                               || (otherKeys == 1 && ordinary && control && !knownControl));
    if (encodeOther)
    {
        int scalar = key;
        if (key >= Qt::Key_A && key <= Qt::Key_Z)
        {
            scalar = (shift ? 'A' : 'a') + key - Qt::Key_A;
        }
        else if (specialLegacy || key == Qt::Key_Escape)
        {
            scalar = code.number;
        }
        else if (!event.text().isEmpty() && event.text().toUcs4().first() >= 32)
        {
            scalar = static_cast<int>(event.text().toUcs4().first());
        }
        return QByteArray("\x1b[27;") + QByteArray::number(_modifiers(modifiers)) + ';' + QByteArray::number(scalar)
             + '~';
    }
    QByteArray bytes;
    if (keypad && screen.applicationKeypad())
    {
        const int number = _keypadCode(key);
        char      final  = 0;
        if (number >= 57399 && number <= 57408)
        {
            final = static_cast<char>('p' + number - 57399);
        }
        else
        {
            switch (number)
            {
                case 57409:
                    final = 'n';
                    break;
                case 57410:
                    final = 'o';
                    break;
                case 57411:
                    final = 'j';
                    break;
                case 57412:
                    final = 'm';
                    break;
                case 57413:
                    final = 'k';
                    break;
                case 57414:
                    final = 'M';
                    break;
                case 57415:
                    final = 'X';
                    break;
                case 57416:
                    final = 'l';
                    break;
                default:
                    break;
            }
        }
        if (final)
        {
            if (!ansi)
            {
                return QByteArray("\x1b?") + final;
            }
            return _modifiers(modifiers) == 1 ? QByteArray("\x1bO") + final : _csi({1, final}, _modifiers(modifiers));
        }
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter)
    {
        bytes = screen.mode(20, false) ? "\r\n" : "\r";
    }
    else if (key == Qt::Key_Backspace)
    {
        bytes = control || screen.mode(67) ? "\x08" : "\x7f";
    }
    else if (key == Qt::Key_Escape)
    {
        bytes = "\x1b";
    }
    else if (key == Qt::Key_Backtab || (key == Qt::Key_Tab && shift))
    {
        bytes = "\x1b[Z";
    }
    else if (key == Qt::Key_Tab)
    {
        bytes = "\t";
    }
    else if (functional)
    {
        if (!ansi
            && (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Right || key == Qt::Key_Left
                || key == Qt::Key_Home || (key >= Qt::Key_F1 && key <= Qt::Key_F4)))
        {
            return QByteArray(1, '\x1b') + code.final;
        }
        if (code.final == 'u' && !(key >= Qt::Key_F21 && key <= Qt::Key_F35))
        {
            return {};
        }
        if (code.ss3 && _modifiers(modifiers) == 1 && (key >= Qt::Key_F1 || screen.applicationCursorKeys()))
        {
            return QByteArray("\x1bO") + code.final;
        }
        return _csi(code, _modifiers(modifiers));
    }
    else if (control && !group)
    {
        int value = -1;
        if (key >= Qt::Key_A && key <= Qt::Key_Z)
        {
            value = key - Qt::Key_A + 1;
        }
        else
        {
            switch (key)
            {
                case Qt::Key_Space:
                case Qt::Key_At:
                case Qt::Key_2:
                    value = 0;
                    break;
                case Qt::Key_BracketLeft:
                case Qt::Key_3:
                    value = 27;
                    break;
                case Qt::Key_Backslash:
                case Qt::Key_4:
                    value = 28;
                    break;
                case Qt::Key_BracketRight:
                case Qt::Key_5:
                    value = 29;
                    break;
                case Qt::Key_AsciiCircum:
                case Qt::Key_6:
                    value = 30;
                    break;
                case Qt::Key_Underscore:
                case Qt::Key_Slash:
                case Qt::Key_7:
                    value = 31;
                    break;
                case Qt::Key_Question:
                case Qt::Key_8:
                    value = 127;
                    break;
                default:
                    break;
            }
        }
        if (value >= 0)
        {
            bytes.append(static_cast<char>(value));
        }
        else
        {
            bytes = event.text().toUtf8();
        }
    }
    else
    {
        bytes = event.text().toUtf8();
    }
    if (alt && !bytes.isEmpty())
    {
        bytes.prepend('\x1b');
    }
    return bytes;
}

QByteArray TerminalInput::paste(const QString& text, const TerminalScreen& screen)
{
    constexpr qsizetype limit = 4 * 1024 * 1024;
    if (text.size() > limit)
    {
        return {};
    }
    QByteArray bytes = text.toUtf8();
    if (bytes.size() > limit || bytes.isEmpty())
    {
        return {};
    }
    if (screen.bracketedPaste())
    {
        return QByteArray("\x1b[200~") + bytes + "\x1b[201~";
    }
    return bytes;
}

QByteArray TerminalInput::focus(bool focused, const TerminalScreen& screen)
{
    return screen.focusReporting() ? QByteArray(focused ? "\x1b[I" : "\x1b[O") : QByteArray();
}

QByteArray TerminalInput::mouse(MouseEventKind        kind,
                                Qt::MouseButton       button,
                                Qt::MouseButtons      buttons,
                                int                   column,
                                int                   row,
                                Qt::KeyboardModifiers modifiers,
                                const TerminalScreen& screen,
                                bool                  pixelCoordinates)
{
    const int tracking = screen.mouseTracking();
    const int encoding = screen.mouseEncoding();
    if (!tracking || column < 0 || row < 0 || column >= 1000000 || row >= 1000000)
    {
        return {};
    }
    if (encoding == 1016 && !pixelCoordinates)
    {
        return {};
    }
    if (tracking == 9 && kind != MouseEventKind::Press)
    {
        return {};
    }
    if (kind == MouseEventKind::Move
        && (tracking == 1000 || tracking == 9 || (tracking == 1002 && buttons == Qt::NoButton)))
    {
        return {};
    }
    if (kind == MouseEventKind::Move)
    {
        if (buttons.testFlag(Qt::LeftButton))
        {
            button = Qt::LeftButton;
        }
        else if (buttons.testFlag(Qt::MiddleButton))
        {
            button = Qt::MiddleButton;
        }
        else if (buttons.testFlag(Qt::RightButton))
        {
            button = Qt::RightButton;
        }
        else
        {
            button = Qt::NoButton;
        }
    }
    int code = button == Qt::LeftButton ? 0 : button == Qt::MiddleButton ? 1 : button == Qt::RightButton ? 2 : 3;
    if (kind == MouseEventKind::WheelUp)
    {
        code = 64;
    }
    else if (kind == MouseEventKind::WheelDown)
    {
        code = 65;
    }
    else if (kind == MouseEventKind::Move)
    {
        code |= 32;
    }
    else if (kind == MouseEventKind::Release && encoding != 1006 && encoding != 1016)
    {
        code = 3;
    }
    if (tracking != 9)
    {
        if (modifiers.testFlag(Qt::ShiftModifier))
        {
            code |= 4;
        }
        if (modifiers.testFlag(Qt::AltModifier))
        {
            code |= 8;
        }
        if (modifiers.testFlag(Qt::ControlModifier))
        {
            code |= 16;
        }
    }
    const int x = column + 1;
    const int y = row + 1;
    if (encoding == 1006 || encoding == 1016)
    {
        return QByteArray("\x1b[<") + QByteArray::number(code) + ';' + QByteArray::number(x) + ';'
             + QByteArray::number(y) + (kind == MouseEventKind::Release ? 'm' : 'M');
    }
    if (encoding == 1015)
    {
        return QByteArray("\x1b[") + QByteArray::number(code + 32) + ';' + QByteArray::number(x) + ';'
             + QByteArray::number(y) + 'M';
    }
    if (encoding == 1005)
    {
        if (x > 2015 || y > 2015)
        {
            return {};
        }
        const char32_t scalars[] = {static_cast<char32_t>(code + 32),
                                    static_cast<char32_t>(x + 32),
                                    static_cast<char32_t>(y + 32)};
        return QByteArray("\x1b[M") + QString::fromUcs4(scalars, 3).toUtf8();
    }
    if (x > 223 || y > 223)
    {
        return {};
    }
    QByteArray result = "\x1b[M";
    result.append(static_cast<char>(code + 32));
    result.append(static_cast<char>(x + 32));
    result.append(static_cast<char>(y + 32));
    return result;
}
