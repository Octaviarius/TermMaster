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
    connect(_termModel->cursor(), &TermCursor::positionChanged, this, &TermWidget::forceCursorNow);
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

void TermWidget::forceCursorNow()
{
    _cursorBlinkForcedActiveNow = true;

    if (_cursorBlinkActive == false)
    {
        _cursorBlinkActive = true;
        _repaintCursor();
    }
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
        if (_cursorBlinkForcedActiveNow)
        {
            _cursorBlinkActive          = true;
            _cursorBlinkForcedActiveNow = false;
        }
        else
        {
            _cursorBlinkActive ^= true;
            _repaintCursor();
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

void TermWidget::_repaintCursor()
{
    auto curPos = _termModel->cursor()->position();

    if (_termModel->rect().contains(curPos))
    {
        repaint(
            QRect(curPos.x() * _symSize.width(), curPos.y() * _symSize.height(), _symSize.width(), _symSize.height()));
    }
}

void TermWidget::_paintRect(QRect rect)
{
    auto painter = QPainter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.fillRect(this->rect(), _defaultBackground);

    auto selection = _termModel->selection();

    QTextLayout                       textLayout;
    QVector<QTextLayout::FormatRange> textFormats;

    TermChar prevCh;

    const auto& lines = _termModel->lines();

    auto topLeft = QPointF(0, 0);

    for (int l = 0; l < lines.count(); l++)
    {
        // stop rendering the text
        if (topLeft.y() > height() + _symSize.height())
        {
            break;
        }

        const auto& line = lines[l];

        if (line.length() == 0)
        {
            continue;
        }

        QString lineText(line.length(), QChar::Space);
        prevCh      = line[0];
        int prevCol = 0;

        for (int c = 0; c < line.length(); c++)
        {
            const bool isLastCol = (c == line.length() - 1);

            auto ch = line[c];

            if (ch.symbol < 0x20)
            {
                lineText[c] = QChar(ch.symbol + 0x2400);
            }
            else
            {
                lineText[c] = QChar(ch.symbol);
            }

            if (prevCh.attrs != ch.attrs || isLastCol)
            {
                textFormats.append(
                    QTextLayout::FormatRange {prevCol, c - prevCol + 1, _attrsToTextCharFormat(prevCh.attrs)});
                prevCh  = ch;
                prevCol = c;
            }
        }

        textLayout.setText(lineText);
        auto textOption = QTextOption();
        textOption.setWrapMode(QTextOption::WrapAnywhere);
        textLayout.setTextOption(textOption);
        textLayout.setFormats(textFormats);
        textLayout.setCacheEnabled(true);

        textLayout.beginLayout();
        while (1)
        {
            QTextLine textLine = textLayout.createLine();
            if (!textLine.isValid())
            {
                break;
            }
            textLine.setLineWidth(width());
            textLine.setPosition(topLeft /*- QPointF(0, textLine.ascent())*/);
            auto bottomRight = topLeft + QPointF(width(), _symSize.height());
            textLayout.draw(&painter, QPointF());
            topLeft += QPointF(0, textLine.height());
        }
        textLayout.endLayout();
    }
}

QTextCharFormat TermWidget::_attrsToTextCharFormat(TermAttribute attrs)
{
    QTextCharFormat fmt;

    fmt.setFont(_font);
    fmt.setFontKerning(false);

    // determine backgound color
    QColor background = attrs.hasBackground() ? _colormap[attrs.background] : _defaultBackground;

    // determine foregound color
    QColor foreground = attrs.hasForeground() ? _colormap[attrs.foreground] : _defaultForeground;

    // set font attributes
    fmt.setFontWeight(attrs.isBold()  ? QFont::Weight::Bold :
                      attrs.isFaint() ? QFont::Weight::Thin :
                                        QFont::Weight::Normal);
    fmt.setFontItalic(attrs.isItalic());
    fmt.setFontStrikeOut(attrs.isStrikeout());
    fmt.setFontUnderline(attrs.isUnderline());

    if (attrs.isInversed())
    {
        fmt.setForeground(background);
        fmt.setBackground(foreground);
    }
    else
    {
        fmt.setForeground(foreground);
        fmt.setBackground(background);
    }

    return fmt;
}
