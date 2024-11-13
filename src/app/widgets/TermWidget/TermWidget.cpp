#include "TermWidget.h"

#include <QApplication>
#include <QPainter>
#include <QResizeEvent>
#include <QTextLayout>
#include <core/utils.h>

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
    return QPoint((divFloor(pixel.x(), _symSize.width()) + divCeil(pixel.x(), _symSize.width())) / 2,
                  (divFloor(pixel.y(), _symSize.height()) + divCeil(pixel.y(), _symSize.height())) / 2);
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
    _cursorBlinkPeriod = periodMs;
    restartCursor();
}

void TermWidget::restartCursor()
{
    killTimer(_cursorTimerId);
    _cursorTimerId = startTimer(_cursorBlinkPeriod);
}

void TermWidget::resizeEvent(QResizeEvent* event)
{
    _updateSize(event->size());
}

void TermWidget::paintEvent(QPaintEvent* event)
{
    // qDebug() << event->rect();

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
            auto sPos = convPixelToSymbol(positive(event->pos()));

            _termModel->setStartSelection(sPos);
            _termModel->setStopSelection(sPos);
            _selectionStarted = true;
            repaint();
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
        auto sPos = convPixelToSymbol(positive(event->pos()));
        _termModel->setStopSelection(sPos);
        repaint();
    }
}

void TermWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::MouseButton::LeftButton)
    {
        auto sPos = convPixelToSymbol(positive(event->pos()));
        auto line = _termModel->lines()[sPos.y()];

        if (line.length() > sPos.x())
        {
            int c1 = -1;
            int c2 = -1;

            if (QChar(line[sPos.x()].symbol).isLetterOrNumber())
            {
                c1 = c2 = sPos.x();

                for (int i = sPos.x() + 1; i < line.length(); i++)
                {
                    if (QChar(line[i].symbol).isLetterOrNumber())
                    {
                        c2 = i;
                    }
                    else
                    {
                        break;
                    }
                }

                for (int i = sPos.x() - 1; i >= 0; i--)
                {
                    if (QChar(line[i].symbol).isLetterOrNumber())
                    {
                        c1 = i;
                    }
                    else
                    {
                        break;
                    }
                }
            }

            if (c1 >= 0)
            {
                _termModel->setStartSelection(QPoint(c1, sPos.y()));
                _termModel->setStopSelection(QPoint(c2 + 1, sPos.y()));
                _termModel->update();
            }
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

void TermWidget::keyPressEvent(QKeyEvent* event)
{
    emit outputKey(event);
    qDebug() << "Text: " << event->text() << " key " << event->keyCombination();
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
    painter.setRenderHint(QPainter::TextAntialiasing, true);

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
        QVector<QTextLayout::FormatRange> formats;
        QTextLayout                       layout;

        auto  lastCur = QPoint(rect.left(), l);
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
