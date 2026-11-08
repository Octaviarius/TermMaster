#pragma once
#include <QColor>
#include <QStringDecoder>
#include <QVector>

/** Bounded stream model implementing a small ECMA-48 subset, not a VT emulator.
 * Columns count Unicode scalar values; CJK double-width and complex grid editing
 * are intentionally unsupported. The owner must use this model on one thread.
 */
class TerminalScreen
{
public:
    struct Cell
    {
        QString text;
        QColor  foreground;
        QColor  background;
    };
    struct Line
    {
        QVector<Cell> cells;
        quint64       revision = 0;
    };
    TerminalScreen();
    void                 receive(const QByteArray& bytes);
    void                 clear();
    void                 setLimits(int columns, int rows, int history);
    void                 setColors(const QColor& foreground, const QColor& background);
    const QVector<Line>& lines() const;
    int                  cursorLine() const;
    int                  cursorColumn() const;
    quint64              discardedLines() const;
    QString              text() const;

private:
    enum class State
    {
        Text,
        Escape,
        Csi,
        Osc,
        OscEscape
    };
    QVector<Line>  _lines;
    QStringDecoder _decoder {QStringDecoder::Utf8};
    State          _state = State::Text;
    QByteArray     _sequence;
    QColor         _foreground {QStringLiteral("#e5e7eb")};
    QColor         _background {QStringLiteral("#15191f")};
    QColor         _defaultForeground = _foreground;
    QColor         _defaultBackground = _background;
    int            _column            = 0;
    int            _line              = 0;
    int            _columns           = 240;
    int            _rows              = 24;
    int            _history           = 10000;
    quint64        _revision          = 0;
    quint64        _discardedLines    = 0;
    void           _put(const QString& character);
    void           _newline();
    void           _trim();
    void           _csi(char command);
};
