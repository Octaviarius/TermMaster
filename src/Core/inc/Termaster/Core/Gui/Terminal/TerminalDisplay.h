#pragma once
#include <QAbstractScrollArea>
#include <QCache>
#include <QGlyphRun>
#include <QTextLayout>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <Termaster/Core/Terminal/TerminalGraphics.h>
#include <Termaster/Core/Terminal/TerminalScreen.h>
#include <array>
#include <map>
#include <memory>
#include <vector>

class QPainter;

/** GUI-thread terminal endpoint. Incoming data is supplied after stream processing;
 * outgoing events are returned to the owner before processing/transport. No device ownership.
 */
class TerminalDisplay : public QAbstractScrollArea
{
    Q_OBJECT
public:
    enum class OutputKind
    {
        Keyboard,
        Paste,
        Mouse,
        Focus,
        Reply
    };
    using CommittedLineHandler = std::function<void(const QString&, bool softWrapped)>;
    Q_ENUM(OutputKind)
    explicit TerminalDisplay(QWidget* parent = nullptr);
    ~TerminalDisplay() override;
    void                  receive(const QByteArray& bytes);
    void                  localEcho(const QByteArray& bytes);
    void                  setAppearance(const QVariantMap& appearance);
    void                  clear();
    QString               text() const;
    const TerminalScreen& screen() const;
    QSize                 terminalSize() const;
    int                   historyLineCount() const;
    /** Future immutable primary source rows; borrowed text is valid only during this callback. Do not reenter. */
    void          setCommittedLineHandler(CommittedLineHandler handler);
    Q_SIGNAL void terminalSizeChanged(int columns, int rows);
    Q_SIGNAL void outputBytes(const QByteArray& bytes, OutputKind kind, const QByteArray& echo);
    Q_SIGNAL void selectionChanged();
    Q_SIGNAL void textChanged();
    bool          hasSelection() const;
    bool          hasContent() const;
    Q_SLOT void   copy();
    Q_SLOT void   paste();
    Q_SLOT void   selectAll();
    Q_SIGNAL void titleChanged(const QString& title);
    Q_SIGNAL void linkActivated(const QUrl& url);

protected:
    bool     event(QEvent* event) override;
    bool     focusNextPrevChild(bool next) override;
    void     paintEvent(QPaintEvent* event) override;
    void     resizeEvent(QResizeEvent* event) override;
    void     keyPressEvent(QKeyEvent* event) override;
    void     keyReleaseEvent(QKeyEvent* event) override;
    void     mousePressEvent(QMouseEvent* event) override;
    void     mouseDoubleClickEvent(QMouseEvent* event) override;
    void     mouseMoveEvent(QMouseEvent* event) override;
    void     mouseReleaseEvent(QMouseEvent* event) override;
    void     wheelEvent(QWheelEvent* event) override;
    void     focusInEvent(QFocusEvent* event) override;
    void     focusOutEvent(QFocusEvent* event) override;
    void     inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
    struct ShapeKey
    {
        QString         text;
        QTextCharFormat format;

        bool operator==(const ShapeKey&) const = default;

        friend size_t qHash(const ShapeKey& key, size_t seed = 0)
        {
            return ::qHash(key.text, seed);
        }
    };
    struct Shape
    {
        ShapeKey                     key;
        std::unique_ptr<QTextLayout> layout;
        QList<QGlyphRun>             glyphs;
        bool                         directGlyphs = false;
    };
    struct RasterKey
    {
        ShapeKey shape;
        QRgb     background = 0;
        QSize    size;

        bool operator==(const RasterKey&) const = default;

        friend size_t qHash(const RasterKey& key, size_t seed = 0)
        {
            return qHashMulti(seed, qHash(key.shape), key.background, key.size);
        }
    };
    struct Run
    {
        int                    column  = 0;
        int                    columns = 0;
        QColor                 foreground;
        QColor                 background;
        bool                   doubleUnderline = false;
        std::shared_ptr<Shape> shape;
    };
    struct CachedLine
    {
        quint64          revision = ~quint64(0);
        bool             blinking = false;
        std::vector<Run> runs;
    };
    struct Span
    {
        int row    = 0;
        int column = 0;
        int count  = 0;
    };
    struct VisualRow
    {
        QVector<Span> spans;
    };
    struct Extent
    {
        quint64 revision    = ~quint64(0);
        int     columns     = 0;
        int     maximum     = 0;
        int     contentRows = 0;
    };
    struct IndexState
    {
        int     columns   = 0;
        int     rows      = 0;
        int     sources   = 0;
        bool    primary   = false;
        bool    wrap      = false;
        quint64 discarded = 0;
        quint64 reset     = 0;
    };
    QVector<VisualRow>    _visualRows;
    QVector<int>          _sourceVisualRows;
    IndexState            _index;
    int                   _visualContentRows = 0;
    QVector<Extent>       _extents;
    bool                  _wrapLines             = true;
    bool                  _appearanceInitialized = false;
    QFont                 _metricsFont;
    int                   _metricsDpiX = 0;
    int                   _metricsDpiY = 0;
    qreal                 _metricsDpr  = 0;
    std::array<bool, 128> _fixedAscii {};
    TerminalScreen        _screen;
    TerminalGraphics      _graphics;
    // Charged by text bytes plus per-entry overhead; row runs retain shared
    // layouts independently when an entry is evicted from this bounded LRU.
    QCache<ShapeKey, std::shared_ptr<Shape>> _shapes {64 * 1024};
    QCache<RasterKey, QPixmap>               _rasters {8 * 1024 * 1024};
    std::map<int, CachedLine>                _cache;
    QColor                                   _foreground {QStringLiteral("#e5e7eb")};
    QColor                                   _background {QStringLiteral("#15191f")};
    int                                      _history    = 10000;
    qreal                                    _lineHeight = 16;
    qreal                                    _cellWidth  = 8;
    QPoint                                   _anchor {-1, -1};
    QPoint                                   _selection {-1, -1};
    bool                                     _selecting    = false;
    bool                                     _blinkVisible = true;
    bool                                     _syncLatched  = false;
    bool                                     _syncTimedOut = false;
    QTimer                                   _blinkTimer;
    QTimer                                   _syncTimer;
    QString                                  _preedit;
    QByteArray                               _pendingReplies;
    quint64                                  _seenReset     = 0;
    quint64                                  _graphicsReset = 0;
    QSize                                    _terminalSize {80, 24};
    void                                     _updateGeometry();
    void                                     _rebuildVisualRows(int columns);
    QPoint                                   _visualPosition(const QPoint& source) const;
    void                                     _updateOpacity();
    void        _afterInput(quint64 discarded, bool alternate, bool reverse, bool atBottom, QPoint scrollAnchor);
    CachedLine& _layout(int index);
    void        _drawRun(QPainter& painter, const Run& run, const QRectF& area);
    void        _drawImages(QPainter& painter, int first, bool behind);
    QPoint      _position(const QPoint& point, bool snapToCell = true) const;
    void        _selectWord(const QPoint& position);
    QRectF      _cursorRect() const;
    void        _emit(const QByteArray& bytes, OutputKind kind, const QByteArray& echo = {});
    bool        _sendMouse(int                   kind,
                           Qt::MouseButton       button,
                           Qt::MouseButtons      buttons,
                           const QPoint&         point,
                           Qt::KeyboardModifiers modifiers);
};
