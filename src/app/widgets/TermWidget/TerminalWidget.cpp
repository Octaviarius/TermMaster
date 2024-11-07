#include "TerminalWidget.h"

#include <QApplication>
#include <QPainter>
#include <QResizeEvent>

const TerminalColorMap defaultTerminalColormap = ([]() {
    TerminalColorMap ret;

    int r, g, b;

    for (int colorIndex = 0; colorIndex < 256; colorIndex++)
    {
        if (colorIndex < 16)
        {
            // Системные цвета (0-15)
            static const int basicColors[16][3] = {
                {  0,   0,   0},
                {128,   0,   0},
                {  0, 128,   0},
                {128, 128,   0},
                {  0,   0, 128},
                {128,   0, 128},
                {  0, 128, 128},
                {192, 192, 192},
                {128, 128, 128},
                {255,   0,   0},
                {  0, 255,   0},
                {255, 255,   0},
                {  0,   0, 255},
                {255,   0, 255},
                {  0, 255, 255},
                {255, 255, 255}
            };

            r = basicColors[colorIndex][0];
            g = basicColors[colorIndex][1];
            b = basicColors[colorIndex][2];
        }
        else if (colorIndex >= 16 && colorIndex <= 231)
        {
            // Цвета 6x6x6 куба (16-231)
            int index      = colorIndex - 16;
            int redLevel   = index / 36;
            int greenLevel = (index / 6) % 6;
            int blueLevel  = index % 6;

            r = 55 * redLevel + 40;
            g = 55 * greenLevel + 40;
            b = 55 * blueLevel + 40;
        }
        else if (colorIndex >= 232 && colorIndex <= 255)
        {
            // Градации серого (232-255)
            int grayLevel = colorIndex - 232;
            r = g = b = 8 + 10 * grayLevel;
        }

        ret[colorIndex] = QColor(r, g, b);
    }

    return ret;
})();

//-----------------------------------------------------------------------------------

const TerminalChar TerminalChar::filler;

//-----------------------------------------------------------------------------------

TerminalWidget::TerminalWidget(QWidget* parent) : QWidget(parent)
{
    setTerminalFont(QFont("Courier New", 14));
}

void TerminalWidget::setTerminalFont(QFont font)
{
    _font        = font;
    auto metrics = QFontMetrics(font);
    _symSize     = QSize(metrics.maxWidth(), metrics.height());

    _symDescent = metrics.descent();

    _updateSize(size());
}

QSize TerminalWidget::preferredSize()
{
    return _preferredWidgetSize;
}

void TerminalWidget::eraseAll()
{
    for (size_t p = _activeLineFirst; p <= _activeLineLast; p++)
    {
        _lines[p].chars.fill(TerminalChar::filler);
    }
}

void TerminalWidget::eraseAbove()
{
    for (size_t p = _activeLineFirst; p <= _cursor.linePos; p++)
    {
        _lines[p].chars.fill(TerminalChar::filler);
    }
}

void TerminalWidget::eraseBelow()
{
    for (size_t p = _cursor.linePos; p <= _activeLineLast; p++)
    {
        _lines[p].chars.fill(TerminalChar::filler);
    }
}

void TerminalWidget::eraseLineAfter()
{
    std::fill_n(&_lines[_cursor.linePos].chars[_cursor.columnPos],
                _termSize.width() - 1 - _cursor.columnPos,
                TerminalChar::filler);
}

void TerminalWidget::eraseLineBefore()
{
    std::fill_n(&_lines[_cursor.linePos].chars[0], _cursor.columnPos, TerminalChar::filler);
}

void TerminalWidget::eraseLine()
{
    std::fill_n(&_lines[_cursor.linePos].chars[0], _termSize.width() - 1, TerminalChar::filler);
}

void TerminalWidget::setAttrs(TextAttributes flags)
{
    _cursor.attrs.textAttrs |= flags;
}

void TerminalWidget::clrAttrs(TextAttributes flags)
{
    _cursor.attrs.textAttrs &= ~flags;
}

void TerminalWidget::setForeground(uint8_t index)
{
    _cursor.attrs.foreground = index;
    setAttrs(TextAttribute::Foreground);
}

void TerminalWidget::clrForeground()
{
    clrAttrs(TextAttribute::Foreground);
}

void TerminalWidget::setBackground(uint8_t index)
{
    _cursor.attrs.background = index;
    setAttrs(TextAttribute::Background);
}

void TerminalWidget::clrBackground()
{
    clrAttrs(TextAttribute::Background);
}

void TerminalWidget::setCursorPosition(int line, int col)
{
}

void TerminalWidget::setCursorPositionLine(int line)
{
}

void TerminalWidget::setCursorPositionCol(int col)
{
}

void TerminalWidget::shiftCursorPosition(int line, int col)
{
}

void TerminalWidget::setColorMap(TerminalColorMap colormap)
{
    _colormap = colormap;
}

void TerminalWidget::putCharacter(QChar ch)
{
    auto& line = _lines[_cursor.linePos];

    if (line.chars.count() <= _cursor.columnPos)
    {
        line.chars.resize(_cursor.columnPos + 1);
    }

    line.chars[_cursor.columnPos].set(ch.unicode(), _cursor.attrs);
}

void TerminalWidget::deleteCharacter()
{
}

void TerminalWidget::resizeEvent(QResizeEvent* event)
{
    _updateSize(event->size());
}

void TerminalWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    auto termRect = QRect(_scrollColPos, _scrollLinePos, _termSize.width(), _termSize.height());

    TerminalCharAttribute attrs = {0, 0, 0};

    for (size_t l = termRect.top(); l <= termRect.bottom(); l++)
    {
        QString lineText;

        auto  lastCur = QPoint(0, l);
        auto& line    = _lines[l];
        for (size_t c = termRect.left(); c <= termRect.right(); c++)
        {
            // draw text
            if (attrs != line.chars[c].attrs)
            {
                if (lineText.length() > 0)
                {
                    _drawText(painter, lastCur.y(), lastCur.x(), attrs, lineText);
                    lineText.resize(0);
                    lastCur.rx() = c;
                    lastCur.ry() = l;
                }

                // update attributes
                attrs = line.chars[c].attrs;
            }

            // fix unclear characters
            auto ch   = QChar(line.chars[c].symbol);
            lineText += ch.unicode() >= QChar::Space ? ch : QChar::Space;

            // draw text in the end
            if (lineText.length() > 0 && c == termRect.right())
            {
                _drawText(painter, lastCur.y(), lastCur.x(), attrs, lineText);
                lineText.resize(0);
            }
        }
    }
}

size_t TerminalWidget::_tabSpaces(size_t col)
{
    return divCeil(col + 1, _tabSize) * _tabSize - col;
}

void TerminalWidget::_updateSize(QSize size)
{
    auto diffSize = abs(size - _preferredWidgetSize);

    if (diffSize.width() > _symSize.width() / 2 || diffSize.height() > _symSize.height() / 2)
    {
        _termSize = QSize((size + _symSize / 2) / _symSize);
        _termSize = QSize(std::max(_termSize.width(), 1), std::max(_termSize.height(), 1));

        _preferredWidgetSize = _termSize * _symSize;
        qDebug() << QString("TermSize %1x%2").arg(_termSize.width()).arg(_termSize.height());
        _onTerminalResize();
    }

    resize(_preferredWidgetSize);
}

void TerminalWidget::_onTerminalResize()
{
    if (_termSize.height() > _lines.count())
    {
        _lines.resize(_termSize.height());
    }

    _activeLineLast  = _lines.count() - 1;
    _activeLineFirst = _activeLineLast - (_termSize.height() - 1);

    for (size_t l = _activeLineFirst; l <= _activeLineLast; l++)
    {
        if (_lines[l].length() < _termSize.width())
        {
            _lines[l].resize(_termSize.width());
        }
    }

    _setCursorPos(_cursor.linePos, _cursor.columnPos);

    qDebug() << QString("Active lines %1, total lines %2, [%3:%4]")
                    .arg(_activeLineLast - _activeLineFirst)
                    .arg(_lines.count())
                    .arg(_activeLineFirst)
                    .arg(_activeLineLast);
}

void TerminalWidget::_setCursorPos(size_t line, size_t col)
{
    _cursor.linePos   = std::clamp(line, _activeLineFirst, _activeLineLast);
    _cursor.columnPos = std::min<size_t>(col, _termSize.width());

    qDebug() << QString("Curor %1:%2").arg(_cursor.linePos).arg(_cursor.columnPos);
}

void TerminalWidget::_drawText(QPainter& painter, size_t line, size_t col, TerminalCharAttribute attrs, QString text)
{

    // determine backgound color
    QColor background = attrs.hasBackground() ? _colormap[attrs.background] : _defaultBackground;

    // determine foregound color
    QColor foreground = attrs.hasForeground() ? _colormap[attrs.foreground] : _defaultForeground;

    // set font attributes
    _font.setBold(attrs.isBold());
    _font.setItalic(attrs.isItalic());
    _font.setStrikeOut(attrs.isStrikeout());
    _font.setUnderline(attrs.isUnderline());

    painter.setFont(_font);

    // if text is hidden - we mustn't print it
    if (attrs.isHidden())
    {
        text.fill(' ');
    }

    // if text inversed - foreground and background are swapped
    QColor backColor;

    if (attrs.isInversed())
    {
        backColor = foreground;
        painter.setPen(background);
    }
    else
    {
        backColor = background;
        painter.setPen(foreground);
    }

    painter.setBrush(backColor);

    // painter.drawRect(
    //     QRect(_symSize.width() * col, _symSize.height() * line, _symSize.width() * text.length(),
    //     _symSize.height()));

    painter.fillRect(QRect(_symSize.width() * col,
                           _symSize.height() * line,
                           _symSize.width() * text.length(),
                           _symSize.height()),
                     backColor);

    painter.drawText(QPoint(_symSize.width() * col, _symSize.height() * line + _symSize.height() - _symDescent), text);
}
