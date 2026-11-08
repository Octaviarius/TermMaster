#include "TerminalUnicode.h"

#include <Termaster/Core/Terminal/TerminalScreen.h>
#include <algorithm>

namespace
{
QColor palette(int n)
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
        return QColor(colors[n]);
    }
    if (n >= 232)
    {
        int c = 8 + (n - 232) * 10;
        return QColor(c, c, c);
    }
    n      -= 16;
    auto c  = [](int v) { return v ? 55 + 40 * v : 0; };
    return QColor(c(n / 36), c(n / 6 % 6), c(n % 6));
}
QColor parseColor(const QByteArray& text)
{
    if (text.size() == 7 && text[0] == '#')
    {
        for (char byte : text.sliced(1))
        {
            if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F')))
            {
                return QColor();
            }
        }
        return QColor(QString::fromLatin1(text));
    }
    if (!text.startsWith("rgb:"))
    {
        return QColor();
    }
    const auto parts = text.sliced(4).split('/');
    if (parts.size() != 3)
    {
        return QColor();
    }
    int components[3];
    for (int i = 0; i < 3; ++i)
    {
        if (parts[i].isEmpty() || parts[i].size() > 4)
        {
            return QColor();
        }
        for (char byte : parts[i])
        {
            if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F')))
            {
                return QColor();
            }
        }
        bool ok    = false;
        int  value = parts[i].toInt(&ok, 16);
        if (!ok)
        {
            return QColor();
        }
        int maximum   = (1 << (4 * parts[i].size())) - 1;
        components[i] = (value * 255 + maximum / 2) / maximum;
    }
    return QColor(components[0], components[1], components[2]);
}
QByteArray colorReply(const QColor& color)
{
    return "rgb:" + QByteArray::number(color.red() * 257, 16).rightJustified(4, '0') + '/'
         + QByteArray::number(color.green() * 257, 16).rightJustified(4, '0') + '/'
         + QByteArray::number(color.blue() * 257, 16).rightJustified(4, '0');
}
bool knownMode(int number, bool priv)
{
    if (!priv)
    {
        return number == 4 || number == 20;
    }
    switch (number)
    {
        case 1:
        case 2:
        case 5:
        case 6:
        case 7:
        case 9:
        case 12:
        case 25:
        case 47:
        case 66:
        case 67:
        case 1000:
        case 1002:
        case 1003:
        case 1004:
        case 1005:
        case 1006:
        case 1015:
        case 1016:
        case 1047:
        case 1048:
        case 1049:
        case 2004:
        case 2026:
            return true;
        default:
            return false;
    }
}
} // namespace
void TerminalScreen::Decoder::_append(QString& result, char32_t scalar)
{
    if (!(_atStart && scalar == 0xfeff))
    {
        result += QString::fromUcs4(&scalar, 1);
    }
    _atStart = false;
}
void TerminalScreen::Decoder::resetState()
{
    _codepoint   = 0;
    _remaining   = 0;
    _nextMinimum = 0x80;
    _nextMaximum = 0xbf;
    _atStart     = true;
}
QString TerminalScreen::Decoder::flushIncomplete()
{
    QString result;
    if (_remaining)
    {
        _remaining = 0;
        _codepoint = 0;
        _append(result, 0xfffd);
    }
    _nextMinimum = 0x80;
    _nextMaximum = 0xbf;
    return result;
}
QString TerminalScreen::Decoder::decode(QByteArrayView bytes)
{
    QString result;
    for (char raw : bytes)
    {
        const unsigned char byte = static_cast<unsigned char>(raw);
        if (_remaining)
        {
            if (byte >= _nextMinimum && byte <= _nextMaximum)
            {
                _codepoint   = (_codepoint << 6) | (byte & 0x3f);
                _nextMinimum = 0x80;
                _nextMaximum = 0xbf;
                if (--_remaining == 0)
                {
                    _append(result, _codepoint);
                }
                continue;
            }
            _append(result, 0xfffd);
            _remaining = 0;
            _codepoint = 0;
        }
        _nextMinimum = 0x80;
        _nextMaximum = 0xbf;
        if (byte < 0x80)
        {
            _append(result, byte);
        }
        else if (byte >= 0xc2 && byte <= 0xdf)
        {
            _codepoint = byte & 0x1f;
            _remaining = 1;
        }
        else if (byte >= 0xe0 && byte <= 0xef)
        {
            _codepoint = byte & 0x0f;
            _remaining = 2;
            if (byte == 0xe0)
            {
                _nextMinimum = 0xa0;
            }
            if (byte == 0xed)
            {
                _nextMaximum = 0x9f;
            }
        }
        else if (byte >= 0xf0 && byte <= 0xf4)
        {
            _codepoint = byte & 7;
            _remaining = 3;
            if (byte == 0xf0)
            {
                _nextMinimum = 0x90;
            }
            if (byte == 0xf4)
            {
                _nextMaximum = 0x8f;
            }
        }
        else
        {
            _append(result, 0xfffd);
        }
    }
    return result;
}
TerminalScreen::TerminalScreen()
{
    clear();
}
void TerminalScreen::_resetStyle()
{
    _style            = Cell {};
    _style.foreground = _defaultForeground;
    _style.background = _defaultBackground;
}
TerminalScreen::Cell TerminalScreen::_blank() const
{
    Cell c  = _style;
    c.text  = QStringLiteral(" ");
    c.width = 1;
    c.hyperlink.clear();
    c.protectedCell = false;
    return c;
}
TerminalScreen::Line TerminalScreen::_blankLine()
{
    return Line {QVector<Cell>(_columns, _blank()), ++_revision};
}
int TerminalScreen::_screenTop() const
{
    return std::max(0, int(_lines.size()) - _rows);
}
void TerminalScreen::clear()
{
    ++_resetRevision;
    _parser.reset();
    _defaultForeground = _configuredForeground;
    _defaultBackground = _configuredBackground;
    _palette.resize(256);
    for (int i = 0; i < 256; ++i)
    {
        _palette[i] = palette(i);
    }
    _decoder.resetState();
    _echoDecoder.resetState();
    _resetStyle();
    _lines.clear();
    _primary.clear();
    _column      = 0;
    _row         = 0;
    _top         = 0;
    _bottom      = _rows - 1;
    _wrap        = false;
    _alternate   = false;
    _keypad      = false;
    _joinNext    = false;
    _graphics[0] = _graphics[1] = false;
    _charset                    = 0;
    _cursorShape                = 0;
    _kittyFlags                 = 0;
    _privateModes               = {2, 7, 12, 25};
    _vt52                       = false;
    _vt52Address                = -1;
    _modes.clear();
    _kittyStack.clear();
    _modifyOtherKeys = 0;
    _tabs.clear();
    _output.clear();
    _lastPrinted.clear();
    _discardedLines = 0;
    for (int c = 8; c < _columns; c += 8)
    {
        _tabs.insert(c);
    }
    for (int r = 0; r < _rows; ++r)
    {
        _lines.append(_blankLine());
    }
    _save();
}
void TerminalScreen::setLimits(int columns, int rows, int history)
{
    columns = std::clamp(columns, 1, 4096);
    rows    = std::clamp(rows, 1, 256);
    history = std::clamp(history, 0, std::min(100000, maximumCells / columns - rows));
    if (columns == _columns && rows == _rows)
    {
        if (history == _history)
        {
            return;
        }
        _history = history;
        _trim();
        if (!_primary.isEmpty())
        {
            int excess = int(_primary.size()) - (_rows + std::min(_history, maximumCells / _columns - 2 * _rows));
            if (excess > 0)
            {
                _primary.remove(0, excess);
                _discardedLines += excess;
            }
        }
        return;
    }
    const int oldColumns = _columns;
    const int oldRows    = _rows;
    _columns             = std::clamp(columns, 1, 4096);
    _rows                = std::clamp(rows, 1, 256);
    _history             = std::clamp(history, 0, std::min(100000, maximumCells / _columns - _rows));
    auto resize          = [&](QVector<Line>& lines, int& cursorRow, Saved* saved, int historyLimit) {
        const int oldBase        = std::max(0, int(lines.size()) - oldRows);
        int       absoluteCursor = oldBase + cursorRow;
        int       absoluteSaved  = oldBase + (saved ? saved->row : cursorRow);
        if (_rows < oldRows)
        {
            const int remove = std::min(oldRows - _rows, oldRows - 1 - cursorRow);
            if (remove > 0)
            {
                lines.remove(lines.size() - remove, remove);
            }
        }
        if (_rows > oldRows)
        {
            for (int r = oldRows; r < _rows; ++r)
            {
                lines.append(_blankLine());
            }
        }
        // Trim before widening cells, keeping transient allocations bounded too.
        const int excess = std::max(0, int(lines.size()) - (_rows + historyLimit));
        if (excess)
        {
            lines.remove(0, excess);
            absoluteCursor -= excess;
            absoluteSaved  -= excess;
            if (&lines != &_lines || !_alternate)
            {
                _discardedLines += excess;
            }
        }
        for (auto& line : lines)
        {
            const int previousColumns = int(line.cells.size());
            line.cells.resize(_columns);
            for (int c = previousColumns; c < _columns; ++c)
            {
                line.cells[c] = _blank();
            }
            _repair(line);
            line.revision = ++_revision;
        }
        while (lines.size() < _rows)
        {
            lines.append(_blankLine());
        }
        const int base = std::max(0, int(lines.size()) - _rows);
        cursorRow      = std::clamp(absoluteCursor - base, 0, _rows - 1);
        if (saved)
        {
            saved->row    = std::clamp(absoluteSaved - base, 0, _rows - 1);
            saved->column = std::clamp(saved->column, 0, _columns - 1);
            if (oldColumns != _columns)
            {
                saved->wrap = false;
            }
        }
    };
    resize(_lines, _row, &_saved, _alternate ? 0 : _history);
    if (!_primary.isEmpty())
    {
        resize(_primary, _primarySaved.row, nullptr, std::min(_history, maximumCells / _columns - 2 * _rows));
        _primarySaved.column = std::clamp(_primarySaved.column, 0, _columns - 1);
        if (oldColumns != _columns)
        {
            _primarySaved.wrap = false;
        }
    }
    _column = std::min(_column, _columns - 1);
    if (oldColumns != _columns)
    {
        _wrap = false;
    }
    _top    = 0;
    _bottom = _rows - 1;
    for (int c = ((oldColumns + 7) / 8) * 8; c < _columns; c += 8)
    {
        if (c)
        {
            _tabs.insert(c);
        }
    }
    _trim();
}
void TerminalScreen::setColors(const QColor& foreground, const QColor& background)
{
    _configuredForeground = _defaultForeground = foreground;
    _configuredBackground = _defaultBackground = background;
    _recolor();
}
void TerminalScreen::_recolor()
{
    auto update = [&](Cell& cell) {
        const QColor foreground = cell.defaultForeground    ? _defaultForeground :
                                  cell.foregroundIndex >= 0 ? _palette[cell.foregroundIndex] :
                                                              cell.foreground;
        const QColor background = cell.defaultBackground    ? _defaultBackground :
                                  cell.backgroundIndex >= 0 ? _palette[cell.backgroundIndex] :
                                                              cell.background;
        bool         changed    = cell.foreground != foreground || cell.background != background;
        cell.foreground         = foreground;
        cell.background         = background;
        return changed;
    };
    update(_style);
    update(_saved.style);
    update(_primarySaved.style);
    for (auto* buffer : {&_lines, &_primary})
    {
        for (auto& line : *buffer)
        {
            bool changed = false;
            for (auto& cell : line.cells)
            {
                changed |= update(cell);
            }
            if (changed)
            {
                line.revision = ++_revision;
            }
        }
    }
}
const QVector<TerminalScreen::Line>& TerminalScreen::lines() const
{
    return _lines;
}
int TerminalScreen::cursorLine() const
{
    return _screenTop() + _row;
}
int TerminalScreen::cursorColumn() const
{
    return _column;
}
quint64 TerminalScreen::discardedLines() const
{
    return _discardedLines;
}
quint64 TerminalScreen::resetRevision() const
{
    return _resetRevision;
}
bool TerminalScreen::alternateScreen() const
{
    return _alternate;
}
bool TerminalScreen::cursorVisible() const
{
    return mode(25);
}
int TerminalScreen::cursorShape() const
{
    return _cursorShape;
}
bool TerminalScreen::mode(int number, bool priv) const
{
    return (priv ? _privateModes : _modes).contains(number);
}
bool TerminalScreen::bracketedPaste() const
{
    return mode(2004);
}
bool TerminalScreen::applicationCursorKeys() const
{
    return mode(1);
}
bool TerminalScreen::applicationKeypad() const
{
    return _keypad;
}
bool TerminalScreen::focusReporting() const
{
    return mode(1004);
}
int TerminalScreen::mouseTracking() const
{
    for (int m : {1003, 1002, 1000, 9})
    {
        if (mode(m))
        {
            return m;
        }
    }
    return 0;
}
int TerminalScreen::mouseEncoding() const
{
    for (int m : {1016, 1006, 1015, 1005})
    {
        if (mode(m))
        {
            return m;
        }
    }
    return 0;
}
int TerminalScreen::kittyKeyboardFlags() const
{
    return _kittyFlags;
}
int TerminalScreen::modifyOtherKeys() const
{
    return _modifyOtherKeys;
}
QString TerminalScreen::title() const
{
    return _title;
}
QString TerminalScreen::currentDirectory() const
{
    return _directory;
}
QByteArray TerminalScreen::takeOutput()
{
    QByteArray result;
    result.swap(_output);
    return result;
}
void TerminalScreen::setReplyHandler(ReplyHandler handler)
{
    _replyHandler = std::move(handler);
}
void TerminalScreen::setExtensionHandler(std::function<void(const VtParser::Sequence&)> handler)
{
    _extensionHandler = std::move(handler);
}
QString TerminalScreen::text() const
{
    QStringList result;
    for (const auto& line : _lines)
    {
        QString s;
        for (const auto& c : line.cells)
        {
            if (c.width)
            {
                s += c.text;
            }
        }
        while (s.endsWith(' '))
        {
            s.chop(1);
        }
        result.append(s);
    }
    while (!result.isEmpty() && result.last().isEmpty())
    {
        result.removeLast();
    }
    return result.join('\n');
}
void TerminalScreen::_trim()
{
    int excess = int(_lines.size()) - (_rows + (_alternate ? 0 : _history));
    if (excess > 0)
    {
        _lines.remove(0, excess);
        if (!_alternate)
        {
            _discardedLines += excess;
        }
    }
}
void TerminalScreen::_touch(int row)
{
    _lines[_screenTop() + row].revision = ++_revision;
}
void TerminalScreen::_repair(Line& line)
{
    for (int c = 0; c < line.cells.size(); ++c)
    {
        auto& cell = line.cells[c];
        if (cell.width == 2 && (c + 1 >= line.cells.size() || line.cells[c + 1].width != 0))
        {
            cell = _blank();
        }
        else if (cell.width == 0 && (c == 0 || line.cells[c - 1].width != 2))
        {
            cell = _blank();
        }
    }
}
void TerminalScreen::_erase(int row, int first, int last, bool selective)
{
    auto& line = _lines[_screenTop() + row];
    first      = std::clamp(first, 0, _columns);
    last       = std::clamp(last, 0, _columns);
    if (first > 0 && first < _columns && line.cells[first].width == 0)
    {
        --first;
    }
    if (last < _columns && last > 0 && line.cells[last - 1].width == 2)
    {
        ++last;
    }
    for (int c = first; c < last; ++c)
    {
        if (!selective || !line.cells[c].protectedCell)
        {
            line.cells[c] = _blank();
        }
    }
    _touch(row);
}
void TerminalScreen::_move(int row, int column)
{
    _row      = std::clamp(row, mode(6) ? _top : 0, mode(6) ? _bottom : _rows - 1);
    _column   = std::clamp(column, 0, _columns - 1);
    _wrap     = false;
    _joinNext = false;
}
void TerminalScreen::_scroll(int count, bool down)
{
    count = std::clamp(count, 1, _bottom - _top + 1);
    for (int n = 0; n < count; ++n)
    {
        int base = _screenTop();
        if (!down && _top == 0 && _bottom == _rows - 1 && !_alternate)
        {
            _lines.append(_blankLine());
            _trim();
        }
        else if (down)
        {
            _lines.removeAt(base + _bottom);
            _lines.insert(base + _top, _blankLine());
        }
        else
        {
            _lines.removeAt(base + _top);
            _lines.insert(base + _bottom, _blankLine());
        }
    }
    for (int r = _top; r <= _bottom; ++r)
    {
        _touch(r);
    }
}
void TerminalScreen::_lineFeed()
{
    _wrap = false;
    if (_row == _bottom)
    {
        _scroll(1);
    }
    else if (_row < _rows - 1)
    {
        ++_row;
    }
    if (mode(20, false))
    {
        _column = 0;
    }
}
void TerminalScreen::_put(char32_t scalar)
{
    QString    text         = QString::fromUcs4(&scalar, 1);
    int        width        = TerminalUnicode::width(scalar);
    const bool regional     = scalar >= 0x1f1e6 && scalar <= 0x1f1ff;
    bool       regionalPair = false;
    if (regional && (_column > 0 || _wrap))
    {
        const auto& cells    = _lines[cursorLine()].cells;
        int         previous = _wrap ? _column : _column - 1;
        if (cells[previous].width == 0 && previous > 0)
        {
            --previous;
        }
        const auto chars = cells[previous].text.toUcs4();
        regionalPair     = chars.size() == 1 && chars[0] >= 0x1f1e6 && chars[0] <= 0x1f1ff;
    }
    if (regional)
    {
        width = 2;
    }
    if (width == 0 || _joinNext || regionalPair || (scalar >= 0x1f3fb && scalar <= 0x1f3ff))
    {
        int r = _row;
        int c = _wrap ? _column : _column - 1;
        if (c < 0 && r > 0)
        {
            --r;
            c = _columns - 1;
        }
        if (c >= 0)
        {
            auto& line = _lines[_screenTop() + r];
            if (line.cells[c].width == 0 && c > 0)
            {
                --c;
            }
            if (line.cells[c].text.size() + text.size() <= 256)
            {
                line.cells[c].text += text;
                if ((scalar == 0xfe0f || scalar == 0x20e3) && line.cells[c].width == 1 && c + 1 < _columns)
                {
                    _erase(r, c + 1, c + 2);
                    line.cells[c].width = 2;
                    Cell continuation   = line.cells[c];
                    continuation.text.clear();
                    continuation.width = 0;
                    line.cells[c + 1]  = continuation;
                    if (r == _row && !_wrap)
                    {
                        ++_column;
                        if (_column >= _columns)
                        {
                            _column = _columns - 1;
                            _wrap   = true;
                        }
                    }
                }
                _touch(r);
            }
        }
        _joinNext = scalar == 0x200d;
        return;
    }
    if (_graphics[_charset] && scalar >= 0x60 && scalar <= 0x7e)
    {
        static const char16_t graphics[] = u"◆▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥π≠£·";
        text                             = QChar(graphics[scalar - 0x60]);
    }
    if (_wrap && mode(7))
    {
        _column = 0;
        _lineFeed();
    }
    _wrap = false;
    if (width == 2 && _column == _columns - 1)
    {
        if (mode(7))
        {
            _column = 0;
            _lineFeed();
        }
        else
        {
            return;
        }
    }
    if (width > _columns)
    {
        return;
    }
    auto& line = _lines[_screenTop() + _row];
    if (mode(4, false))
    {
        for (int c = _columns - 1; c >= _column + width; --c)
        {
            line.cells[c] = line.cells[c - width];
        }
    }
    _erase(_row, _column, _column + width);
    Cell cell           = _style;
    cell.text           = text;
    cell.width          = width;
    line.cells[_column] = cell;
    if (width == 2)
    {
        cell.text.clear();
        cell.width              = 0;
        line.cells[_column + 1] = cell;
    }
    _repair(line);
    _touch(_row);
    _lastPrinted  = text;
    _column      += width;
    if (_column >= _columns)
    {
        _column = _columns - 1;
        _wrap   = true;
    }
}
void TerminalScreen::_save()
{
    _saved = {_row, _column, _style, mode(6), _wrap};
}
void TerminalScreen::_restore()
{
    _row    = std::clamp(_saved.row, 0, _rows - 1);
    _column = std::clamp(_saved.column, 0, _columns - 1);
    _style  = _saved.style;
    _wrap   = _saved.wrap;
    if (_saved.origin)
    {
        _privateModes.insert(6);
    }
    else
    {
        _privateModes.remove(6);
    }
}
void TerminalScreen::_reply(const QByteArray& bytes)
{
    if (_replyHandler)
    {
        _replyHandler(bytes);
        return;
    }
    if (_output.size() + bytes.size() <= 65536)
    {
        _output += bytes;
    }
}
void TerminalScreen::_setMode(int number, bool enabled, bool priv)
{
    if (!knownMode(number, priv))
    {
        return;
    }
    auto& modes = priv ? _privateModes : _modes;
    if (enabled)
    {
        modes.insert(number);
    }
    else
    {
        modes.remove(number);
    }
    if (priv && number == 2)
    {
        _vt52        = !enabled;
        _vt52Address = -1;
    }
    if (priv && number == 66)
    {
        _keypad = enabled;
    }
    if (priv && number == 6)
    {
        _move(enabled ? _top : 0, 0);
    }
    if (priv && (number == 47 || number == 1047 || number == 1049) && enabled != _alternate)
    {
        if (enabled)
        {
            _save();
            _primarySaved          = _saved;
            _primary               = std::move(_lines);
            const int primaryLimit = std::min(_history, maximumCells / _columns - 2 * _rows) + _rows;
            const int excess       = std::max(0, int(_primary.size()) - primaryLimit);
            if (excess)
            {
                _primary.remove(0, excess);
                _discardedLines += excess;
            }
            _lines.clear();
            for (int r = 0; r < _rows; ++r)
            {
                _lines.append(_blankLine());
            }
            _row       = 0;
            _column    = 0;
            _alternate = true;
        }
        else
        {
            _lines     = std::move(_primary);
            _alternate = false;
            _saved     = _primarySaved;
            _restore();
        }
        _top    = 0;
        _bottom = _rows - 1;
        _wrap   = false;
    }
    if (priv && number == 1048)
    {
        if (enabled)
        {
            _save();
        }
        else
        {
            _restore();
        }
    }
}
void TerminalScreen::receive(const QByteArray& bytes)
{
    const auto handler = [this](const VtParser::Sequence& sequence) { _sequence(sequence); };
    qsizetype  start   = 0;
    for (qsizetype i = 0; i < bytes.size(); ++i)
    {
        const unsigned char byte = static_cast<unsigned char>(bytes[i]);
        if (byte >= 0x20 && byte != 0x7f)
        {
            continue;
        }
        if (i > start)
        {
            _parser.receive(QByteArrayView(bytes).sliced(start, i - start), handler);
        }
        for (char32_t scalar : _decoder.flushIncomplete().toUcs4())
        {
            _put(scalar);
        }
        _parser.receive(QByteArrayView(bytes).sliced(i, 1), handler);
        start = i + 1;
    }
    if (start < bytes.size())
    {
        _parser.receive(QByteArrayView(bytes).sliced(start), handler);
    }
}
void TerminalScreen::_sequence(const VtParser::Sequence& s)
{
    using K = VtParser::Kind;
    if (s.kind == K::Print)
    {
        QByteArray data = s.data;
        while (_vt52Address >= 0 && !data.isEmpty())
        {
            int value = std::max(0, int(static_cast<unsigned char>(data[0])) - 32);
            data.remove(0, 1);
            if (_vt52Address == 0)
            {
                _vt52Row     = value;
                _vt52Address = 1;
            }
            else
            {
                _move(_vt52Row, value);
                _vt52Address = -1;
            }
        }
        QString decoded = _decoder.decode(data);
        for (char32_t c : decoded.toUcs4())
        {
            _put(c);
        }
    }
    else if (s.kind == K::Execute)
    {
        switch (s.command)
        {
            case 8:
                _column = std::max(0, _column - 1);
                _wrap   = false;
                break;
            case 9:
            {
                int c = _column + 1;
                while (c < _columns - 1 && !_tabs.contains(c))
                {
                    ++c;
                }
                _column = std::min(c, _columns - 1);
                _wrap   = false;
                break;
            }
            case 10:
            case 11:
            case 12:
                _lineFeed();
                break;
            case 13:
                _column = 0;
                _wrap   = false;
                break;
            case 14:
                _charset = 1;
                break;
            case 15:
                _charset = 0;
                break;
            default:
                break;
        }
    }
    else if (s.kind == K::Escape)
    {
        if (_vt52 && s.intermediates.isEmpty())
        {
            switch (s.command)
            {
                case 'A':
                    _move(_row - 1, _column);
                    break;
                case 'B':
                    _move(_row + 1, _column);
                    break;
                case 'C':
                    _move(_row, _column + 1);
                    break;
                case 'D':
                    _move(_row, _column - 1);
                    break;
                case 'H':
                    _move(0, 0);
                    break;
                case 'I':
                    if (_row == 0)
                    {
                        _scroll(1, true);
                    }
                    else
                    {
                        --_row;
                    }
                    break;
                case 'J':
                    _erase(_row, _column, _columns);
                    for (int r = _row + 1; r < _rows; ++r)
                    {
                        _erase(r, 0, _columns);
                    }
                    break;
                case 'K':
                    _erase(_row, _column, _columns);
                    break;
                case 'Y':
                    _vt52Address = 0;
                    break;
                case 'Z':
                    _reply("\x1b/Z");
                    break;
                case 'F':
                    _graphics[_charset] = true;
                    break;
                case 'G':
                    _graphics[_charset] = false;
                    break;
                case '=':
                    _keypad = true;
                    break;
                case '>':
                    _keypad = false;
                    break;
                case '<':
                    _vt52 = false;
                    _privateModes.insert(2);
                    break;
                default:
                    break;
            }
            return;
        }
        if (s.intermediates == "#" && s.command == '8')
        {
            for (int r = 0; r < _rows; ++r)
            {
                auto& line = _lines[_screenTop() + r];
                for (auto& cell : line.cells)
                {
                    cell      = _blank();
                    cell.text = QStringLiteral("E");
                }
                _touch(r);
            }
            _move(0, 0);
            return;
        }
        if (!s.intermediates.isEmpty() && s.intermediates != "(" && s.intermediates != ")")
        {
            return;
        }
        if (s.intermediates == "(" || s.intermediates == ")")
        {
            _graphics[s.intermediates == ")"] = s.command == '0';
        }
        else
        {
            switch (s.command)
            {
                case '7':
                    _save();
                    break;
                case '8':
                    _restore();
                    break;
                case 'D':
                    _lineFeed();
                    break;
                case 'E':
                    _column = 0;
                    _lineFeed();
                    break;
                case 'M':
                    if (_row == _top)
                    {
                        _scroll(1, true);
                    }
                    else
                    {
                        _row = std::max(0, _row - 1);
                    }
                    break;
                case 'H':
                    _tabs.insert(_column);
                    break;
                case '=':
                    _keypad = true;
                    break;
                case '>':
                    _keypad = false;
                    break;
                case 'c':
                {
                    QByteArray pendingReplies = std::move(_output);
                    clear();
                    _output = std::move(pendingReplies);
                    break;
                }
                default:
                    break;
            }
        }
    }
    else if (s.kind == K::Csi)
    {
        if (!_vt52)
        {
            _csi(s);
        }
    }
    else if (s.kind == K::Osc)
    {
        _osc(s.data);
        if (_extensionHandler)
        {
            _extensionHandler(s);
        }
    }
    else if (s.kind == K::Apc)
    {
        if (_extensionHandler)
        {
            _extensionHandler(s);
        }
    }
    else if (s.kind == K::Dcs)
    {
        if (s.intermediates == "$" && s.command == 'q')
        {
            QByteArray value;
            if (s.data == "r")
            {
                value = QByteArray::number(_top + 1) + ';' + QByteArray::number(_bottom + 1) + 'r';
            }
            _reply("\x1bP" + QByteArray(value.isEmpty() ? "0$r" : "1$r") + value + "\x1b\\");
        }
        if (_extensionHandler)
        {
            _extensionHandler(s);
        }
    }
}
void TerminalScreen::_csi(const VtParser::Sequence& s)
{
    auto arg = [&](int i, int fallback = 1) {
        return i < s.parameters.size() && !s.parameters[i].isEmpty() && s.parameters[i][0] > 0 ?
                   std::min(s.parameters[i][0], 1000000) :
                   fallback;
    };
    auto raw = [&](int i, int fallback = 0) {
        return i < s.parameters.size() && !s.parameters[i].isEmpty() && s.parameters[i][0] >= 0 ? s.parameters[i][0] :
                                                                                                  fallback;
    };
    if (s.command == 'm' && s.privateMarkers == ">" && s.intermediates.isEmpty())
    {
        if (raw(0) == 4 && s.parameters.size() <= 2 && raw(1) <= 2)
        {
            _modifyOtherKeys = raw(1);
        }
        return;
    }
    const QByteArray& markers          = s.privateMarkers;
    bool              namespaceAllowed = markers.isEmpty();
    if (s.command == 'h' || s.command == 'l' || s.command == 'J' || s.command == 'K' || s.command == 'n'
        || s.command == 'p')
    {
        namespaceAllowed |= markers == "?";
    }
    if (s.command == 'c')
    {
        namespaceAllowed |= markers == ">";
    }
    if (s.command == 'u')
    {
        namespaceAllowed |= markers == "?" || markers == ">" || markers == "=" || markers == "<";
    }
    if (!namespaceAllowed)
    {
        return;
    }
    if (!s.intermediates.isEmpty())
    {
        bool supported = (s.command == 'q' && (s.intermediates == "\"" || s.intermediates == " "))
                      || (s.command == 'p' && s.intermediates == "$")
                      || (markers.isEmpty() && s.intermediates == "$"
                          && (s.command == 'z' || s.command == '{' || s.command == 'x' || s.command == 'v'));
        if (!supported)
        {
            return;
        }
    }
    bool priv   = s.privateMarkers == "?";
    int  n      = arg(0);
    int  origin = mode(6) ? _top : 0;
    if (s.command == 'q' && s.intermediates == "\"")
    {
        _style.protectedCell = raw(0) == 1;
        return;
    }
    if (s.intermediates == "$" && (s.command == 'z' || s.command == '{' || s.command == 'x' || s.command == 'v'))
    {
        int offset = s.command == 'x' ? 1 : 0;
        int top    = std::clamp(arg(offset) - 1 + origin, 0, _rows - 1);
        int left   = std::clamp(arg(offset + 1) - 1, 0, _columns - 1);
        int bottom = std::clamp(arg(offset + 2, _rows) - 1 + origin, 0, _rows - 1);
        int right  = std::clamp(arg(offset + 3, _columns) - 1, 0, _columns - 1);
        if (bottom < top || right < left)
        {
            return;
        }
        if (s.command == 'v')
        {
            int                    destinationTop  = std::clamp(arg(5) - 1 + origin, 0, _rows - 1);
            int                    destinationLeft = std::clamp(arg(6) - 1, 0, _columns - 1);
            QVector<QVector<Cell>> copy;
            for (int r = top; r <= bottom; ++r)
            {
                copy.append(_lines[_screenTop() + r].cells.mid(left, right - left + 1));
            }
            for (int r = 0; r < copy.size() && destinationTop + r < _rows; ++r)
            {
                auto& line = _lines[_screenTop() + destinationTop + r];
                for (int c = 0; c < copy[r].size() && destinationLeft + c < _columns; ++c)
                {
                    line.cells[destinationLeft + c] = copy[r][c];
                }
                _repair(line);
                _touch(destinationTop + r);
            }
        }
        else
        {
            for (int r = top; r <= bottom; ++r)
            {
                if (s.command == 'x')
                {
                    char32_t character = arg(0, 32);
                    if (character < 32 || (character > 126 && character < 160) || character > 255)
                    {
                        return;
                    }
                    auto& line = _lines[_screenTop() + r];
                    _erase(r, left, right + 1);
                    Cell cell  = _style;
                    cell.text  = QString::fromUcs4(&character, 1);
                    cell.width = 1;
                    for (int c = left; c <= right; ++c)
                    {
                        line.cells[c] = cell;
                    }
                    _repair(line);
                    _touch(r);
                }
                else
                {
                    _erase(r, left, right + 1, s.command == '{');
                }
            }
        }
        return;
    }
    if (s.command == 'm')
    {
        _sgr(s);
        return;
    }
    if (s.command == 'h' || s.command == 'l')
    {
        for (const auto& p : s.parameters)
        {
            if (!p.isEmpty())
            {
                _setMode(p[0], s.command == 'h', priv);
            }
        }
        return;
    }
    if (s.command == 'p' && s.intermediates == "$")
    {
        _reply("\x1b[" + QByteArray(priv ? "?" : "") + QByteArray::number(n) + ';'
               + QByteArray::number(knownMode(n, priv) ? (mode(n, priv) ? 1 : 2) : 0) + "$y");
        return;
    }
    if (s.command == 'u' && s.privateMarkers == ">")
    {
        if (_kittyStack.size() < 32)
        {
            _kittyStack.append(_kittyFlags);
        }
        _kittyFlags = raw(0) & 31;
        return;
    }
    if (s.command == 'u' && s.privateMarkers == "=")
    {
        int flags     = raw(0) & 31;
        int operation = arg(1);
        if (operation == 1)
        {
            _kittyFlags = flags;
        }
        else if (operation == 2)
        {
            _kittyFlags |= flags;
        }
        else if (operation == 3)
        {
            _kittyFlags &= ~flags;
        }
        return;
    }
    if (s.command == 'u' && s.privateMarkers == "<")
    {
        for (int i = 0; i < std::min(n, 32); ++i)
        {
            _kittyFlags = _kittyStack.isEmpty() ? 0 : _kittyStack.takeLast();
        }
        return;
    }
    if (s.command == 'u' && priv)
    {
        _reply("\x1b[?" + QByteArray::number(_kittyFlags) + 'u');
        return;
    }
    switch (s.command)
    {
        case 'A':
            _move(_row - n, _column);
            break;
        case 'B':
        case 'e':
            _move(_row + n, _column);
            break;
        case 'C':
        case 'a':
            _move(_row, _column + n);
            break;
        case 'D':
            _move(_row, _column - n);
            break;
        case 'E':
            _move(_row + n, 0);
            break;
        case 'F':
            _move(_row - n, 0);
            break;
        case 'G':
        case '`':
            _move(_row, n - 1);
            break;
        case 'd':
            _move(origin + n - 1, _column);
            break;
        case 'H':
        case 'f':
            _move(origin + n - 1, arg(1) - 1);
            break;
        case 'r':
            if (s.privateMarkers.isEmpty())
            {
                int top    = n - 1;
                int bottom = arg(1, _rows) - 1;
                if (top >= 0 && bottom < _rows && top < bottom)
                {
                    _top    = top;
                    _bottom = bottom;
                    _move(mode(6) ? _top : 0, 0);
                }
            }
            break;
        case 's':
            _save();
            break;
        case 'u':
            _restore();
            break;
        case 'q':
            if (s.intermediates == " ")
            {
                _cursorShape = std::clamp(raw(0), 0, 6);
                if (_cursorShape == 0 || _cursorShape == 1 || _cursorShape == 3 || _cursorShape == 5)
                {
                    _privateModes.insert(12);
                }
                else
                {
                    _privateModes.remove(12);
                }
            }
            break;
        case 'g':
            if (raw(0) == 3)
            {
                _tabs.clear();
            }
            else
            {
                _tabs.remove(_column);
            }
            break;
        case 'I':
            for (int i = 0; i < std::min(n, _columns); ++i)
            {
                int c = _column + 1;
                while (c < _columns - 1 && !_tabs.contains(c))
                {
                    ++c;
                }
                _column = std::min(c, _columns - 1);
            }
            _wrap = false;
            break;
        case 'Z':
            for (int i = 0; i < std::min(n, _columns); ++i)
            {
                int c = _column - 1;
                while (c > 0 && !_tabs.contains(c))
                {
                    --c;
                }
                _column = std::max(c, 0);
            }
            _wrap = false;
            break;
        case 'J':
        {
            int m = raw(0);
            if (m == 3)
            {
                int count = _screenTop();
                _lines.remove(0, count);
                if (!_alternate)
                {
                    _discardedLines += count;
                }
            }
            else if (m <= 2)
            {
                for (int r = 0; r < _rows; ++r)
                {
                    if (m == 2 || (m == 0 && r > _row) || (m == 1 && r < _row))
                    {
                        _erase(r, 0, _columns, priv);
                    }
                    else if (r == _row)
                    {
                        _erase(r, m == 0 ? _column : 0, m == 1 ? _column + 1 : _columns, priv);
                    }
                }
            }
            break;
        }
        case 'K':
        {
            int m = raw(0);
            if (m <= 2)
            {
                _erase(_row, m == 0 ? _column : 0, m == 1 ? _column + 1 : _columns, priv);
            }
            break;
        }
        case 'X':
            _erase(_row, _column, std::min(_columns, _column + n));
            break;
        case '@':
        case 'P':
        {
            auto& line = _lines[cursorLine()];
            n          = std::min(n, _columns - _column);
            if (s.command == '@')
            {
                for (int c = _columns - 1; c >= _column + n; --c)
                {
                    line.cells[c] = line.cells[c - n];
                }
                for (int c = _column; c < _column + n; ++c)
                {
                    line.cells[c] = _blank();
                }
            }
            else
            {
                for (int c = _column; c < _columns - n; ++c)
                {
                    line.cells[c] = line.cells[c + n];
                }
                for (int c = _columns - n; c < _columns; ++c)
                {
                    line.cells[c] = _blank();
                }
            }
            _repair(line);
            _touch(_row);
            break;
        }
        case 'L':
        case 'M':
            if (_row >= _top && _row <= _bottom)
            {
                int top = _top;
                _top    = _row;
                _scroll(n, s.command == 'L');
                _top = top;
            }
            break;
        case 'S':
            _scroll(n);
            break;
        case 'T':
            _scroll(n, true);
            break;
        case 'b':
            for (int i = 0; i < std::min(n, 100000); ++i)
            {
                const auto chars = _lastPrinted.toUcs4();
                for (char32_t c : chars)
                {
                    _put(c);
                }
            }
            break;
        case 'c':
            if (s.privateMarkers == ">")
            {
                _reply("\x1b[>0;1;0c");
            }
            else if (s.privateMarkers.isEmpty())
            {
                _reply("\x1b[?62;4;6;22c");
            }
            break;
        case 'n':
            if (n == 5)
            {
                _reply("\x1b[0n");
            }
            else if (n == 6)
            {
                _reply("\x1b[" + QByteArray(priv ? "?" : "") + QByteArray::number(_row - origin + 1) + ';'
                       + QByteArray::number(_column + 1) + 'R');
            }
            break;
        case 't':
            if (n == 18)
            {
                _reply("\x1b[8;" + QByteArray::number(_rows) + ';' + QByteArray::number(_columns) + 't');
            }
            break;
        default:
            break;
    }
}
void TerminalScreen::_sgr(const VtParser::Sequence& s)
{
    auto params = s.parameters;
    if (params.isEmpty())
    {
        params.append({0});
    }
    for (int i = 0; i < params.size(); ++i)
    {
        const auto p = params[i];
        int        n = p.isEmpty() ? 0 : std::max(0, p[0]);
        if (n == 0)
        {
            const QString hyperlink     = _style.hyperlink;
            const bool    protectedCell = _style.protectedCell;
            _resetStyle();
            _style.hyperlink     = hyperlink;
            _style.protectedCell = protectedCell;
        }
        else if (n == 1)
        {
            _style.attributes |= Bold;
        }
        else if (n == 2)
        {
            _style.attributes |= Faint;
        }
        else if (n == 3)
        {
            _style.attributes |= Italic;
        }
        else if (n == 4 || n == 21)
        {
            int u                 = n == 21 ? 2 : p.value(1, 1);
            _style.underlineStyle = static_cast<UnderlineStyle>(std::clamp(u, 0, 5));
            if (u)
            {
                _style.attributes |= Underline;
            }
            else
            {
                _style.attributes &= ~Underline;
            }
        }
        else if (n == 5 || n == 6)
        {
            _style.attributes |= Blink;
        }
        else if (n == 7)
        {
            _style.attributes |= Inverse;
        }
        else if (n == 8)
        {
            _style.attributes |= Invisible;
        }
        else if (n == 9)
        {
            _style.attributes |= Strike;
        }
        else if (n == 22)
        {
            _style.attributes &= ~(Bold | Faint);
        }
        else if (n == 23)
        {
            _style.attributes &= ~Italic;
        }
        else if (n == 24)
        {
            _style.attributes     &= ~Underline;
            _style.underlineStyle  = UnderlineStyle::None;
        }
        else if (n == 25)
        {
            _style.attributes &= ~Blink;
        }
        else if (n == 27)
        {
            _style.attributes &= ~Inverse;
        }
        else if (n == 28)
        {
            _style.attributes &= ~Invisible;
        }
        else if (n == 29)
        {
            _style.attributes &= ~Strike;
        }
        else if (n == 53)
        {
            _style.attributes |= Overline;
        }
        else if (n == 55)
        {
            _style.attributes &= ~Overline;
        }
        else if (n == 39)
        {
            _style.foreground        = _defaultForeground;
            _style.defaultForeground = true;
            _style.foregroundIndex   = -1;
        }
        else if (n == 49)
        {
            _style.background        = _defaultBackground;
            _style.defaultBackground = true;
            _style.backgroundIndex   = -1;
        }
        else if (n == 59)
        {
            _style.underlineColor = QColor();
        }
        else if ((n >= 30 && n <= 37) || (n >= 90 && n <= 97))
        {
            _style.foregroundIndex   = n >= 90 ? n - 82 : n - 30;
            _style.foreground        = _palette[_style.foregroundIndex];
            _style.defaultForeground = false;
        }
        else if ((n >= 40 && n <= 47) || (n >= 100 && n <= 107))
        {
            _style.backgroundIndex   = n >= 100 ? n - 92 : n - 40;
            _style.background        = _palette[_style.backgroundIndex];
            _style.defaultBackground = false;
        }
        else if (n == 38 || n == 48 || n == 58)
        {
            QVector<int> values;
            if (p.size() > 1)
            {
                values = p.mid(1);
            }
            else if (i + 1 < params.size())
            {
                int kind = params[++i].value(0, -1);
                values.append(kind);
                int count = kind == 2 ? 3 : kind == 5 ? 1 : 0;
                for (int j = 0; j < count && i + 1 < params.size(); ++j)
                {
                    values.append(params[++i].value(0, -1));
                }
            }
            QColor color;
            int    colorIndex = -1;
            if (values.value(0) == 5 && values.size() >= 2)
            {
                colorIndex = std::clamp(values[1], 0, 255);
                color      = _palette[colorIndex];
            }
            else if (values.value(0) == 2 && values.size() >= 4)
            {
                int at = values.size() - 3;
                color  = QColor(std::clamp(values[at], 0, 255),
                                std::clamp(values[at + 1], 0, 255),
                                std::clamp(values[at + 2], 0, 255));
            }
            if (color.isValid())
            {
                if (n == 38)
                {
                    _style.foregroundIndex   = colorIndex;
                    _style.foreground        = color;
                    _style.defaultForeground = false;
                }
                else if (n == 48)
                {
                    _style.backgroundIndex   = colorIndex;
                    _style.background        = color;
                    _style.defaultBackground = false;
                }
                else
                {
                    _style.underlineColor = color;
                }
            }
        }
    }
}
void TerminalScreen::_osc(const QByteArray& data)
{
    int  split   = data.indexOf(';');
    bool ok      = false;
    int  command = (split < 0 ? data : data.first(split)).toInt(&ok);
    if (!ok)
    {
        return;
    }
    const QByteArray payload = split < 0 ? QByteArray() : data.mid(split + 1);
    if (command == 0 || command == 2)
    {
        _title = QString::fromUtf8(payload).left(4096);
    }
    else if (command == 7)
    {
        _directory = QString::fromUtf8(payload).left(4096);
    }
    else if (command == 8)
    {
        int separator = payload.indexOf(';');
        if (separator >= 0)
        {
            _style.hyperlink = QString::fromUtf8(payload.mid(separator + 1)).left(4096);
        }
    }
    else if (command == 10 || command == 11)
    {
        QColor& current = command == 10 ? _defaultForeground : _defaultBackground;
        if (payload == "?")
        {
            _reply("\x1b]" + QByteArray::number(command) + ';' + colorReply(current) + "\x1b\\");
        }
        else
        {
            QColor color = parseColor(payload);
            if (color.isValid() && color != current)
            {
                current = color;
                _recolor();
            }
        }
    }
    else if (command == 110 || command == 111)
    {
        if (!payload.isEmpty())
        {
            return;
        }
        if (command == 110)
        {
            _defaultForeground = _configuredForeground;
        }
        else
        {
            _defaultBackground = _configuredBackground;
        }
        _recolor();
    }
    else if (command == 4)
    {
        const auto parts   = payload.split(';');
        bool       changed = false;
        for (int i = 0; i + 1 < parts.size() && i < 512; i += 2)
        {
            bool valid = false;
            int  index = parts[i].toInt(&valid);
            if (!valid || index < 0 || index >= 256)
            {
                continue;
            }
            if (parts[i + 1] == "?")
            {
                _reply("\x1b]4;" + QByteArray::number(index) + ';' + colorReply(_palette[index]) + "\x1b\\");
            }
            else
            {
                QColor color = parseColor(parts[i + 1]);
                if (color.isValid() && color != _palette[index])
                {
                    _palette[index] = color;
                    changed         = true;
                }
            }
        }
        if (changed)
        {
            _recolor();
        }
    }
    else if (command == 104)
    {
        if (payload.isEmpty())
        {
            for (int index = 0; index < 256; ++index)
            {
                _palette[index] = palette(index);
            }
        }
        else
        {
            const auto parts = payload.split(';');
            for (int i = 0; i < parts.size() && i < 256; ++i)
            {
                bool valid = false;
                int  index = parts[i].toInt(&valid);
                if (valid && index >= 0 && index < 256)
                {
                    _palette[index] = palette(index);
                }
            }
        }
        _recolor();
    }
}

void TerminalScreen::echo(const QByteArray& bytes)
{
    for (char byte : bytes)
    {
        unsigned char value = static_cast<unsigned char>(byte);
        if (value < 32 || value == 127)
        {
            for (char32_t scalar : _echoDecoder.flushIncomplete().toUcs4())
            {
                _put(scalar);
            }
            if (byte == '\r')
            {
                _column = 0;
                _wrap   = false;
            }
            else if (byte == '\n')
            {
                _lineFeed();
            }
            else if (byte == '\b')
            {
                _column = std::max(0, _column - 1);
                _wrap   = false;
            }
            else if (byte == '\t')
            {
                int c = _column + 1;
                while (c < _columns - 1 && !_tabs.contains(c))
                {
                    ++c;
                }
                _column = std::min(c, _columns - 1);
                _wrap   = false;
            }
            continue;
        }
        const QString decoded = _echoDecoder.decode(QByteArrayView(&byte, 1));
        for (char32_t scalar : decoded.toUcs4())
        {
            _put(scalar);
        }
    }
}

void TerminalScreen::advanceGraphics(int columns, int rows)
{
    rows    = std::clamp(rows, 0, _rows + _history);
    columns = std::clamp(columns, 0, _columns);
    _wrap   = false;
    for (int r = 0; r < rows; ++r)
    {
        _lineFeed();
    }
    _column = std::min(_columns - 1, _column + columns);
}
