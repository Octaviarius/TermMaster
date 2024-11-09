#pragma once

#include "TermChar.h"
#include "TermCursor.h"

#include <QList>
#include <QObject>
#include <QRect>
#include <QSize>

using TermLine = QList<TermChar>;

class TermModel : public QObject
{
    Q_OBJECT
public:
    TermModel(QObject* parent = nullptr);

    friend TermCursor;

    constexpr TermCursor* cursor()
    {
        return &_cursor;
    }

    void update();

    QPair<int, int> activeLines() const
    {
        return QPair<int, int>(std::max(0, (int)_lines.count() - _termSize.height()), _lines.count());
    }

    constexpr QRect rect() const
    {
        return QRect(_scrollPosition.x(), _scrollPosition.y(), _termSize.width(), _termSize.height());
    }

    constexpr QSize size() const
    {
        return _termSize;
    }

    void setSize(QSize size);

    constexpr QPoint scrollPosition() const
    {
        return _scrollPosition;
    }

    constexpr int tabSize() const
    {
        return _tabSize;
    }

    constexpr void setTabSize(int size)
    {
        _tabSize = size;
    }

    int tabulateColumn(int col) const;
    int tabulateSpaces(int col) const;

    void setStartSelection(QPoint start);
    void setStopSelection(QPoint stop);
    void resetSelection();

    QRect selection() const;

    QList<TermLine>        lines(int start, int count);
    const QList<TermLine>& lines();

    void inputData(QString data);

private:
    TermCursor _cursor;

    QList<TermLine> _lines;

    QSize  _termSize;
    QPoint _scrollPosition;

    QPoint _selectionStart;
    QPoint _selectionStop;

    int _tabSize = 4;
signals:
    void updated();
    void resized(QSize oldSize, QSize newSize);
    void linesOut(QList<TermLine> lines);
};
