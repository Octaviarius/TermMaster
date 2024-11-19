#pragma once
#include "TermChar.h"
#include "TermColormap.h"
#include "TermModel.h"

#include <QEvent>
#include <QTextCharFormat>
#include <QWidget>
#include <core/utils.h>

class TermWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TermWidget(QWidget* parent = nullptr);
    ~TermWidget();

    void setTerminalFont(QFont font);

    QSize preferredSize();

    void setColorMap(TermColorMap colormap);

    TermModel* termModel();
    void       setTermModel(TermModel* model);

    QPoint convPixelToSymbol(QPoint pixel) const;
    QPoint convSymbolToPixel(QPoint symPosition) const;

    void setMinimalTermSize(QSize minSize);

    void setCursorBlinkPeriod(int periodMs);
    void restartCursor();
    void forceCursorNow();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void timerEvent(QTimerEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    TermModel* _termModel;

    QSize _symSize;
    int   _symDescent;
    QSize _preferredWidgetSize;

    QSize _minTermSize;

    QFont        _font;
    TermColorMap _colormap = defaultTermColorMap;

    QColor _defaultForeground = QColor(Qt::white);
    QColor _defaultBackground = QColor(Qt::black);

    size_t _activeLineFirst = 0;
    size_t _activeLineLast  = 0;

    size_t _scrollLinePos = 0;
    size_t _scrollColPos  = 0;

    size_t _tabSize = 4;

    int  _cursorTimerId              = 0;
    int  _cursorBlinkPeriod          = 500;
    bool _cursorBlinkActive          = true;
    bool _cursorBlinkForcedActiveNow = false;

    bool _selectionStarted = false;

    void _updateSize(QSize size);
    void _onTerminalResize();

    void _repaintCursor();

    void            _paintRect(QRect rect);
    QTextCharFormat _attrsToTextCharFormat(TermAttribute attrs);

signals:
    void outputKey(QKeyEvent* event);
};
