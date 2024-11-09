#include "TermCursor.h"

#include "TermModel.h"

QPoint TermCursor::screenPosition() const
{
    return _position - _termModel->scrollPosition();
}

QPoint TermCursor::position() const
{
    return _position;
}

void TermCursor::setStyle(Style style)
{
    _style = style;
    _termModel->update();
}

void TermCursor::update()
{
    _termModel->update();
}

void TermCursor::eraseAll()
{
    auto trect = _termModel->rect();

    for (int l = trect.top(); l < trect.bottom(); l++)
    {
        _termModel->_lines[l].resize(0);
    }
}

void TermCursor::eraseAbove()
{
    auto trect = _termModel->rect();

    for (int l = trect.top(); l < _position.y(); l++)
    {
        _termModel->_lines[l].resize(0);
    }
}

void TermCursor::eraseBelow()
{
    auto trect = _termModel->rect();

    for (int l = _position.y(); l < trect.bottom(); l++)
    {
        _termModel->_lines[l].resize(0);
    }
}

void TermCursor::eraseLineAfter()
{
    auto& line = _termModel->_lines[_position.y()];
    line.resize(_position.y());
}

void TermCursor::eraseLineBefore()
{
    auto& line = _termModel->_lines[_position.y()];
    for (int c = std::min<int>(_position.x(), line.length() - 1); c >= 0; c--)
    {
        line[c].reset();
    }
}

void TermCursor::eraseLine()
{
    auto& line = _termModel->_lines[_position.y()];
    line.resize(0);
}

void TermCursor::shift(int line, int col)
{
    auto activeLines = _termModel->activeLines();
    _position.setY(std::clamp(_position.y() + line, activeLines.first, activeLines.second));
    _position.setX(std::max(0, _position.x() + col));
}

void TermCursor::moveTo(int line, int col)
{
    auto activeLines = _termModel->activeLines();
    _position.setY(std::clamp(line + activeLines.first, activeLines.first, activeLines.second));
    _position.setX(std::max(0, col));
}

void TermCursor::moveToLine(int line)
{
    auto activeLines = _termModel->activeLines();
    _position.setY(std::clamp(line + activeLines.first, activeLines.first, activeLines.second));
}

void TermCursor::moveToColumn(int col)
{
    _position.setX(std::max(0, col));
}

void TermCursor::moveToNextLine()
{
    auto activeLines = _termModel->activeLines();
    _position.setY(std::clamp(_position.y() + 1, activeLines.first, activeLines.second));
    _position.setX(0);
}

void TermCursor::moveToPrevLine()
{
    auto activeLines = _termModel->activeLines();
    _position.setY(std::clamp(_position.y() - 1, activeLines.first, activeLines.second));
    _position.setX(0);
}

void TermCursor::lineFeed()
{
    auto activeLines = _termModel->activeLines();

    if (++_position.ry() >= activeLines.second)
    {
        _termModel->_lines.resize(_termModel->_lines.count() + 1);
    }
}

void TermCursor::carriageReturn()
{
    moveToColumn(0);
}

void TermCursor::tabulate()
{
    auto spaces = _termModel->tabulateSpaces(_position.x());

    if (spaces > 0)
    {
        auto attrs = _attrs;
        attrs.clrAttrs(TermAttribute::Attribute::Strikeout | TermAttribute::Attribute::Underline);
        addString(QString(spaces, QChar::Space), attrs);
    }
}

void TermCursor::setAttrs(TermAttribute::Attributes flags)
{
    _attrs.setAttrs(flags);
}

void TermCursor::clrAttrs(TermAttribute::Attributes flags)
{
    _attrs.clrAttrs(flags);
}

void TermCursor::setForeground(uint8_t index)
{
    _attrs.foreground = index;
    _attrs.setAttrs(TermAttribute::Attribute::Foreground);
}

void TermCursor::clrForeground()
{
    _attrs.clrAttrs(TermAttribute::Attribute::Foreground);
}

void TermCursor::setBackground(uint8_t index)
{
    _attrs.background = index;
    _attrs.setAttrs(TermAttribute::Attribute::Background);
}

void TermCursor::clrBackground()
{
    _attrs.clrAttrs(TermAttribute::Attribute::Background);
}

void TermCursor::replaceChar(QChar ch)
{
    auto& line = _termModel->_lines[_position.y()];
    if (line.length() < _position.x())
    {
        line.resize(_position.x() + 1);
    }

    line[_position.x()].set(ch.unicode(), _attrs);
    _termModel->update();
}

void TermCursor::addChar(QChar ch)
{
    auto& line = _termModel->_lines[_position.y()];
    if (line.length() < _position.x())
    {
        line.resize(_position.x() + 1);
    }

    line[_position.rx()++].set(ch.unicode(), _attrs);
}

void TermCursor::addString(QString str)
{
    addString(str, _attrs);
}

void TermCursor::addString(QString str, TermAttribute attrs)
{
    auto& line = _termModel->_lines[_position.y()];
    if (line.length() < _position.x() + str.length())
    {
        line.resize(_position.x() + str.length() + 1);
    }

    for (const auto& ch : str)
    {
        line[_position.rx()++].set(ch.unicode(), attrs);
    }
}
