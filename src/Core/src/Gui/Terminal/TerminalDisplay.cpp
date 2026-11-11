#include <QApplication>
#include <QClipboard>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>
#include <QUrl>
#include <QWheelEvent>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/Terminal/TerminalInput.h>
#include <algorithm>
#include <climits>
#include <cmath>

namespace
{
QTextCharFormat formatFor(const TerminalScreen::Cell& cell, QColor foreground)
{
    QTextCharFormat format;
    format.setForeground(foreground);
    format.setFontWeight(cell.attributes & TerminalScreen::Bold ? QFont::Bold : QFont::Normal);
    format.setFontItalic(cell.attributes & TerminalScreen::Italic);
    format.setFontStrikeOut(cell.attributes & TerminalScreen::Strike);
    format.setFontOverline(cell.attributes & TerminalScreen::Overline);
    if (cell.attributes & TerminalScreen::Underline)
    {
        using Style = TerminalScreen::UnderlineStyle;
        format.setUnderlineStyle(cell.underlineStyle == Style::Curly  ? QTextCharFormat::WaveUnderline :
                                 cell.underlineStyle == Style::Dotted ? QTextCharFormat::DotLine :
                                 cell.underlineStyle == Style::Dashed ? QTextCharFormat::DashUnderline :
                                 cell.underlineStyle == Style::Double ? QTextCharFormat::NoUnderline :
                                                                        QTextCharFormat::SingleUnderline);
        format.setUnderlineColor(cell.underlineColor.isValid() ? cell.underlineColor : foreground);
    }
    return format;
}
} // namespace

TerminalDisplay::TerminalDisplay(QWidget* parent) : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_InputMethodEnabled);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    viewport()->setAutoFillBackground(false);
    _seenReset = _graphicsReset = _screen.resetRevision();
    const auto collectReply     = [this](const QByteArray& bytes) {
        if (bytes.size() <= 64 * 1024 - _pendingReplies.size())
        {
            _pendingReplies += bytes;
        }
    };
    _screen.setReplyHandler(collectReply);
    _graphics.setReplyHandler(collectReply);
    _screen.setExtensionHandler([this](const VtParser::Sequence& sequence) {
        if (_graphicsReset != _screen.resetRevision())
        {
            _graphics.clear();
            _graphicsReset = _screen.resetRevision();
        }
        _graphics.receive(sequence, _screen.cursorColumn(), _screen.cursorLine(), _screen.alternateScreen());
        const QSize advance = _graphics.takeCursorAdvance();
        if (!advance.isEmpty())
        {
            if (sequence.kind == VtParser::Kind::Dcs)
            {
                _screen.advanceGraphics(0, advance.height());
            }
            else
            {
                _screen.advanceGraphics(advance.width(), std::max(0, advance.height() - 1));
            }
        }
    });
    _blinkTimer.setInterval(500);
    connect(&_blinkTimer, &QTimer::timeout, this, [this] {
        _blinkVisible = !_blinkVisible;
        for (auto it = _cache.begin(); it != _cache.end();)
        {
            if (it->second.blinking)
            {
                it = _cache.erase(it);
            }
            else
            {
                ++it;
            }
        }
        viewport()->update();
    });
    _blinkTimer.start();
    _syncTimer.setSingleShot(true);
    _syncTimer.setInterval(150);
    connect(&_syncTimer, &QTimer::timeout, this, [this] {
        _syncTimedOut = true;
        _updateOpacity();
        viewport()->update();
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    setAppearance({});
}

void TerminalDisplay::receive(const QByteArray& bytes)
{
    const auto    discarded     = _screen.discardedLines();
    const bool    alternate     = _screen.alternateScreen();
    const bool    reverse       = _screen.mode(5);
    const bool    atBottom      = verticalScrollBar()->value() == verticalScrollBar()->maximum();
    const int     position      = verticalScrollBar()->value();
    const QString previousTitle = _screen.title();
    _screen.receive(bytes);
    _afterInput(discarded, alternate, reverse, atBottom, position);
    // Replies are output, never fed back into the parser or passed through local echo.
    QByteArray replies;
    replies.swap(_pendingReplies);
    _emit(replies, OutputKind::Reply);
    if (previousTitle != _screen.title())
    {
        emit titleChanged(_screen.title());
    }
}

void TerminalDisplay::localEcho(const QByteArray& bytes)
{
    const auto discarded = _screen.discardedLines();
    const bool atBottom  = verticalScrollBar()->value() == verticalScrollBar()->maximum();
    const int  position  = verticalScrollBar()->value();
    _screen.echo(bytes);
    _afterInput(discarded, _screen.alternateScreen(), _screen.mode(5), atBottom, position);
}

void TerminalDisplay::setAppearance(const QVariantMap& appearance)
{
    auto       selected = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const auto family   = appearance.value("fontFamily").toString();
    if (!family.isEmpty())
    {
        selected.setFamily(family);
    }
    selected.setStyleHint(QFont::Monospace);
    selected.setKerning(false);
    bool        ok   = false;
    const qreal size = appearance.value("fontSize", 12).toDouble(&ok);
    selected.setPointSizeF(ok && std::isfinite(size) ? std::clamp(size, 6.0, 72.0) : 12.0);
    QColor foreground(appearance.value("foreground", "#e5e7eb").toString());
    QColor background(appearance.value("background", "#15191f").toString());
    foreground                 = foreground.isValid() ? foreground : QColor("#e5e7eb");
    background                 = background.isValid() ? background : QColor("#15191f");
    const int requestedHistory = appearance.value("scrollbackLines", 10000).toInt(&ok);
    const int history          = ok ? std::clamp(requestedHistory, 1, 100000) : 10000;
    if (_appearanceInitialized && selected == font() && foreground == _foreground && background == _background
        && history == _history)
    {
        return;
    }

    _appearanceInitialized = true;
    setFont(selected);
    _foreground = foreground;
    _background = background;
    _history    = history;
    _screen.setColors(_foreground, _background);
    _updateOpacity();
    _anchor = _selection = QPoint(-1, -1);
    _selecting           = false;
    _cache.clear();
    _shapes.clear();
    _rasters.clear();
    _updateGeometry();
    viewport()->update();
}

void TerminalDisplay::clear()
{
    _screen.clear();
    _pendingReplies.clear();
    _seenReset = _graphicsReset = _screen.resetRevision();
    _graphics.clear();
    _cache.clear();
    _shapes.clear();
    _rasters.clear();
    _anchor = _selection = QPoint(-1, -1);
    _selecting           = false;
    _preedit.clear();
    _syncLatched  = false;
    _syncTimedOut = false;
    _syncTimer.stop();
    _updateOpacity();
    _updateGeometry();
    emit textChanged();
    emit selectionChanged();
    viewport()->update();
}

QString TerminalDisplay::text() const
{
    return _screen.text();
}

const TerminalScreen& TerminalDisplay::screen() const
{
    return _screen;
}
QSize TerminalDisplay::terminalSize() const
{
    return _terminalSize;
}

bool TerminalDisplay::hasSelection() const
{
    return _anchor.y() >= 0 && _selection.y() >= 0 && _anchor != _selection;
}

bool TerminalDisplay::hasContent() const
{
    for (const auto& line : _screen.lines())
    {
        for (const auto& cell : line.cells)
        {
            if (cell.width > 0)
            {
                for (QChar character : cell.text)
                {
                    if (!character.isSpace())
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void TerminalDisplay::copy()
{
    if (!hasSelection())
    {
        return;
    }
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
    for (int row = a.y(); row <= b.y() && row < _screen.lines().size(); ++row)
    {
        if (row > a.y())
        {
            selected += '\n';
        }
        const auto& cells = _screen.lines()[row].cells;
        QString     line;
        const int end = std::min(row == b.y() ? b.x() : static_cast<int>(cells.size()), static_cast<int>(cells.size()));
        for (int column = row == a.y() ? a.x() : 0; column < end; ++column)
        {
            if (cells[column].width > 0)
            {
                line += cells[column].text.isEmpty() ? " " : cells[column].text;
            }
        }
        while (line.endsWith(' '))
        {
            line.chop(1);
        }
        selected += line;
    }
    QApplication::clipboard()->setText(selected);
}

void TerminalDisplay::paste()
{
    const QString    content = QApplication::clipboard()->text();
    const QByteArray bytes   = TerminalInput::paste(content, _screen);
    QByteArray       echo    = content.toUtf8();
    echo.replace("\r\n", "\n");
    echo.replace("\r", "\n");
    echo.replace("\n", "\r\n");
    _emit(bytes, OutputKind::Paste, echo);
}

void TerminalDisplay::selectAll()
{
    _anchor       = QPoint(0, 0);
    const int row = static_cast<int>(_screen.lines().size()) - 1;
    _selection    = QPoint(_screen.lines()[row].cells.size(), row);
    emit selectionChanged();
    viewport()->update();
}

bool TerminalDisplay::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride)
    {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (!TerminalInput::key(*key, _screen).isEmpty())
        {
            event->accept();
            return true;
        }
    }
    const bool handled = QAbstractScrollArea::event(event);
    if (_appearanceInitialized
        && (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange
            || event->type() == QEvent::DevicePixelRatioChange || event->type() == QEvent::ScreenChangeInternal))
    {
        // Screen/font notifications need not be accompanied by a pixel resize.
        _metricsDpr = 0;
        _updateGeometry();
        viewport()->update();
    }
    return handled;
}

bool TerminalDisplay::focusNextPrevChild(bool)
{
    return false;
}

void TerminalDisplay::paintEvent(QPaintEvent* event)
{
    if (_screen.mode(2026) && !_syncTimedOut)
    {
        return;
    }
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), _screen.mode(5) ? _foreground : _background);
    const int first = verticalScrollBar()->value();
    const int last  = std::min(static_cast<int>(_screen.lines().size()),
                               first + static_cast<int>(viewport()->height() / _lineHeight) + 2);
    for (auto it = _cache.begin(); it != _cache.end();)
    {
        if (it->first < first || it->first >= last)
        {
            it = _cache.erase(it);
        }
        else
        {
            ++it;
        }
    }
    const QRect dirty      = event->rect();
    const int   paintFirst = std::max(first, first + static_cast<int>(dirty.top() / _lineHeight));
    const int   paintLast  = std::min(last, first + static_cast<int>(dirty.bottom() / _lineHeight) + 1);
    for (int row = paintFirst; row < paintLast; ++row)
    {
        for (const auto& run : _layout(row).runs)
        {
            painter.fillRect(QRectF(run.column * _cellWidth,
                                    (row - first) * _lineHeight,
                                    run.columns * _cellWidth,
                                    _lineHeight),
                             run.background);
        }
    }
    _drawImages(painter, first, true);
    QPoint a = _anchor;
    QPoint b = _selection;
    if (a.y() > b.y() || (a.y() == b.y() && a.x() > b.x()))
    {
        std::swap(a, b);
    }
    for (int row = paintFirst; row < paintLast; ++row)
    {
        auto&       cached = _layout(row);
        const qreal y      = (row - first) * _lineHeight;
        for (const auto& run : cached.runs)
        {
            _drawRun(painter, run, QRectF(run.column * _cellWidth, y, run.columns * _cellWidth, _lineHeight));
        }
        if (a.y() >= 0 && row >= a.y() && row <= b.y())
        {
            const int start     = row == a.y() ? a.x() : 0;
            const int end       = row == b.y() ? b.x() : _screen.lines()[row].cells.size();
            QColor    highlight = palette().highlight().color();
            highlight.setAlpha(100);
            painter.fillRect(QRectF(start * _cellWidth, y, std::max(0, end - start) * _cellWidth, _lineHeight),
                             highlight);
        }
    }
    _drawImages(painter, first, false);
    const QRectF cursor = _cursorRect();
    const int    shape  = _screen.cursorShape();
    const bool   blink  = shape == 0 || shape == 1 || shape == 3 || shape == 5;
    if (hasFocus() && _screen.cursorVisible() && (!blink || _blinkVisible) && cursor.intersects(viewport()->rect()))
    {
        if (shape == 3 || shape == 4)
        {
            painter.fillRect(QRectF(cursor.left(), cursor.bottom() - 2, cursor.width(), 2), _foreground);
        }
        else if (shape == 5 || shape == 6)
        {
            painter.fillRect(QRectF(cursor.left(), cursor.top(), 2, cursor.height()), _foreground);
        }
        else
        {
            painter.setPen(_foreground);
            painter.drawRect(cursor.adjusted(0, 0, -1, -1));
        }
    }
    if (!_preedit.isEmpty() && hasFocus())
    {
        painter.fillRect(QRectF(cursor.topLeft(), QSizeF(_cellWidth * _preedit.size(), _lineHeight)), _background);
        painter.setPen(_foreground);
        painter.drawText(QPointF(cursor.left(), cursor.top() + QFontMetricsF(font()).ascent()), _preedit);
        painter.drawLine(cursor.bottomLeft(), QPointF(cursor.left() + _cellWidth * _preedit.size(), cursor.bottom()));
    }
}

void TerminalDisplay::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    _updateGeometry();
}

void TerminalDisplay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_C && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier))
    {
        copy();
        return;
    }
    if ((event->key() == Qt::Key_V && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier))
        || (event->key() == Qt::Key_Insert && event->modifiers() == Qt::ShiftModifier))
    {
        paste();
        return;
    }
    const auto bytes = TerminalInput::key(*event, _screen);
    if (!bytes.isEmpty())
    {
        _blinkVisible = true;
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        QByteArray echo;
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            echo = "\r\n";
        }
        else if (event->key() == Qt::Key_Backspace)
        {
            echo = "\b \b";
        }
        else if (event->key() == Qt::Key_Tab)
        {
            echo = "\t";
        }
        else if (!(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
                 || (event->modifiers() & Qt::GroupSwitchModifier))
        {
            echo = event->text().toUtf8();
        }
        _emit(bytes, OutputKind::Keyboard, echo);
    }
    else
    {
        QAbstractScrollArea::keyPressEvent(event);
    }
}

void TerminalDisplay::keyReleaseEvent(QKeyEvent* event)
{
    _emit(TerminalInput::key(*event, _screen, true), OutputKind::Keyboard);
}

void TerminalDisplay::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    if (_sendMouse(int(TerminalInput::MouseEventKind::Press),
                   event->button(),
                   event->buttons(),
                   event->position().toPoint(),
                   event->modifiers()))
    {
        return;
    }
    if (event->button() == Qt::LeftButton)
    {
        const QPoint position = _position(event->position().toPoint());
        if (event->modifiers() & Qt::ControlModifier)
        {
            const auto& cells = _screen.lines()[position.y()].cells;
            int         hit   = std::clamp(static_cast<int>(event->position().x() / _cellWidth),
                                           0,
                                           static_cast<int>(cells.size()) - 1);
            while (hit > 0 && cells[hit].width == 0)
            {
                --hit;
            }
            if (!cells[hit].hyperlink.isEmpty())
            {
                const QUrl url(cells[hit].hyperlink);
                if (url.scheme() == "https" || url.scheme() == "http" || url.scheme() == "mailto")
                {
                    emit linkActivated(url);
                }
                return;
            }
        }
        _anchor = _selection = position;
        emit selectionChanged();
        _selecting = true;
        viewport()->update();
    }
}

void TerminalDisplay::mouseMoveEvent(QMouseEvent* event)
{
    if (!_selecting
        && _sendMouse(int(TerminalInput::MouseEventKind::Move),
                      Qt::NoButton,
                      event->buttons(),
                      event->position().toPoint(),
                      event->modifiers()))
    {
        return;
    }
    if (_selecting)
    {
        _selection = _position(event->position().toPoint());
        emit selectionChanged();
        viewport()->update();
    }
}

void TerminalDisplay::mouseReleaseEvent(QMouseEvent* event)
{
    if (_selecting && event->button() == Qt::LeftButton)
    {
        _selecting = false;
        return;
    }
    if (_sendMouse(int(TerminalInput::MouseEventKind::Release),
                   event->button(),
                   event->buttons(),
                   event->position().toPoint(),
                   event->modifiers()))
    {
        return;
    }
    if (event->button() == Qt::LeftButton)
    {
        _selecting = false;
    }
}

void TerminalDisplay::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() && _screen.mouseTracking() && !(event->modifiers() & Qt::ShiftModifier))
    {
        const auto kind  = event->angleDelta().y() > 0 ? TerminalInput::MouseEventKind::WheelUp :
                                                         TerminalInput::MouseEventKind::WheelDown;
        const int  count = std::clamp(std::abs(event->angleDelta().y()) / 120, 1, 100);
        for (int i = 0; i < count; ++i)
        {
            _sendMouse(int(kind), Qt::NoButton, event->buttons(), event->position().toPoint(), event->modifiers());
        }
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void TerminalDisplay::focusInEvent(QFocusEvent* event)
{
    QAbstractScrollArea::focusInEvent(event);
    _blinkVisible = true;
    _emit(TerminalInput::focus(true, _screen), OutputKind::Focus);
    viewport()->update();
}

void TerminalDisplay::focusOutEvent(QFocusEvent* event)
{
    QAbstractScrollArea::focusOutEvent(event);
    _preedit.clear();
    _emit(TerminalInput::focus(false, _screen), OutputKind::Focus);
    viewport()->update();
}

void TerminalDisplay::inputMethodEvent(QInputMethodEvent* event)
{
    _preedit          = event->preeditString().left(1024);
    const auto commit = event->commitString().toUtf8();
    if (commit.size() <= 4 * 1024 * 1024)
    {
        _emit(commit, OutputKind::Keyboard, commit);
    }
    event->accept();
    viewport()->update();
}

QVariant TerminalDisplay::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImCursorRectangle)
    {
        return _cursorRect().translated(viewport()->pos());
    }
    if (query == Qt::ImFont)
    {
        return font();
    }
    if (query == Qt::ImEnabled)
    {
        return true;
    }
    return QAbstractScrollArea::inputMethodQuery(query);
}

void TerminalDisplay::_updateOpacity()
{
    // Qt can omit ancestor/background preparation when this paint fills every
    // pixel. Deferred synchronized output and translucent themes cannot promise it.
    const auto background = _screen.mode(5) ? _foreground : _background;
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent,
                             background.alpha() == 255 && (!_screen.mode(2026) || _syncTimedOut));
}

void TerminalDisplay::_updateGeometry()
{
    if (_metricsFont != font() || _metricsDpiX != viewport()->logicalDpiX() || _metricsDpiY != viewport()->logicalDpiY()
        || _metricsDpr != viewport()->devicePixelRatioF())
    {
        _metricsFont = font();
        _metricsDpiX = viewport()->logicalDpiX();
        _metricsDpiY = viewport()->logicalDpiY();
        _metricsDpr  = viewport()->devicePixelRatioF();
        const QFontMetricsF metrics(font(), viewport());
        _lineHeight = std::max(1.0, std::ceil(metrics.height()));
        _cellWidth  = std::max(1.0, metrics.horizontalAdvance(QLatin1Char('M')));
        for (int character = 0; character < 128; ++character)
        {
            _fixedAscii[character] = std::abs(metrics.horizontalAdvance(QChar(character)) - _cellWidth) < 0.02;
        }
        _cache.clear();
        _shapes.clear();
        _rasters.clear();
    }
    const int rows    = std::clamp(static_cast<int>(viewport()->height() / _lineHeight), 1, 256);
    const int columns = std::clamp(static_cast<int>(viewport()->width() / _cellWidth), 1, 4096);
    _screen.setLimits(columns, rows, _history);
    const bool sizeChanged = _terminalSize != QSize(columns, rows);
    _terminalSize          = QSize(columns, rows);
    _graphics.setCellSize(QSize(std::max(1, qRound(_cellWidth)), std::max(1, qRound(_lineHeight))));
    const auto count = static_cast<int>(_screen.lines().size());
    verticalScrollBar()->setRange(0, std::max(0, count - rows));
    verticalScrollBar()->setPageStep(rows);
    if (sizeChanged)
    {
        _anchor = _selection = QPoint(-1, -1);
        _selecting           = false;
        emit terminalSizeChanged(columns, rows);
    }
}

void TerminalDisplay::_afterInput(quint64 discarded, bool alternate, bool reverse, bool atBottom, int scrollPosition)
{
    const bool reset = _seenReset != _screen.resetRevision();
    _seenReset       = _screen.resetRevision();
    if (_graphicsReset != _seenReset)
    {
        _graphics.clear();
        _graphicsReset = _seenReset;
    }
    _updateGeometry();
    const auto now     = _screen.discardedLines();
    const auto removed = now >= discarded ? now - discarded : now;
    if (now < discarded)
    {
        _graphics.clear();
    }
    else if (removed)
    {
        _graphics.trimHistory(static_cast<int>(std::min<quint64>(removed, INT_MAX)));
    }
    if (reset || removed || alternate != _screen.alternateScreen() || reverse != _screen.mode(5) || now < discarded)
    {
        _cache.clear();
        _anchor = _selection = QPoint(-1, -1);
        _selecting           = false;
    }
    if (atBottom || alternate != _screen.alternateScreen())
    {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
    else if (removed)
    {
        verticalScrollBar()->setValue(
            std::max(0, scrollPosition - static_cast<int>(std::min<quint64>(removed, scrollPosition))));
    }
    if (_screen.mode(2026))
    {
        if (!_syncLatched)
        {
            _syncLatched = true;
            _syncTimer.start();
        }
    }
    else
    {
        _syncLatched  = false;
        _syncTimedOut = false;
        _syncTimer.stop();
    }
    _updateOpacity();
    emit textChanged();
    emit selectionChanged();
    viewport()->update();
}

TerminalDisplay::CachedLine& TerminalDisplay::_layout(int index)
{
    auto&       cached = _cache[index];
    const auto& row    = _screen.lines()[index];
    if (cached.revision == row.revision)
    {
        return cached;
    }
    cached.runs.clear();
    cached.blinking = false;
    for (int column = 0; column < row.cells.size();)
    {
        const auto& cell = row.cells[column];
        if (cell.width == 0)
        {
            ++column;
            continue;
        }
        QColor foreground = cell.foreground;
        QColor background = cell.background;
        if (bool(cell.attributes & TerminalScreen::Inverse) != _screen.mode(5))
        {
            std::swap(foreground, background);
        }
        if (cell.attributes & TerminalScreen::Faint)
        {
            foreground.setAlphaF(foreground.alphaF() * 0.6);
        }
        if (cell.attributes & TerminalScreen::Blink)
        {
            cached.blinking = true;
        }
        if ((cell.attributes & TerminalScreen::Invisible)
            || ((cell.attributes & TerminalScreen::Blink) && !_blinkVisible))
        {
            foreground = background;
        }
        const auto plainSpace = [](const TerminalScreen::Cell& item) {
            return item.width == 1 && (item.text.isEmpty() || item.text == QLatin1String(" "))
                && !(item.attributes & (TerminalScreen::Underline | TerminalScreen::Strike | TerminalScreen::Overline));
        };
        if (plainSpace(cell))
        {
            int end = column + 1;
            while (end < row.cells.size())
            {
                const auto& next           = row.cells[end];
                const auto  nextBackground = bool(next.attributes & TerminalScreen::Inverse) != _screen.mode(5) ?
                                                 next.foreground :
                                                 next.background;
                if (!plainSpace(next) || nextBackground != background)
                {
                    break;
                }
                ++end;
            }
            // Background-only spaces need no shaping or glyph draw, including
            // colored/inverse blanks. Decorations retain the QTextLayout path.
            Run run;
            run.column     = column;
            run.columns    = end - column;
            run.background = background;
            cached.runs.push_back(std::move(run));
            column = end;
            continue;
        }
        const auto format = formatFor(cell, foreground);
        QString    text   = cell.text.isEmpty() ? QString(" ") : cell.text;
        int        end    = column + std::max(1, cell.width);
        // Coalesce fixed-width ASCII. Other graphemes retain explicit cell positions,
        // independent of fallback-font metrics and QTextLayout's Unicode shaping.
        auto fixedAscii   = [&](const TerminalScreen::Cell& item) {
            return item.width == 1 && item.text.size() == 1 && item.text[0].unicode() < 0x7f
                && _fixedAscii[item.text[0].unicode()];
        };
        if (fixedAscii(cell))
        {
            while (end < row.cells.size())
            {
                const auto& next = row.cells[end];
                if (!fixedAscii(next) || next.foreground != cell.foreground || next.background != cell.background
                    || next.attributes != cell.attributes || next.underlineStyle != cell.underlineStyle
                    || next.underlineColor != cell.underlineColor)
                {
                    break;
                }
                text += next.text;
                ++end;
            }
        }
        Run run;
        run.column          = column;
        run.columns         = end - column;
        run.foreground      = foreground;
        run.background      = background;
        run.doubleUnderline = (cell.attributes & TerminalScreen::Underline)
                           && cell.underlineStyle == TerminalScreen::UnderlineStyle::Double;
        const ShapeKey key {text, format};
        if (const auto* shape = _shapes.object(key))
        {
            run.shape = *shape;
        }
        else
        {
            run.shape         = std::make_shared<Shape>();
            run.shape->key    = key;
            run.shape->layout = std::make_unique<QTextLayout>(text, font(), viewport());
            run.shape->layout->setCacheEnabled(true);
            QTextOption option;
            option.setTextDirection(Qt::LeftToRight);
            option.setWrapMode(QTextOption::NoWrap);
            run.shape->layout->setTextOption(option);
            run.shape->layout->setFormats({
                {0, static_cast<int>(text.size()), format}
            });
            run.shape->layout->beginLayout();
            auto line = run.shape->layout->createLine();
            if (line.isValid())
            {
                line.setLineWidth(1000000);
            }
            run.shape->layout->endLayout();
            run.shape->directGlyphs = !(
                cell.attributes & (TerminalScreen::Underline | TerminalScreen::Strike | TerminalScreen::Overline));
            if (run.shape->directGlyphs)
            {
                run.shape->glyphs = run.shape->layout->glyphRuns();
            }
            _shapes.insert(key, new std::shared_ptr<Shape>(run.shape), 256 + int(text.size()) * 2);
        }
        cached.runs.push_back(std::move(run));
        column = end;
    }
    cached.revision = row.revision;
    return cached;
}

void TerminalDisplay::_drawRun(QPainter& painter, const Run& run, const QRectF& area)
{
    if (!run.shape || !run.shape->layout->lineCount())
    {
        return;
    }
    const qreal naturalWidth = run.shape->layout->lineAt(0).naturalTextWidth();
    const qreal dpr          = viewport()->devicePixelRatioF();
    const auto  aligned      = [dpr](qreal value) { return std::abs(value * dpr - std::round(value * dpr)) < 0.001; };
    if (run.shape->directGlyphs && run.background.alpha() == 255 && _graphics.placements().isEmpty()
        && aligned(area.x()) && aligned(area.y()) && aligned(area.width()) && aligned(area.height()))
    {
        const QSize     pixels(qRound(area.width() * dpr), qRound(area.height() * dpr));
        const RasterKey key {run.shape->key, run.background.rgb(), pixels};
        if (const auto* image = _rasters.object(key))
        {
            painter.drawPixmap(area.topLeft(), *image);
            return;
        }
        const qint64 cost = qint64(pixels.width()) * pixels.height() * 4 + run.shape->key.text.size() * 2 + 256;
        if (cost > 0 && cost <= _rasters.maxCost())
        {
            QPixmap image(pixels);
            image.setDevicePixelRatio(dpr);
            image.fill(run.background);
            {
                QPainter target(&image);
                target.setClipRect(QRectF(QPointF(), area.size()));
                if (naturalWidth > area.width() + 0.1)
                {
                    target.scale(area.width() / naturalWidth, 1.0);
                }
                target.setPen(run.foreground);
                for (const auto& glyphs : run.shape->glyphs)
                {
                    target.drawGlyphRun(QPointF(), glyphs);
                }
            }
            painter.drawPixmap(area.topLeft(), image);
            _rasters.insert(key, new QPixmap(std::move(image)), int(cost));
            return;
        }
    }
    painter.save();
    painter.setClipRect(area);
    painter.translate(area.topLeft());
    if (naturalWidth > area.width() + 0.1)
    {
        painter.scale(area.width() / naturalWidth, 1.0);
    }
    if (run.shape->directGlyphs)
    {
        painter.setPen(run.foreground);
        for (const auto& glyphs : run.shape->glyphs)
        {
            painter.drawGlyphRun(QPointF(), glyphs);
        }
    }
    else
    {
        run.shape->layout->draw(&painter, QPointF());
    }
    painter.restore();
    if (run.doubleUnderline)
    {
        painter.setPen(run.foreground);
        painter.drawLine(QPointF(area.left(), area.bottom() - 2), QPointF(area.right(), area.bottom() - 2));
        painter.drawLine(QPointF(area.left(), area.bottom() - 4), QPointF(area.right(), area.bottom() - 4));
    }
}

void TerminalDisplay::_drawImages(QPainter& painter, int first, bool behind)
{
    for (const auto& image : _graphics.placements())
    {
        if (image.alternate != _screen.alternateScreen() || (image.z < 0) != behind)
        {
            continue;
        }
        const QSizeF size = image.pixelSize.isValid() ? QSizeF(image.pixelSize) :
                                                        QSizeF(image.columns * _cellWidth, image.rows * _lineHeight);
        const QRectF target(QPointF(image.column * _cellWidth, (image.row - first) * _lineHeight), size);
        if (!target.intersects(viewport()->rect()))
        {
            continue;
        }
        painter.drawImage(target, image.image, image.source.isEmpty() ? image.image.rect() : image.source);
    }
}

QPoint TerminalDisplay::_position(const QPoint& point) const
{
    const int   row   = std::clamp(verticalScrollBar()->value() + static_cast<int>(point.y() / _lineHeight),
                                   0,
                                   static_cast<int>(_screen.lines().size()) - 1);
    const auto& cells = _screen.lines()[row].cells;
    int column = std::clamp(static_cast<int>(std::floor(point.x() / _cellWidth)), 0, static_cast<int>(cells.size()));
    if (column < cells.size())
    {
        while (column > 0 && cells[column].width == 0)
        {
            --column;
        }
        const int width = std::max(1, cells[column].width);
        if (point.x() > (column + width * 0.5) * _cellWidth)
        {
            column += width;
        }
    }
    return QPoint(column, row);
}

QRectF TerminalDisplay::_cursorRect() const
{
    return QRectF(_screen.cursorColumn() * _cellWidth,
                  (_screen.cursorLine() - verticalScrollBar()->value()) * _lineHeight,
                  _cellWidth,
                  _lineHeight);
}

void TerminalDisplay::_emit(const QByteArray& bytes, OutputKind kind, const QByteArray& echo)
{
    if (!bytes.isEmpty())
    {
        emit outputBytes(bytes, kind, echo);
    }
}

bool TerminalDisplay::_sendMouse(int                   kind,
                                 Qt::MouseButton       button,
                                 Qt::MouseButtons      buttons,
                                 const QPoint&         point,
                                 Qt::KeyboardModifiers modifiers)
{
    if (!_screen.mouseTracking() || (modifiers & Qt::ShiftModifier))
    {
        return false;
    }
    const int screenRows = verticalScrollBar()->pageStep();
    const int top        = std::max(0, static_cast<int>(_screen.lines().size()) - screenRows);
    const int row        = verticalScrollBar()->value() + static_cast<int>(point.y() / _lineHeight) - top;
    if (row < 0 || row >= screenRows)
    {
        return false;
    }
    const bool pixels = _screen.mouseEncoding() == 1016;
    const int  x      = pixels ? point.x() : static_cast<int>(point.x() / _cellWidth);
    const int  y      = pixels ? row * qRound(_lineHeight) + point.y() % std::max(1, qRound(_lineHeight)) : row;
    _emit(TerminalInput::mouse(static_cast<TerminalInput::MouseEventKind>(kind),
                               button,
                               buttons,
                               std::max(0, x),
                               std::max(0, y),
                               modifiers,
                               _screen,
                               pixels),
          OutputKind::Mouse);
    return true;
}
