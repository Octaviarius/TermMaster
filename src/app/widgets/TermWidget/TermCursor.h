#pragma once

#include "TermChar.h"

#include <QSize>
#include <qpoint.h>
#include <stdint.h>

class TermModel;

class TermCursor
{
public:
    enum class Style : uint8_t
    {
        Line,
        Round,
        Underline,
    };

    TermCursor(TermModel* model) : _termModel(model), _position(0, 0)
    {
    }

    constexpr TermModel* termModel() const
    {
        return _termModel;
    }

    QPoint screenPosition() const;
    QPoint position() const;

    constexpr Style style() const
    {
        return _style;
    }

    void setStyle(Style style);

    constexpr TermAttribute attrs() const
    {
        return _attrs;
    }
    constexpr void setAttrs(TermAttribute attrs)
    {
        _attrs = attrs;
    }

    void update();

    void eraseAll();
    void eraseAbove();
    void eraseBelow();
    void eraseLineAfter();
    void eraseLineBefore();
    void eraseLine();

    void shift(int line, int col);
    void moveTo(int line, int col);
    void moveToLine(int line);
    void moveToColumn(int col);
    void moveToNextLine();
    void moveToPrevLine();
    void lineFeed();
    void carriageReturn();

    void tabulate();

    void setAttrsFlags(TermAttribute::Attributes flags);
    void clrAttrsFlags(TermAttribute::Attributes flags);
    void setForeground(uint8_t index);
    void clrForeground();
    void setBackground(uint8_t index);
    void clrBackground();

    void replaceChar(QChar ch = QChar::Null);
    void addChar(QChar ch);
    void addString(QString str);
    void addString(QString str, TermAttribute attrs);

private:
    TermModel* _termModel;
    QPoint     _position;

    TermAttribute _attrs;
    Style         _style = Style::Round;
};
