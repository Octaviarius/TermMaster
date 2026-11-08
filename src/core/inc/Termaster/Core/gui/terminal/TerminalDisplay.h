#pragma once
#include <QAbstractScrollArea>
#include <QTextLayout>
#include <QVariantMap>
#include <Termaster/Core/terminal/TerminalScreen.h>
#include <memory>
#include <vector>

/** GUI-thread byte terminal view. Implements a bounded terminal subset only.
 * QTextLayout handles shaping; double-width VT cell positioning is unsupported.
 */
class TerminalDisplay : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit TerminalDisplay(QWidget* parent = nullptr);
    void          receive(const QByteArray& bytes);
    void          setAppearance(const QVariantMap& appearance);
    void          clear();
    QString       text() const;
    Q_SIGNAL void inputBytes(const QByteArray& bytes);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    struct CachedLine
    {
        quint64                      revision = ~quint64(0);
        std::unique_ptr<QTextLayout> layout;
    };
    TerminalScreen          _screen;
    std::vector<CachedLine> _cache;
    QColor                  _foreground {QStringLiteral("#e5e7eb")};
    QColor                  _background {QStringLiteral("#15191f")};
    int                     _history    = 10000;
    qreal                   _lineHeight = 16;
    qreal                   _cellWidth  = 8;
    QPoint                  _anchor {-1, -1};
    QPoint                  _selection {-1, -1};
    bool                    _selecting = false;
    void                    _updateGeometry();
    QTextLayout&            _layout(int index);
    QPoint                  _position(const QPoint& point);
    void                    _copy();
};
