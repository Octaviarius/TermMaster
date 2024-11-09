#include "TermWidget.h"

#include <QApplication>
#include <QPainter>
#include <QResizeEvent>

TermWidget::TermWidget(QWidget* parent) : QWidget(parent), _termModel(nullptr), _minTermSize(1, 1)
{
    setTermModel(new TermModel());
    setTerminalFont(QFont("Courier New", 14));
    setCursorBlinkPeriod(500);
}

TermWidget::~TermWidget()
{
}

void TermWidget::setTerminalFont(QFont font)
{
    _font        = font;
    auto metrics = QFontMetrics(font);
    _symSize     = QSize(metrics.averageCharWidth(), metrics.height());

    _symDescent = metrics.descent();

    _updateSize(size());
}

QSize TermWidget::preferredSize()
{
    return _preferredWidgetSize;
}

void TermWidget::setColorMap(TermColorMap colormap)
{
    _colormap = colormap;
}

TermModel* TermWidget::termModel()
{
    return _termModel;
}

void TermWidget::setTermModel(TermModel* model)
{
    if (_termModel)
    {
        delete _termModel;
    }
    _termModel = model;

    auto onModelUpdated = [this]() { repaint(); };
    connect(_termModel, &TermModel::updated, this, onModelUpdated);
}

QPoint TermWidget::convPixelToSymbol(QPoint pixel) const
{
    return QPoint(pixel.x() / _symSize.width(), pixel.y() / _symSize.height());
}

QPoint TermWidget::convSymbolToPixel(QPoint symPosition) const
{
    return QPoint(symPosition.x() * _symSize.width(), symPosition.x() * _symSize.height());
}

void TermWidget::setMinimalTermSize(QSize minSize)
{
    _minTermSize = minSize;
}

void TermWidget::setCursorBlinkPeriod(int periodMs)
{
    killTimer(_cursorTimerId);
    _cursorBlinkPeriod = periodMs;
    _cursorTimerId     = startTimer(periodMs);
}

void TermWidget::resizeEvent(QResizeEvent* event)
{
    _updateSize(event->size());
}

void TermWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    qDebug() << event->rect();

    auto rect     = event->rect();
    auto termRect = QRect(divRound(rect.left(), _symSize.width()),
                          divRound(rect.top(), _symSize.height()),
                          divRound(rect.width(), _symSize.width()),
                          divRound(rect.height(), _symSize.height()));

    _paintRect(termRect);
}

void TermWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::MouseButton::LeftButton)
    {
        if (!_selectionStarted)
        {
            auto pos  = event->pos();
            auto sPos = QPoint(divRound(pos.x(), _symSize.width()),
                               (divFloor(pos.y(), _symSize.height()) + divCeil(pos.y(), _symSize.height()) + 1) / 2);

            _termModel->resetSelection();
            _termModel->setStartSelection(sPos);
            _selectionStarted = true;
        }
    }
}

void TermWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::MouseButton::LeftButton))
    {
        if (_selectionStarted)
        {
            _selectionStarted = false;
        }
    }
}

void TermWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (_selectionStarted)
    {
        auto pos  = event->pos();
        auto sPos = QPoint(divRound(pos.x(), _symSize.width()),
                           (divFloor(pos.y(), _symSize.height()) + divCeil(pos.y(), _symSize.height()) + 1) / 2);

        _termModel->setStopSelection(sPos);
    }
}

void TermWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::MouseButton::LeftButton)
    {
        auto pos  = event->pos();
        auto sPos = QPoint(divRound(pos.x(), _symSize.width()),
                           (divFloor(pos.y(), _symSize.height()) + divCeil(pos.y(), _symSize.height()) + 1) / 2);

        auto line = _termModel->lines()[sPos.y()];

        if (line.length() > sPos.x())
        {
        }
    }
}

void TermWidget::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == _cursorTimerId)
    {
        _cursorBlinkActive ^= true;
        auto curPos         = _termModel->cursor()->position();

        if (_termModel->rect().contains(curPos))
        {
            repaint(QRect(curPos.x() * _symSize.width(),
                          curPos.y() * _symSize.height(),
                          _symSize.width(),
                          _symSize.height()));
        }
    }
}

void TermWidget::_updateSize(QSize size)
{
    auto termSize = QSize(std::max(_minTermSize.width(), divCeil(size.width(), _symSize.width())),
                          std::max(_minTermSize.height(), divCeil(size.height(), _symSize.height())));

    if (termSize != _termModel->size())
    {
        _preferredWidgetSize = termSize * _symSize;

        qDebug() << QString("TermSize %1x%2").arg(termSize.width()).arg(termSize.height());

        _termModel->setSize(termSize);
    }

    // resize(_preferredWidgetSize);
}

void TermWidget::_paintRect(QRect rect)
{
    auto painter = QPainter(this);

    QString lineText;

    TermChar prevCh;

    auto selection = _termModel->selection();
    int  l1, l2, c1, c2;

    if (selection.bottom() < selection.top())
    {
        l1 = selection.bottom();
        l2 = selection.top();
        c1 = selection.right();
        c2 = selection.left();
    }
    else
    {
        l1 = selection.top();
        l2 = selection.bottom();

        if (selection.bottom() == selection.top() && selection.right() < selection.left())
        {
            c1 = selection.right();
            c2 = selection.left();
        }
        else
        {
            c1 = selection.left();
            c2 = selection.right();
        }
    }

    for (size_t l = rect.top(); l <= rect.bottom(); l++)
    {
        auto  lastCur = QPoint(0, l);
        auto& line    = _termModel->lines()[l];

        lineText.resize(0);

        for (size_t c = rect.left(); c <= rect.right(); c++)
        {
            bool selected = (l >= l1 && l <= l2 && (c >= c1 || l > l1) && (c < c2 || l < l2));
            bool onCursor = (_termModel->cursor()->position() == QPoint(c, l));

            TermChar ch = (c < line.length()) ? line[c] : TermChar(QChar::Space, TermAttribute {0, 0, 0});

            bool inverse = selected;

            if (onCursor)
            {
                switch (_termModel->cursor()->style())
                {
                    case TermCursor::Style::Round:
                        inverse ^= _cursorBlinkActive;
                        break;

                    default:
                        break;
                }
            }

            if (inverse)
            {
                if (ch.attrs.isInversed())
                {
                    ch.attrs.clrAttrs(TermAttribute::Attribute::Inversed);
                }
                else
                {
                    ch.attrs.setAttrs(TermAttribute::Attribute::Inversed);
                }
            }

            if (ch.attrs != prevCh.attrs)
            {
                if (lineText.length() > 0)
                {
                    _drawText(painter, lastCur.y(), lastCur.x(), prevCh.attrs, lineText);
                }
                lineText.resize(0);
                lastCur.rx() = c;
                lastCur.ry() = l;
            }

            lineText += ch.symbol >= QChar::Space ? QChar(ch.symbol) : QChar::Space;
            prevCh    = ch;

            if (c == rect.right() && lineText.length() > 0)
            {
                _drawText(painter, lastCur.y(), lastCur.x(), prevCh.attrs, lineText);
            }
        }
    }
}

void TermWidget::_drawText(QPainter& painter, size_t line, size_t col, TermAttribute attrs, QString text)
{
    // determine backgound color
    QColor background = attrs.hasBackground() ? _colormap[attrs.background] : _defaultBackground;

    // determine foregound color
    QColor foreground = attrs.hasForeground() ? _colormap[attrs.foreground] : _defaultForeground;

    // set font attributes
    _font.setBold(attrs.isBold());
    _font.setItalic(attrs.isItalic());
    _font.setStrikeOut(attrs.isStrikeout());
    _font.setUnderline(attrs.isUnderline());

    painter.setFont(_font);

    // if text is hidden - we mustn't print it
    if (attrs.isHidden())
    {
        text.fill(' ');
    }

    // if text inversed - foreground and background are swapped
    QColor backColor;

    if (attrs.isInversed())
    {
        backColor = foreground;
        painter.setPen(background);
    }
    else
    {
        backColor = background;
        painter.setPen(foreground);
    }

    painter.setBrush(backColor);

    painter.fillRect(QRect(_symSize.width() * col,
                           _symSize.height() * line,
                           _symSize.width() * text.length(),
                           _symSize.height()),
                     backColor);

    for (int c = 0; c < text.length(); c++)
    {
        painter.drawText(QPoint(_symSize.width() * (col + c),
                                _symSize.height() * line + _symSize.height() - _symDescent),
                         text[c]);
    }
}
