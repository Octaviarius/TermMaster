#ifndef TERMINALWIDGET_H
#define TERMINALWIDGET_H

#include <QEvent>
#include <QWidget>
#include <array>
#include <core/utils.h>

#define TERMINAL_WIDGET_MAX_TAB_SIZE (128)

using TerminalColorMap = std::array<QColor, 256>;

enum class TextAttribute
{
    None = 0,

    Bold      = (1 << 0),
    Faint     = (1 << 1),
    Italic    = (1 << 2),
    Underline = (1 << 3),
    Strikeout = (1 << 4),
    Blink     = (1 << 5),
    FastBlink = (1 << 6),
    Inversed  = (1 << 7),
    Hidden    = (1 << 8),

    Foreground = (1 << 9),
    Background = (1 << 10),
};
Q_DECLARE_FLAGS(TextAttributes, TextAttribute)
Q_DECLARE_OPERATORS_FOR_FLAGS(TextAttributes);

extern const TerminalColorMap defaultTerminalColormap;

struct TerminalCharAttribute
{
    uint8_t  foreground = 0;
    uint8_t  background = 0;
    uint16_t textAttrs  = 0;

    constexpr bool operator==(const TerminalCharAttribute& other) const
    {
        return other.foreground == foreground && other.background == background && other.textAttrs == textAttrs;
    }

    constexpr bool operator!=(const TerminalCharAttribute& other) const
    {
        return !(other == *this);
    }

    constexpr bool isBold() const
    {
        return textAttrs & (uint16_t)TextAttribute::Bold;
    }

    constexpr bool isFaint() const
    {
        return textAttrs & (uint16_t)TextAttribute::Faint;
    }

    constexpr bool isItalic() const
    {
        return textAttrs & (uint16_t)TextAttribute::Italic;
    }

    constexpr bool isUnderline() const
    {
        return textAttrs & (uint16_t)TextAttribute::Underline;
    }

    constexpr bool isStrikeout() const
    {
        return textAttrs & (uint16_t)TextAttribute::Strikeout;
    }

    constexpr bool isHidden() const
    {
        return textAttrs & (uint16_t)TextAttribute::Hidden;
    }

    constexpr bool isInversed() const
    {
        return textAttrs & (uint16_t)TextAttribute::Inversed;
    }

    constexpr bool isBlink() const
    {
        return textAttrs & (uint16_t)TextAttribute::Blink;
    }

    constexpr bool isFastBlink() const
    {
        return textAttrs & (uint16_t)TextAttribute::FastBlink;
    }

    constexpr bool hasBackground() const
    {
        return textAttrs & (uint16_t)TextAttribute::Background;
    }
    constexpr bool hasForeground() const
    {
        return textAttrs & (uint16_t)TextAttribute::Foreground;
    }

    constexpr bool hasNothing() const
    {
        return textAttrs == (uint16_t)TextAttribute::None;
    }
};

struct TerminalChar
{
    uint32_t symbol;

    TerminalCharAttribute attrs;

    TerminalChar() : symbol(0), attrs {0, 0, 0}
    {
    }

    TerminalChar(uint32_t symbol) : symbol(symbol), attrs {0, 0, 0}
    {
    }

    TerminalChar(uint32_t symbol, TerminalCharAttribute attrs) : symbol(symbol), attrs(attrs)
    {
    }

    void set(uint32_t symbol, TerminalCharAttribute attrs)
    {
        this->symbol = symbol;
        this->attrs  = attrs;
    }

    static const TerminalChar filler;
};

struct TerminalLine
{
    QList<TerminalChar> chars;

    void resize(int size)
    {
        chars.resize(size);
    }

    size_t length() const
    {
        return chars.count();
    }
};

struct TerminalCursor
{
    enum class Style
    {
        Line,
        Round,
        Underline,
    };

    size_t linePos   = 0;
    size_t columnPos = 0;

    TerminalCharAttribute attrs;
    Style                 style = Style::Round;
};

class TerminalWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget* parent = nullptr);

    void setTerminalFont(QFont font);

    QSize preferredSize();
    void  eraseAll();
    void  eraseAbove();
    void  eraseBelow();
    void  eraseLineAfter();
    void  eraseLineBefore();
    void  eraseLine();
    void  setAttrs(TextAttributes flags);
    void  clrAttrs(TextAttributes flags);

    void setForeground(uint8_t index);
    void clrForeground();
    void setBackground(uint8_t index);
    void clrBackground();

    void setCursorPosition(int line, int col);
    void setCursorPositionLine(int line);
    void setCursorPositionCol(int col);
    void shiftCursorPosition(int line, int col);

    void setColorMap(TerminalColorMap colormap);

    void putCharacter(QChar ch);
    void deleteCharacter();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QSize _termSize;
    QSize _symSize;
    int   _symDescent;
    QSize _preferredWidgetSize;

    QFont            _font;
    TerminalColorMap _colormap = defaultTerminalColormap;

    QColor _defaultForeground = QColor(Qt::white);
    QColor _defaultBackground = QColor(Qt::black);

    size_t _activeLineFirst = 0;
    size_t _activeLineLast  = 0;

    size_t _scrollLinePos = 0;
    size_t _scrollColPos  = 0;

    size_t _tabSize = 4;

    QVector<TerminalLine> _lines;

    TerminalCursor _cursor;

    size_t _tabSpaces(size_t col);

    void _updateSize(QSize size);
    void _onTerminalResize();
    void _setCursorPos(size_t line, size_t col);

    void _drawText(QPainter& painter, size_t line, size_t col, TerminalCharAttribute attrs, QString text);

signals:
    void outputKey(Qt::Key key);
};

#endif // TERMINALWIDGET_H
