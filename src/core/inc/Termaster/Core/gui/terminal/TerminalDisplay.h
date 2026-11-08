#pragma once
#include <QAbstractScrollArea>
#include <QTextLayout>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <Termaster/Core/terminal/TerminalGraphics.h>
#include <Termaster/Core/terminal/TerminalScreen.h>
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
    Q_ENUM(OutputKind)
    explicit TerminalDisplay(QWidget* parent = nullptr);
    void                  receive(const QByteArray& bytes);
    void                  localEcho(const QByteArray& bytes);
    void                  setAppearance(const QVariantMap& appearance);
    void                  clear();
    QString               text() const;
    const TerminalScreen& screen() const;
    Q_SIGNAL void         outputBytes(const QByteArray& bytes, OutputKind kind, const QByteArray& echo);
    Q_SIGNAL void         selectionChanged();
    Q_SIGNAL void         textChanged();
    bool                  hasSelection() const;
    bool                  hasContent() const;
    Q_SLOT void           copy();
    Q_SLOT void           paste();
    Q_SLOT void           selectAll();
    Q_SIGNAL void         titleChanged(const QString& title);
    Q_SIGNAL void         linkActivated(const QUrl& url);

protected:
    bool     event(QEvent* event) override;
    bool     focusNextPrevChild(bool next) override;
    void     paintEvent(QPaintEvent* event) override;
    void     resizeEvent(QResizeEvent* event) override;
    void     keyPressEvent(QKeyEvent* event) override;
    void     keyReleaseEvent(QKeyEvent* event) override;
    void     mousePressEvent(QMouseEvent* event) override;
    void     mouseMoveEvent(QMouseEvent* event) override;
    void     mouseReleaseEvent(QMouseEvent* event) override;
    void     wheelEvent(QWheelEvent* event) override;
    void     focusInEvent(QFocusEvent* event) override;
    void     focusOutEvent(QFocusEvent* event) override;
    void     inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
    struct Run
    {
        int                          column  = 0;
        int                          columns = 0;
        QColor                       foreground;
        QColor                       background;
        bool                         doubleUnderline = false;
        std::unique_ptr<QTextLayout> layout;
    };
    struct CachedLine
    {
        quint64          revision = ~quint64(0);
        bool             blinking = false;
        std::vector<Run> runs;
    };
    TerminalScreen            _screen;
    TerminalGraphics          _graphics;
    std::map<int, CachedLine> _cache;
    QColor                    _foreground {QStringLiteral("#e5e7eb")};
    QColor                    _background {QStringLiteral("#15191f")};
    int                       _history    = 10000;
    qreal                     _lineHeight = 16;
    qreal                     _cellWidth  = 8;
    QPoint                    _anchor {-1, -1};
    QPoint                    _selection {-1, -1};
    bool                      _selecting    = false;
    bool                      _blinkVisible = true;
    bool                      _syncLatched  = false;
    bool                      _syncTimedOut = false;
    QTimer                    _blinkTimer;
    QTimer                    _syncTimer;
    QString                   _preedit;
    QByteArray                _pendingReplies;
    quint64                   _seenReset     = 0;
    quint64                   _graphicsReset = 0;
    void                      _updateGeometry();
    void        _afterInput(quint64 discarded, bool alternate, bool reverse, bool atBottom, int scrollPosition);
    CachedLine& _layout(int index);
    void        _drawImages(QPainter& painter, int first, bool behind);
    QPoint      _position(const QPoint& point) const;
    QRectF      _cursorRect() const;
    void        _emit(const QByteArray& bytes, OutputKind kind, const QByteArray& echo = {});
    bool        _sendMouse(int                   kind,
                           Qt::MouseButton       button,
                           Qt::MouseButtons      buttons,
                           const QPoint&         point,
                           Qt::KeyboardModifiers modifiers);
};
