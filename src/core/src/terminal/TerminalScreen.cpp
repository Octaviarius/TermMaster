#include <Termaster/Core/terminal/TerminalScreen.h>
#include <algorithm>

namespace
{
QColor paletteColor(int n)
{
    static const char* colors[] = {"#000000",
                                   "#cd3131",
                                   "#0dbc79",
                                   "#e5e510",
                                   "#2472c8",
                                   "#bc3fbc",
                                   "#11a8cd",
                                   "#e5e5e5",
                                   "#666666",
                                   "#f14c4c",
                                   "#23d18b",
                                   "#f5f543",
                                   "#3b8eea",
                                   "#d670d6",
                                   "#29b8db",
                                   "#ffffff"};
    n                           = std::clamp(n, 0, 255);
    if (n < 16)
    {
        return QColor(QString::fromLatin1(colors[n]));
    }
    if (n >= 232)
    {
        int c = 8 + (n - 232) * 10;
        return QColor(c, c, c);
    }
    n              -= 16;
    auto component  = [](int v) { return v ? 55 + 40 * v : 0; };
    return QColor(component(n / 36), component(n / 6 % 6), component(n % 6));
}
} // namespace
TerminalScreen::TerminalScreen()
{
    clear();
}
void TerminalScreen::clear()
{
    _lines  = {Line {}};
    _line   = 0;
    _column = 0;
    _state  = State::Text;
    _sequence.clear();
    _decoder.resetState();
    _foreground = _defaultForeground;
    _background = _defaultBackground;
    ++_revision;
}
void TerminalScreen::setColors(const QColor& foreground, const QColor& background)
{
    _foreground = _defaultForeground = foreground;
    _background = _defaultBackground = background;
}
void TerminalScreen::setLimits(int columns, int rows, int history)
{
    _columns = std::clamp(columns, 1, 4096);
    _rows    = std::clamp(rows, 1, 256);
    _history = std::clamp(history, 0, 100000);
    for (auto& line : _lines)
    {
        if (line.cells.size() > _columns)
        {
            line.cells.resize(_columns);
            line.revision = ++_revision;
        }
    }
    _column = std::min(_column, _columns);
    _trim();
}
const QVector<TerminalScreen::Line>& TerminalScreen::lines() const
{
    return _lines;
}
int TerminalScreen::cursorLine() const
{
    return _line;
}
int TerminalScreen::cursorColumn() const
{
    return _column;
}
quint64 TerminalScreen::discardedLines() const
{
    return _discardedLines;
}
QString TerminalScreen::text() const
{
    QString result;
    for (int i = 0; i < _lines.size(); ++i)
    {
        if (i)
        {
            result += '\n';
        }
        for (const auto& c : _lines[i].cells)
        {
            result += c.text;
        }
    }
    return result;
}
void TerminalScreen::_trim()
{
    const int excess = _lines.size() - (_rows + _history);
    if (excess > 0)
    {
        _lines.remove(0, excess);
        _discardedLines += excess;
        _line            = std::max(0, _line - excess);
    }
}
void TerminalScreen::_newline()
{
    _column = std::min(_column, _columns - 1);
    ++_line;
    if (_line >= _lines.size())
    {
        _lines.append(Line {});
    }
    _trim();
}
void TerminalScreen::_put(const QString& character)
{
    if (_column >= _columns)
    {
        _column = 0;
        _newline();
    }
    auto& line = _lines[_line];
    while (line.cells.size() <= _column)
    {
        line.cells.append(Cell {QStringLiteral(" "), _foreground, _background});
    }
    line.cells[_column++] = Cell {character, _foreground, _background};
    line.revision         = ++_revision;
}
void TerminalScreen::receive(const QByteArray& bytes)
{
    // Decode one byte at a time so control boundaries cannot flush partial UTF-8.
    for (char byte : bytes)
    {
        const unsigned char value = static_cast<unsigned char>(byte);
        if (_state == State::Osc || _state == State::OscEscape)
        {
            if (value == 7 || (_state == State::OscEscape && byte == '\\'))
            {
                _state = State::Text;
            }
            else
            {
                _state = byte == 27 ? State::OscEscape : State::Osc;
            }
            continue;
        }
        if (_state == State::Escape)
        {
            _state = byte == '[' ? State::Csi : byte == ']' ? State::Osc : State::Text;
            _sequence.clear();
            continue;
        }
        if (_state == State::Csi)
        {
            if (value >= 0x40 && value <= 0x7e)
            {
                _csi(byte);
                _state = State::Text;
            }
            else if (_sequence.size() < 256 && value >= 0x20 && value <= 0x3f)
            {
                _sequence += byte;
            }
            else
            {
                _sequence.clear();
                _state = State::Text;
            }
            continue;
        }
        if (value == 27)
        {
            _state = State::Escape;
            continue;
        }
        if (value < 32 || value == 127)
        {
            if (byte == '\r')
            {
                _column = 0;
            }
            else if (byte == '\n')
            {
                _newline();
            }
            else if (byte == '\b')
            {
                _column = std::max(0, _column - 1);
            }
            else if (byte == '\t')
            {
                int count = 8 - _column % 8;
                while (count--)
                {
                    _put(QStringLiteral(" "));
                }
            }
            continue;
        }
        const QString decoded = _decoder(QByteArrayView(&byte, 1));
        for (char32_t scalar : decoded.toUcs4())
        {
            _put(QString::fromUcs4(&scalar, 1));
        }
    }
}
void TerminalScreen::_csi(char command)
{
    if (_sequence.startsWith('?') || _sequence.contains(':'))
    {
        return;
    }
    QList<int> args;
    for (const auto& part : _sequence.split(';'))
    {
        bool ok = false;
        int  n  = part.toInt(&ok);
        args.append(ok ? std::clamp(n, 0, 100000) : 0);
    }
    auto arg = [&](int index, int fallback) { return index < args.size() && args[index] > 0 ? args[index] : fallback; };
    const int top = std::max(0, static_cast<int>(_lines.size()) - _rows);
    if (command == 'm')
    {
        for (int i = 0; i < args.size(); ++i)
        {
            const int n = args[i];
            if (n == 0)
            {
                _foreground = _defaultForeground;
                _background = _defaultBackground;
            }
            else if (n == 39)
            {
                _foreground = _defaultForeground;
            }
            else if (n == 49)
            {
                _background = _defaultBackground;
            }
            else if ((n >= 30 && n <= 37) || (n >= 90 && n <= 97))
            {
                _foreground = paletteColor(n >= 90 ? n - 90 + 8 : n - 30);
            }
            else if ((n >= 40 && n <= 47) || (n >= 100 && n <= 107))
            {
                _background = paletteColor(n >= 100 ? n - 100 + 8 : n - 40);
            }
            else if ((n == 38 || n == 48) && i + 2 < args.size())
            {
                QColor color;
                if (args[i + 1] == 5)
                {
                    color  = paletteColor(args[i + 2]);
                    i     += 2;
                }
                else if (args[i + 1] == 2 && i + 4 < args.size())
                {
                    color  = QColor(std::clamp(args[i + 2], 0, 255),
                                    std::clamp(args[i + 3], 0, 255),
                                    std::clamp(args[i + 4], 0, 255));
                    i     += 4;
                }
                if (color.isValid())
                {
                    if (n == 38)
                    {
                        _foreground = color;
                    }
                    else
                    {
                        _background = color;
                    }
                }
            }
        }
    }
    else if (command == 'A')
    {
        _line = std::max(top, _line - arg(0, 1));
    }
    else if (command == 'B')
    {
        _line = std::min(top + _rows - 1, _line + arg(0, 1));
        while (_lines.size() <= _line)
        {
            _lines.append(Line {});
        }
    }
    else if (command == 'C')
    {
        _column = std::min(_columns - 1, _column + arg(0, 1));
    }
    else if (command == 'D')
    {
        _column = std::max(0, _column - arg(0, 1));
    }
    else if (command == 'G')
    {
        _column = std::min(_columns - 1, arg(0, 1) - 1);
    }
    else if (command == 'H' || command == 'f')
    {
        _line   = top + std::min(_rows - 1, arg(0, 1) - 1);
        _column = std::min(_columns - 1, arg(1, 1) - 1);
        while (_lines.size() <= _line)
        {
            _lines.append(Line {});
        }
    }
    else if (command == 'K' || command == 'J')
    {
        int mode  = args.value(0);
        int first = command == 'J' ? (mode == 0 ? _line : top) : _line;
        int last  = command == 'J' ? (mode == 1 ? _line : static_cast<int>(_lines.size()) - 1) : _line;
        if (mode > 2)
        {
            return;
        }
        for (int r = first; r <= last; ++r)
        {
            auto& line  = _lines[r];
            int   begin = (r == _line && mode == 0) ? _column : 0;
            int   end   = (r == _line && mode == 1) ? std::min(_column + 1, static_cast<int>(line.cells.size())) :
                                                      line.cells.size();
            for (int c = begin; c < end; ++c)
            {
                line.cells[c] = Cell {QStringLiteral(" "), _foreground, _background};
            }
            line.revision = ++_revision;
        }
    }
}
