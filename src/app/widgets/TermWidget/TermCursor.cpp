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
    emit positionChanged();
}

void TermCursor::moveTo(int line, int col)
{
    auto activeLines = _termModel->activeLines();
    _position.setY(std::clamp(line + activeLines.first, activeLines.first, activeLines.second));
    _position.setX(std::max(0, col));
    emit positionChanged();
}

void TermCursor::moveToLine(int line)
{
    auto activeLines = _termModel->activeLines();
    _position.setY(std::clamp(line + activeLines.first, activeLines.first, activeLines.second));
    emit positionChanged();
}

void TermCursor::moveToColumn(int col)
{
    _position.setX(std::max(0, col));
    emit positionChanged();
}

void TermCursor::moveToNextLine()
{
    auto activeLines = _termModel->activeLines();
    _position.setY(std::clamp(_position.y() + 1, activeLines.first, activeLines.second));
    _position.setX(0);
    emit positionChanged();
}

void TermCursor::moveToPrevLine()
{
    auto activeLines = _termModel->activeLines();
    _position.setY(std::clamp(_position.y() - 1, activeLines.first, activeLines.second));
    _position.setX(0);
    emit positionChanged();
}

void TermCursor::lineFeed()
{
    auto activeLines = _termModel->activeLines();

    if (++_position.ry() >= activeLines.second)
    {
        _termModel->_lines.resize(_termModel->_lines.count() + 1);
    }
    emit positionChanged();
}

void TermCursor::carriageReturn()
{
    moveToColumn(0);
    emit positionChanged();
}

void TermCursor::tabulate()
{
    auto spaces = _termModel->tabulateSpaces(_position.x());

    if (spaces > 0)
    {
        _position.rx() += spaces;
    }
    emit positionChanged();
}

void TermCursor::setAttrsFlags(TermAttribute::Attributes flags)
{
    _attrs.setAttrs(flags);
}

void TermCursor::clrAttrsFlags(TermAttribute::Attributes flags)
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
    _preserveLines(_position);
    auto& line = _termModel->_lines[_position.y()];
    line[_position.x()].set(ch.unicode(), _attrs);
    _termModel->update();
}

void TermCursor::addChar(QChar ch)
{
    _preserveLines(_position + QPoint(1, 0));
    auto& line = _termModel->_lines[_position.y()];
    line[_position.rx()++].set(ch.unicode(), _attrs);
    emit positionChanged();
}

void TermCursor::addString(QString str)
{
    addString(str, _attrs);
}

void TermCursor::addString(QString str, TermAttribute attrs)
{
    _preserveLines(QPoint(_position.x() + str.length(), _position.y()));
    auto& line = _termModel->_lines[_position.y()];

    for (const auto& ch : str)
    {
        line[_position.rx()++].set(ch.unicode(), attrs);
    }
    emit positionChanged();
}

void TermCursor::_preserveLines(QPoint desiredCursorPosition)
{
    auto& lines = _termModel->_lines;

    if (lines.count() <= desiredCursorPosition.y())
    {
        lines.resize(desiredCursorPosition.y() + 1);
    }

    auto& line = lines[desiredCursorPosition.y()];

    if (line.length() <= desiredCursorPosition.x())
    {
        auto currLen = line.length();
        line.resize(desiredCursorPosition.x() + 1);
        while (currLen < line.length())
        {
            line[currLen++] = QChar::Space;
        }
    }
}
