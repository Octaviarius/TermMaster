#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBoundaryFinder>
#include <Termaster/Core/gui/terminal/TerminalDisplay.h>
#include <algorithm>
#include <cmath>

TerminalDisplay::TerminalDisplay(QWidget* parent) : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    setAppearance({});
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
}
void TerminalDisplay::receive(const QByteArray& bytes)
{
    const bool    bottom         = verticalScrollBar()->value() == verticalScrollBar()->maximum();
    const quint64 discarded      = _screen.discardedLines();
    const int     scrollPosition = verticalScrollBar()->value();
    _screen.receive(bytes);
    _updateGeometry();
    const quint64 removed = _screen.discardedLines() - discarded;
    if (bottom)
    {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
    else if (removed)
    {
        verticalScrollBar()->setValue(
            std::max(0, scrollPosition - static_cast<int>(std::min<quint64>(removed, scrollPosition))));
    }
    if (removed)
    {
        _anchor    = QPoint(-1, -1);
        _selection = _anchor;
        _selecting = false;
    }
    else
    {
        for (QPoint* endpoint : {&_anchor, &_selection})
        {
            if (endpoint->y() < 0)
            {
                continue;
            }
            const QString       line = _layout(endpoint->y()).text();
            QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, line);
            boundary.setPosition(std::min(endpoint->x(), static_cast<int>(line.size())));
            if (!boundary.isAtBoundary())
            {
                boundary.toPreviousBoundary();
            }
            endpoint->setX(std::max(0, static_cast<int>(boundary.position())));
        }
    }
    viewport()->update();
}
void TerminalDisplay::setAppearance(const QVariantMap& appearance)
{
    QFont         f      = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QString family = appearance.value(QStringLiteral("fontFamily")).toString();
    if (!family.isEmpty())
    {
        f.setFamily(family);
    }
    bool  ok   = false;
    qreal size = appearance.value(QStringLiteral("fontSize"), 12).toDouble(&ok);
    if (!ok || !std::isfinite(size))
    {
        size = 12;
    }
    f.setPointSizeF(std::clamp(size, 6.0, 72.0));
    setFont(f);
    QColor fg(appearance.value(QStringLiteral("foreground"), QStringLiteral("#e5e7eb")).toString());
    QColor bg(appearance.value(QStringLiteral("background"), QStringLiteral("#15191f")).toString());
    _foreground = fg.isValid() ? fg : QColor(QStringLiteral("#e5e7eb"));
    _background = bg.isValid() ? bg : QColor(QStringLiteral("#15191f"));
    int history = appearance.value(QStringLiteral("scrollbackLines"), 10000).toInt(&ok);
    _history    = ok ? std::clamp(history, 1, 100000) : 10000;
    _screen.setColors(_foreground, _background);
    _anchor    = QPoint(-1, -1);
    _selection = _anchor;
    _selecting = false;
    _cache.clear();
    _updateGeometry();
    viewport()->update();
}
void TerminalDisplay::clear()
{
    _screen.clear();
    _cache.clear();
    _anchor    = QPoint(-1, -1);
    _selection = _anchor;
    _updateGeometry();
    viewport()->update();
}
QString TerminalDisplay::text() const
{
    return _screen.text();
}
void TerminalDisplay::_updateGeometry()
{
    QFontMetricsF metrics(font());
    _lineHeight = std::max(1.0, metrics.height());
    _cellWidth  = std::max(1.0, metrics.horizontalAdvance(QLatin1Char('M')));
    int rows    = std::clamp(static_cast<int>(viewport()->height() / _lineHeight), 1, 256);
    int cols    = std::clamp(static_cast<int>(viewport()->width() / _cellWidth), 1, 4096);
    _screen.setLimits(cols, rows, _history);
    _cache.resize(_screen.lines().size());
    verticalScrollBar()->setRange(0, std::max(0, static_cast<int>(_screen.lines().size()) - rows));
    verticalScrollBar()->setPageStep(rows);
}
QTextLayout& TerminalDisplay::_layout(int index)
{
    auto&       cached = _cache[index];
    const auto& line   = _screen.lines()[index];
    if (!cached.layout || cached.revision != line.revision)
    {
        QString                         text;
        QList<QTextLayout::FormatRange> formats;
        for (const auto& cell : line.cells)
        {
            QTextCharFormat format;
            format.setForeground(cell.foreground);
            format.setBackground(cell.background);
            if (!formats.isEmpty() && formats.last().format == format)
            {
                formats.last().length += cell.text.size();
            }
            else
            {
                formats.append({static_cast<int>(text.size()), static_cast<int>(cell.text.size()), format});
            }
            text += cell.text;
        }
        cached.layout = std::make_unique<QTextLayout>(text, font());
        cached.layout->setFormats(formats);
        cached.layout->beginLayout();
        auto textLine = cached.layout->createLine();
        if (textLine.isValid())
        {
            textLine.setLineWidth(1000000);
        }
        cached.layout->endLayout();
        cached.revision = line.revision;
    }
    return *cached.layout;
}
void TerminalDisplay::paintEvent(QPaintEvent*)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), _background);
    const int first = verticalScrollBar()->value();
    const int last  = std::min(static_cast<int>(_screen.lines().size()),
                               first + static_cast<int>(viewport()->height() / _lineHeight) + 2);
    QPoint    a     = _anchor;
    QPoint    b     = _selection;
    if (a.y() > b.y() || (a.y() == b.y() && a.x() > b.x()))
    {
        std::swap(a, b);
    }
    for (int i = first; i < last; ++i)
    {
        auto&                           layout = _layout(i);
        QList<QTextLayout::FormatRange> selection;
        if (a.y() >= 0 && i >= a.y() && i <= b.y())
        {
            int             start = i == a.y() ? a.x() : 0;
            int             end   = i == b.y() ? b.x() : layout.text().size();
            QTextCharFormat format;
            format.setBackground(palette().highlight());
            format.setForeground(palette().highlightedText());
            selection.append({start, std::max(0, end - start), format});
        }
        layout.draw(&painter, QPointF(0, (i - first) * _lineHeight), selection);
    }
    const int row = _screen.cursorLine();
    if (hasFocus() && row >= first && row < last)
    {
        auto&       layout = _layout(row);
        int         offset = 0;
        const auto& cells  = _screen.lines()[row].cells;
        for (int c = 0; c < std::min(_screen.cursorColumn(), static_cast<int>(cells.size())); ++c)
        {
            offset += cells[c].text.size();
        }
        qreal x  = layout.lineCount() ? layout.lineAt(0).cursorToX(offset) : 0;
        x       += std::max(0, _screen.cursorColumn() - static_cast<int>(cells.size())) * _cellWidth;
        painter.fillRect(QRectF(x, (row - first) * _lineHeight, 2, _lineHeight), _foreground);
    }
}
void TerminalDisplay::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    _anchor    = QPoint(-1, -1);
    _selection = _anchor;
    _selecting = false;
    _cache.clear();
    _updateGeometry();
}
QPoint TerminalDisplay::_position(const QPoint& point)
{
    int                 row    = std::clamp(verticalScrollBar()->value() + static_cast<int>(point.y() / _lineHeight),
                                            0,
                                            static_cast<int>(_screen.lines().size()) - 1);
    auto&               layout = _layout(row);
    int                 pos    = layout.lineCount() ? layout.lineAt(0).xToCursor(point.x()) : 0;
    QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, layout.text());
    boundary.setPosition(pos);
    if (!boundary.isAtBoundary())
    {
        pos = std::max(0, static_cast<int>(boundary.toPreviousBoundary()));
    }
    return QPoint(pos, row);
}
void TerminalDisplay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        setFocus();
        _anchor = _selection = _position(event->position().toPoint());
        _selecting           = true;
        viewport()->update();
    }
}
void TerminalDisplay::mouseMoveEvent(QMouseEvent* event)
{
    if (_selecting)
    {
        _selection = _position(event->position().toPoint());
        viewport()->update();
    }
}
void TerminalDisplay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        _selecting = false;
    }
}
void TerminalDisplay::_copy()
{
    QPoint a = _anchor;
    QPoint b = _selection;
    if (a.y() < 0)
    {
        return;
    }
    if (a.y() > b.y() || (a.y() == b.y() && a.x() > b.x()))
    {
        std::swap(a, b);
    }
    QString selected;
    for (int i = a.y(); i <= b.y(); ++i)
    {
        if (i > a.y())
        {
            selected += '\n';
        }
        const QString line   = _layout(i).text();
        int           start  = i == a.y() ? a.x() : 0;
        int           end    = i == b.y() ? b.x() : line.size();
        selected            += line.mid(start, end - start);
    }
    QApplication::clipboard()->setText(selected);
}
void TerminalDisplay::keyPressEvent(QKeyEvent* event)
{
    const auto mods = event->modifiers();
    const int  key  = event->key();
    if (key == Qt::Key_C && mods == (Qt::ControlModifier | Qt::ShiftModifier))
    {
        _copy();
        return;
    }
    if ((key == Qt::Key_V && mods == (Qt::ControlModifier | Qt::ShiftModifier))
        || (key == Qt::Key_Insert && mods == Qt::ShiftModifier))
    {
        emit inputBytes(QApplication::clipboard()->text().toUtf8());
        return;
    }
    QByteArray bytes;
    if (mods == Qt::ControlModifier && key >= Qt::Key_A && key <= Qt::Key_Z)
    {
        bytes.append(static_cast<char>(key - Qt::Key_A + 1));
    }
    else
    {
        switch (key)
        {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                bytes = "\r";
                break;
            case Qt::Key_Backspace:
                bytes = "\x7f";
                break;
            case Qt::Key_Tab:
                bytes = "\t";
                break;
            case Qt::Key_Escape:
                bytes = "\x1b";
                break;
            case Qt::Key_Up:
                bytes = "\x1b[A";
                break;
            case Qt::Key_Down:
                bytes = "\x1b[B";
                break;
            case Qt::Key_Right:
                bytes = "\x1b[C";
                break;
            case Qt::Key_Left:
                bytes = "\x1b[D";
                break;
            case Qt::Key_Home:
                bytes = "\x1b[H";
                break;
            case Qt::Key_End:
                bytes = "\x1b[F";
                break;
            case Qt::Key_Delete:
                bytes = "\x1b[3~";
                break;
            case Qt::Key_PageUp:
                bytes = "\x1b[5~";
                break;
            case Qt::Key_PageDown:
                bytes = "\x1b[6~";
                break;
            default:
                if (!(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
                {
                    bytes = event->text().toUtf8();
                }
        }
    }
    if (!bytes.isEmpty())
    {
        emit inputBytes(bytes);
    }
    else
    {
        QAbstractScrollArea::keyPressEvent(event);
    }
}
