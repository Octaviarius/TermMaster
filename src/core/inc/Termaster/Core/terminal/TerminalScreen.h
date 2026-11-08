#pragma once
#include <QColor>
#include <QSet>
#include <QVector>
#include <Termaster/Core/terminal/VtParser.h>
#include <functional>

/** Bounded VT grid. All access, including extension callbacks, belongs to the owner's thread. */
class TerminalScreen
{
public:
    enum Attribute : quint32
    {
        Bold      = 1,
        Faint     = 2,
        Italic    = 4,
        Underline = 8,
        Blink     = 16,
        Inverse   = 32,
        Invisible = 64,
        Strike    = 128,
        Overline  = 256
    };
    enum class UnderlineStyle
    {
        None,
        Single,
        Double,
        Curly,
        Dotted,
        Dashed
    };
    static constexpr int maximumCells = 2 * 1024 * 1024;
    using ReplyHandler                = std::function<void(const QByteArray&)>;
    struct Cell
    {
        int            foregroundIndex = -1;
        int            backgroundIndex = -1;
        QString        text;
        QColor         foreground;
        QColor         background;
        quint32        attributes    = 0;
        bool           protectedCell = false;
        int            width         = 1;
        QString        hyperlink;
        bool           defaultForeground = true;
        bool           defaultBackground = true;
        QColor         underlineColor;
        UnderlineStyle underlineStyle = UnderlineStyle::None;
    };
    struct Line
    {
        QVector<Cell> cells;
        quint64       revision = 0;
    };
    TerminalScreen();
    Q_DISABLE_COPY_MOVE(TerminalScreen)
    /** Echoes printable UTF-8 and basic spacing controls without protocol side effects. */
    void echo(const QByteArray& bytes);
    /** Advances after graphics delivery without reentering the stream parser. */
    void                 advanceGraphics(int columns, int rows);
    void                 receive(const QByteArray& bytes);
    void                 clear();
    void                 setLimits(int columns, int rows, int history);
    void                 setColors(const QColor& foreground, const QColor& background);
    const QVector<Line>& lines() const;
    int                  cursorLine() const;
    int                  cursorColumn() const;
    quint64              discardedLines() const;
    quint64              resetRevision() const;
    QString              text() const;
    bool                 alternateScreen() const;
    bool                 cursorVisible() const;
    int                  cursorShape() const;
    bool                 mode(int number, bool privateMode = true) const;
    bool                 bracketedPaste() const;
    bool                 applicationCursorKeys() const;
    bool                 applicationKeypad() const;
    bool                 focusReporting() const;
    int                  mouseTracking() const;
    int                  mouseEncoding() const;
    int                  kittyKeyboardFlags() const;
    int                  modifyOtherKeys() const;
    QString              title() const;
    QString              currentDirectory() const;
    QByteArray           takeOutput();
    /** Receives future replies synchronously. Bytes are borrowed; the handler must not reenter the model. */
    void setReplyHandler(ReplyHandler handler);
    /** Callback receives a borrowed sequence synchronously; it must not retain references. */
    void setExtensionHandler(std::function<void(const VtParser::Sequence&)> handler);

private:
    class Decoder
    {
    public:
        QString decode(QByteArrayView bytes);
        QString flushIncomplete();
        void    resetState();

    private:
        char32_t _codepoint   = 0;
        int      _remaining   = 0;
        int      _nextMinimum = 0x80;
        int      _nextMaximum = 0xbf;
        bool     _atStart     = true;
        void     _append(QString& result, char32_t scalar);
    };
    struct Saved
    {
        int  row    = 0;
        int  column = 0;
        Cell style;
        bool origin = false;
        bool wrap   = false;
    };
    QVector<Line>                                  _lines;
    QVector<Line>                                  _primary;
    Decoder                                        _decoder;
    Decoder                                        _echoDecoder;
    VtParser                                       _parser;
    Cell                                           _style;
    QVector<QColor>                                _palette;
    QColor                                         _configuredForeground {QStringLiteral("#e5e7eb")};
    QColor                                         _configuredBackground {QStringLiteral("#15191f")};
    QColor                                         _defaultForeground {QStringLiteral("#e5e7eb")};
    QColor                                         _defaultBackground {QStringLiteral("#15191f")};
    QVector<int>                                   _kittyStack;
    QSet<int>                                      _privateModes;
    QSet<int>                                      _modes;
    QSet<int>                                      _tabs;
    Saved                                          _saved;
    Saved                                          _primarySaved;
    QByteArray                                     _output;
    QString                                        _title;
    QString                                        _directory;
    QString                                        _lastPrinted;
    ReplyHandler                                   _replyHandler;
    std::function<void(const VtParser::Sequence&)> _extensionHandler;
    int                                            _column          = 0;
    int                                            _row             = 0;
    int                                            _columns         = 240;
    int                                            _rows            = 24;
    int                                            _history         = 10000;
    int                                            _top             = 0;
    int                                            _bottom          = 23;
    int                                            _cursorShape     = 0;
    int                                            _modifyOtherKeys = 0;
    int                                            _kittyFlags      = 0;
    int                                            _charset         = 0;
    bool                                           _graphics[2]     = {false, false};
    bool                                           _alternate       = false;
    bool                                           _wrap            = false;
    bool                                           _keypad          = false;
    bool                                           _joinNext        = false;
    bool                                           _vt52            = false;
    int                                            _vt52Address     = -1;
    int                                            _vt52Row         = 0;
    quint64                                        _resetRevision   = 0;
    quint64                                        _revision        = 0;
    quint64                                        _discardedLines  = 0;
    int                                            _screenTop() const;
    Cell                                           _blank() const;
    Line                                           _blankLine();
    void                                           _resetStyle();
    void                                           _recolor();
    void                                           _trim();
    void                                           _touch(int row);
    void                                           _repair(Line& line);
    void                                           _put(char32_t scalar);
    void                                           _lineFeed();
    void                                           _scroll(int count, bool down = false);
    void                                           _move(int row, int column);
    void                                           _erase(int row, int first, int last, bool selective = false);
    void                                           _save();
    void                                           _restore();
    void                                           _setMode(int number, bool enabled, bool priv);
    void                                           _reply(const QByteArray& bytes);
    void                                           _sequence(const VtParser::Sequence& sequence);
    void                                           _csi(const VtParser::Sequence& sequence);
    void                                           _sgr(const VtParser::Sequence& sequence);
    void                                           _osc(const QByteArray& data);
};
