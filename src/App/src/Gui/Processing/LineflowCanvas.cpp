#include "LineflowCanvas.h"

#include "LineflowEditorModel.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHelpEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
#include <Termaster/Core/Gui/Icons.h>
#include <algorithm>

LineflowCanvas::LineflowCanvas(LineflowEditorModel& model, QWidget* parent) : QAbstractItemView(parent), _draft(model)
{
    setObjectName("lineflowCanvas");
    setModel(&model);
    setSelectionMode(SingleSelection);
    setSelectionBehavior(SelectRows);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(ScrollPerPixel);
    setMinimumWidth(300);
    setAccessibleName(tr("Lineflow canvas"));
    connect(&model, &QAbstractItemModel::modelReset, this, [this] {
        _clearDrop();
        updateGeometries();
        viewport()->update();
    });
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { viewport()->update(); });
}

QRect LineflowCanvas::visualRect(const QModelIndex& index) const
{
    for (const auto& item : _items())
    {
        if (item.index == index.siblingAtColumn(0))
        {
            return item.rect.translated(0, -verticalOffset());
        }
    }
    return {};
}

void LineflowCanvas::scrollTo(const QModelIndex& index, ScrollHint hint)
{
    const auto rect = visualRect(index);
    if (!rect.isValid())
    {
        return;
    }
    if (hint == PositionAtTop || rect.top() < 0)
    {
        verticalScrollBar()->setValue(verticalOffset() + rect.top() - 12);
    }
    else if (hint == PositionAtBottom || rect.bottom() > viewport()->height())
    {
        verticalScrollBar()->setValue(verticalOffset() + rect.bottom() - viewport()->height() + 12);
    }
}

QModelIndex LineflowCanvas::indexAt(const QPoint& point) const
{
    const QPoint content = point + QPoint(0, verticalOffset());
    const auto   items   = _items();
    for (auto it = items.crbegin(); it != items.crend(); ++it)
    {
        if (it->rect.contains(content))
        {
            return it->index;
        }
    }
    return {};
}

QList<LineflowCanvas::Port> LineflowCanvas::ports(const QModelIndex& index) const
{
    return _ports(index, visualRect(index));
}

bool LineflowCanvas::dropIndicatorVisible() const
{
    return _dropRow >= 0;
}

void LineflowCanvas::expandAll()
{
    updateGeometries();
    viewport()->update();
}

QModelIndex LineflowCanvas::moveCursor(CursorAction action, Qt::KeyboardModifiers)
{
    const auto items = _items();
    if (items.isEmpty())
    {
        return {};
    }
    int row = 0;
    for (int index = 0; index < items.size(); ++index)
    {
        if (items[index].index == currentIndex())
        {
            row = index;
            break;
        }
    }
    if (action == MoveUp || action == MovePrevious)
    {
        row = qMax(0, row - 1);
    }
    else if (action == MoveDown || action == MoveNext)
    {
        row = qMin(int(items.size()) - 1, row + 1);
    }
    else if (action == MoveHome)
    {
        row = 0;
    }
    else if (action == MoveEnd)
    {
        row = int(items.size()) - 1;
    }
    return items[row].index;
}

int LineflowCanvas::horizontalOffset() const
{
    return 0;
}

int LineflowCanvas::verticalOffset() const
{
    return verticalScrollBar()->value();
}

bool LineflowCanvas::isIndexHidden(const QModelIndex&) const
{
    return false;
}

void LineflowCanvas::setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags flags)
{
    const auto index = indexAt(rect.center());
    if (index.isValid())
    {
        selectionModel()->select(index, flags);
    }
}

QRegion LineflowCanvas::visualRegionForSelection(const QItemSelection& selection) const
{
    QRegion region;
    for (const auto& index : selection.indexes())
    {
        region += visualRect(index);
    }
    return region;
}

void LineflowCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().brush(QPalette::Base));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(0, -verticalOffset());
    const auto items  = _items();
    const bool paired = _draft.blockChannels(_draft.selectedBlock()) == 2;
    int        bottom = 100;
    for (const auto& item : items)
    {
        bottom = qMax(bottom, item.rect.bottom() + qMax(44, fontMetrics().height() + 32));
    }
    const int  left  = _laneRect("rx", 0, 1).center().x();
    const int  right = _laneRect("tx", 0, 1).center().x();
    const auto wire  = palette().color(QPalette::Mid);
    painter.setPen(QPen(wire, 3));
    painter.drawLine(left, 42, left, bottom);
    if (paired)
    {
        painter.drawLine(right, 42, right, bottom);
    }

    const auto endpoint = [&](int y, const QString& text) {
        const QRect band(0, y, _contentWidth(), 32);
        painter.fillRect(band, palette().brush(QPalette::AlternateBase));
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(band.adjusted(12, 0, -12, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRect(left - 16, y, 32, 32), Qt::AlignCenter, tr("Rx"));
        if (paired)
        {
            painter.drawText(QRect(right - 16, y, 32, 32), Qt::AlignCenter, tr("Tx"));
        }
    };
    endpoint(6, tr("Device"));
    endpoint(bottom + 4, tr("Terminal"));

    const auto arrow = [&](int x, int y, bool down) {
        const int direction = down ? 1 : -1;
        painter.setPen(QPen(wire, 2));
        painter.drawLine(x - 4, y - direction * 3, x, y + direction * 3);
        painter.drawLine(x, y + direction * 3, x + 4, y - direction * 3);
    };
    arrow(left, 44, true);
    arrow(left, bottom - 2, true);
    if (paired)
    {
        arrow(right, 44, false);
        arrow(right, bottom - 2, false);
    }

    QList<int> stages;
    for (const auto& item : items)
    {
        if (!item.index.parent().isValid() && !stages.contains(item.rect.top()))
        {
            stages.append(item.rect.top());
        }
    }
    std::sort(stages.begin(), stages.end());
    painter.setPen(palette().color(QPalette::PlaceholderText));
    for (int row = 0; row < stages.size(); ++row)
    {
        painter.drawText(QRect(4, stages[row], 24, _bodyHeight()), Qt::AlignCenter, QString::number(row + 1));
    }
    for (const auto& item : items)
    {
        if (!item.rect.adjusted(0, -_stageGap(), 0, _stageGap())
                 .intersects(QRect(0, verticalOffset(), viewport()->width(), viewport()->height())))
        {
            continue;
        }
        const bool linked     = item.index.data(LineflowEditorModel::LinkedRole).toBool();
        const bool selected   = item.index == currentIndex().siblingAtColumn(0);
        auto       background = palette().color(QPalette::Button);
        const auto ink        = palette().color(QPalette::Text);
        const auto accent     = palette().color(QPalette::Highlight);
        background            = QColor::fromRgb((background.red() * 90 + ink.red() * 6 + accent.red() * 4) / 100,
                                                (background.green() * 90 + ink.green() * 6 + accent.green() * 4) / 100,
                                                (background.blue() * 90 + ink.blue() * 6 + accent.blue() * 4) / 100);
        if (selected)
        {
            const auto highlight = palette().color(QPalette::Highlight);
            background           = QColor::fromRgb((background.red() * 4 + highlight.red()) / 5,
                                                   (background.green() * 4 + highlight.green()) / 5,
                                                   (background.blue() * 4 + highlight.blue()) / 5);
        }
        // Native styles may expose transparent button colors. A node masks the
        // transport wire beneath it even when its palette role is translucent.
        background.setAlpha(255);
        painter.setBrush(background);
        painter.setPen(QPen(selected ? palette().color(QPalette::Highlight) : palette().color(QPalette::Mid), 1));
        painter.drawRoundedRect(item.rect.adjusted(0, 0, -1, -1), 4, 4);
        const auto foreground = palette().color(QPalette::Text);
        painter.setPen(foreground);
        const int  titleHeight = fontMetrics().height() + 4;
        const int  bodyHeight  = linked ? item.rect.height() : _bodyHeight();
        const int  contentTop  = item.rect.top() + (bodyHeight - titleHeight) / 2;
        const auto icon        = qvariant_cast<QIcon>(item.index.data(Qt::DecorationRole));
        icon.paint(&painter, QRect(item.rect.left() + 8, contentTop + (titleHeight - 16) / 2, 16, 16));

        auto titleFont = this->font();
        titleFont.setBold(!linked);
        painter.setFont(titleFont);
        const QRect        titleRect(item.rect.left() + 30, contentTop, qMax(1, item.rect.width() - 60), titleHeight);
        const QString      name      = item.index.data().toString();
        const QString      typeLabel = linked ? QString() : item.index.siblingAtColumn(1).data().toString();
        const QString      type      = typeLabel == name ? QString() : typeLabel;
        const QFontMetrics titleMetrics(titleFont);
        const int          nameWidth = titleMetrics.horizontalAdvance(name);
        const int          typeWidth = fontMetrics().horizontalAdvance(type);
        if (!type.isEmpty() && nameWidth + typeWidth + 8 <= titleRect.width())
        {
            const int start = titleRect.center().x() - (nameWidth + typeWidth + 8) / 2;
            painter.drawText(QRect(start, contentTop, nameWidth, titleHeight), Qt::AlignCenter, name);
            painter.setFont(this->font());
            painter.setPen(palette().color(QPalette::PlaceholderText));
            painter.drawText(QRect(start + nameWidth + 8, contentTop, typeWidth, titleHeight), Qt::AlignCenter, type);
        }
        else
        {
            painter.drawText(titleRect,
                             Qt::AlignCenter,
                             titleMetrics.elidedText(name, Qt::ElideMiddle, titleRect.width()));
        }

        if (!linked)
        {
            if (item.index.data(LineflowEditorModel::InputCountRole).toInt() == 2
                && item.index.data(LineflowEditorModel::OutputCountRole).toInt() == 2)
            {
                const auto flipRect = _flipRect(item.rect);
                painter.setBrush(palette().brush(QPalette::Button));
                painter.setPen(QPen(palette().color(QPalette::Mid), 1));
                painter.drawRoundedRect(flipRect, 3, 3);
                Icons::icon("rotate-cw").paint(&painter, flipRect.adjusted(3, 3, -3, -3));
            }
            for (const auto& port : _ports(item.index, item.rect))
            {
                const int direction = (port.top ? port.input : !port.input) ? 1 : -1;
                const int markerY   = port.marker.center().y();
                painter.setPen(QPen(palette().color(QPalette::Text), 1));
                painter.drawLine(port.point.x() - 4, markerY - direction * 3, port.point.x(), markerY + direction * 3);
                painter.drawLine(port.point.x(), markerY + direction * 3, port.point.x() + 4, markerY - direction * 3);
                painter.setPen(foreground);
                painter.setFont(this->font());
                painter.drawText(port.label,
                                 Qt::AlignCenter,
                                 fontMetrics().elidedText(port.name + (port.input ? tr(" · in") : tr(" · out")),
                                                          Qt::ElideMiddle,
                                                          port.label.width()));
            }
        }
    }
    if (_dropRow >= 0)
    {
        const int  y    = _dropY;
        const auto rect = _laneRect(_dropLane, y - 5, 10);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 3));
        painter.drawLine(rect.left(), y, rect.right(), y);
        painter.setBrush(palette().brush(QPalette::Highlight));
        painter.drawEllipse(QPoint(rect.left(), y), 3, 3);
        painter.drawEllipse(QPoint(rect.right(), y), 3, 3);
    }
}

void LineflowCanvas::resizeEvent(QResizeEvent* event)
{
    QAbstractItemView::resizeEvent(event);
    updateGeometries();
}

void LineflowCanvas::updateGeometries()
{
    int height = 150;
    for (const auto& item : _items())
    {
        height = qMax(height, item.rect.bottom() + qMax(44, fontMetrics().height() + 32) + 40);
    }
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setRange(0, qMax(0, height - viewport()->height()));
}

void LineflowCanvas::startDrag(Qt::DropActions)
{
    if (!currentIndex().isValid() || currentIndex().data(LineflowEditorModel::LinkedRole).toBool())
    {
        return;
    }
    auto* drag = new QDrag(this);
    drag->setMimeData(_draft.mimeData({currentIndex().siblingAtColumn(0)}));
    drag->exec(Qt::MoveAction);
    _clearDrop();
}

void LineflowCanvas::dragEnterEvent(QDragEnterEvent* event)
{
    if (_updateDrop(event->mimeData(), event->position().toPoint()))
    {
        event->acceptProposedAction();
    }
}

void LineflowCanvas::dragMoveEvent(QDragMoveEvent* event)
{
    if (_updateDrop(event->mimeData(), event->position().toPoint()))
    {
        event->acceptProposedAction();
    }
    else
    {
        event->ignore();
    }
}

void LineflowCanvas::dragLeaveEvent(QDragLeaveEvent* event)
{
    _clearDrop();
    event->accept();
}

void LineflowCanvas::dropEvent(QDropEvent* event)
{
    if (_updateDrop(event->mimeData(), event->position().toPoint()))
    {
        int        row   = _dropRow;
        const auto lane  = _dropLane;
        const auto order = _dropOrder;
        if (event->mimeData()->hasFormat("application/x-termaster-lineflow-node"))
        {
            const auto map  = QJsonDocument::fromJson(event->mimeData()->data("application/x-termaster-lineflow-node"))
                                  .toVariant()
                                  .toMap();
            const int  from = order.indexOf(map.value("name").toString());
            if (from >= 0 && from < row)
            {
                --row;
            }
        }
        const int dropRow = _dropRow;
        if (_draft.dropAt(event->mimeData(), dropRow, lane, order))
        {
            setCurrentIndex(_draft.index(row, 0));
            event->acceptProposedAction();
        }
    }
    _clearDrop();
}

void LineflowCanvas::mousePressEvent(QMouseEvent* event)
{
    const auto index = indexAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && index.isValid() && !index.data(LineflowEditorModel::LinkedRole).toBool()
        && index.data(LineflowEditorModel::InputCountRole).toInt() == 2
        && index.data(LineflowEditorModel::OutputCountRole).toInt() == 2
        && _flipRect(visualRect(index)).contains(event->position().toPoint()))
    {
        setCurrentIndex(index);
        const int row = index.row();
        QString   error;
        if (!_draft.setNodeFlipped(_draft.selectedBlock(),
                                   index.data(LineflowEditorModel::NodeRole).toString(),
                                   !index.data(LineflowEditorModel::FlippedRole).toBool(),
                                   &error))
        {
            emit _draft.rejected(error);
        }
        setCurrentIndex(_draft.index(row, 0));
        event->accept();
        return;
    }

    QAbstractItemView::mousePressEvent(event);
}

void LineflowCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    const auto index = indexAt(event->position().toPoint());
    if (index.isValid())
    {
        setCurrentIndex(index);
        emit doubleClicked(index);
    }
}

void LineflowCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete && currentIndex().isValid())
    {
        if (!currentIndex().data(LineflowEditorModel::LinkedRole).toBool())
        {
            const int row = currentIndex().row();
            QString   error;
            if (_draft.removeNode(_draft.selectedBlock(),
                                  currentIndex().data(LineflowEditorModel::NodeRole).toString(),
                                  &error))
            {
                setCurrentIndex(_draft.index(qMin(row, _draft.rowCount() - 1), 0));
            }
            else
            {
                emit _draft.rejected(error);
            }
        }
        event->accept();
        return;
    }
    QAbstractItemView::keyPressEvent(event);
}

bool LineflowCanvas::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
    {
        updateGeometries();
        viewport()->update();
    }
    if (event->type() == QEvent::ToolTip)
    {
        auto*      help   = static_cast<QHelpEvent*>(event);
        const auto target = indexAt(help->pos());
        if (target.isValid() && !target.data(LineflowEditorModel::LinkedRole).toBool()
            && target.data(LineflowEditorModel::InputCountRole).toInt() == 2
            && target.data(LineflowEditorModel::OutputCountRole).toInt() == 2
            && _flipRect(visualRect(target)).contains(help->pos()))
        {
            QToolTip::showText(help->globalPos(), tr("Rotate port positions by 180°"), this);
            return true;
        }
        for (const auto& item : _items())
        {
            for (const auto& port : _ports(item.index, item.rect.translated(0, -verticalOffset())))
            {
                if (port.label.contains(help->pos())
                    || QRect(port.point - QPoint(9, 9), QSize(19, 19)).contains(help->pos()))
                {
                    QToolTip::showText(help->globalPos(),
                                       tr("%1 %2: %3")
                                           .arg(port.input ? tr("Input") : tr("Output"))
                                           .arg(port.index + 1)
                                           .arg(port.name),
                                       this);
                    return true;
                }
            }
        }
        const auto index = indexAt(help->pos());
        QString    text
            = index.isValid() ?
                  index.data(Qt::ToolTipRole).toString() :
                  tr("Device at top; terminal at bottom. Rx flows down the left lane; Tx flows up the right lane.");
        if (index.isValid())
        {
            const auto inputs  = index.data(LineflowEditorModel::InputNamesRole).toStringList();
            const auto outputs = index.data(LineflowEditorModel::OutputNamesRole).toStringList();
            for (int port = 0; port < inputs.size(); ++port)
            {
                text += tr("\nInput %1: %2").arg(port + 1).arg(inputs[port]);
            }
            for (int port = 0; port < outputs.size(); ++port)
            {
                text += tr("\nOutput %1: %2").arg(port + 1).arg(outputs[port]);
            }
            if (index.data(LineflowEditorModel::FlippedRole).toBool())
            {
                text += tr("\nDuplex port orientation is flipped.");
            }
        }
        QToolTip::showText(help->globalPos(), text, this);
        return true;
    }
    return QAbstractItemView::viewportEvent(event);
}

QList<LineflowCanvas::Port> LineflowCanvas::_ports(const QModelIndex& index, const QRect& rect) const
{
    if (!index.isValid() || !rect.isValid() || index.data(LineflowEditorModel::LinkedRole).toBool())
    {
        return {};
    }
    const auto  inputs  = index.data(LineflowEditorModel::InputNamesRole).toStringList();
    const auto  outputs = index.data(LineflowEditorModel::OutputNamesRole).toStringList();
    const auto  lane    = index.data(LineflowEditorModel::LaneRole).toString();
    const bool  duplex  = index.data(LineflowEditorModel::InputCountRole).toInt() == 2
                       && index.data(LineflowEditorModel::OutputCountRole).toInt() == 2;
    const bool  flipped = duplex && index.data(LineflowEditorModel::FlippedRole).toBool();
    QList<Port> result;
    const auto  add = [&](bool input, bool top, int logical, int x, int width) {
        const int     physical = flipped ? logical ^ 1 : logical;
        const auto    names    = input ? inputs : outputs;
        const QString name     = names.value(physical, QString(input ? "Input%1" : "Output%1").arg(physical + 1));
        const QPoint  point(x, top ? rect.top() : rect.bottom());
        const int     height     = fontMetrics().height() + 2;
        const int     halfWidth  = qMin(x - rect.left() - 6, rect.right() - 6 - x);
        const int     labelWidth = qMax(1, qMin(width, 2 * halfWidth));
        const QRect   label(x - labelWidth / 2, top ? rect.top() + 6 : rect.bottom() - height - 6, labelWidth, height);
        const int     markerY = point.y();
        const QRect   marker(point.x() - 4, markerY - 3, 9, 7);
        result.append({name, physical, input, top, point, marker, label});
    };
    if (duplex)
    {
        const int left  = _laneRect("rx", 0, 1).center().x();
        const int right = _laneRect("tx", 0, 1).center().x();
        const int width = qMax(24, rect.width() / 2 - 20);
        add(true, true, 0, left, width);
        add(false, true, 1, right, width);
        add(false, false, 0, left, width);
        add(true, false, 1, right, width);
    }
    else
    {
        const bool tx    = lane == "tx";
        const int  width = qMax(24, rect.width() - 12);
        add(!tx, true, 0, rect.center().x(), width);
        add(tx, false, 0, rect.center().x(), width);
    }
    return result;
}

int LineflowCanvas::_stageGap() const
{
    return 8;
}

int LineflowCanvas::_bodyHeight() const
{
    return qMax(62, 3 * fontMetrics().height() + 14);
}

QRect LineflowCanvas::_flipRect(const QRect& rect) const
{
    return QRect(rect.right() - 28, rect.top() + (_bodyHeight() - 22) / 2, 22, 22);
}

int LineflowCanvas::_contentWidth() const
{
    return viewport()->width();
}

int LineflowCanvas::_linkedCount(const QModelIndex& index) const
{
    int result = _draft.rowCount(index);
    for (int row = 0; row < _draft.rowCount(index); ++row)
    {
        result += _linkedCount(_draft.index(row, 0, index));
    }
    return result;
}

void LineflowCanvas::_appendLinked(QList<Item>& items, const QModelIndex& parent, const QRect& card, int& y) const
{
    for (int row = 0; row < _draft.rowCount(parent); ++row)
    {
        const auto index  = _draft.index(row, 0, parent);
        const int  height = qMax(38, fontMetrics().height() + 16);
        items.append({index, QRect(card.left() + 10, y, card.width() - 20, height)});
        y += height + 4;
        _appendLinked(items, index, card, y);
    }
}

QList<LineflowCanvas::Item> LineflowCanvas::_items() const
{
    QList<Item> result;
    const bool  paired = _draft.blockChannels(_draft.selectedBlock()) == 2;
    int         yRx    = 58;
    int         yTx    = yRx;
    for (int row = 0; row < _draft.rowCount(); ++row)
    {
        const auto index        = _draft.index(row, 0);
        const auto lane         = index.data(LineflowEditorModel::LaneRole).toString();
        const bool duplex       = paired && lane == "duplex";
        const bool tx           = paired && lane == "tx";
        const int  y            = duplex ? qMax(yRx, yTx) : tx ? yTx : yRx;
        const int  cardHeight   = _bodyHeight();
        const int  linkedHeight = qMax(38, fontMetrics().height() + 16);
        const int  linkedCount  = _linkedCount(index);
        const int  footerHeight = linkedCount > 0 ? fontMetrics().height() + 12 : 0;
        const int  height       = cardHeight + linkedCount * (linkedHeight + 4) + footerHeight;
        const auto rect         = _laneRect(lane, y, height);
        result.append({index, rect});
        int childY = y + cardHeight;
        _appendLinked(result, index, rect, childY);
        if (duplex || !tx)
        {
            yRx = y + height + _stageGap();
        }
        if (duplex || tx)
        {
            yTx = y + height + _stageGap();
        }
    }
    return result;
}

QRect LineflowCanvas::_laneRect(const QString& lane, int y, int height) const
{
    const int width = _contentWidth();
    const int left  = 36;
    const int right = width - 12;
    if (_draft.blockChannels(_draft.selectedBlock()) != 2 || lane == "duplex")
    {
        return QRect(left, y, qMax(30, right - left), height);
    }

    const int laneWidth = qMax(30, (right - left - 6) / 2);
    return QRect(lane == "tx" ? left + laneWidth + 6 : left, y, laneWidth, height);
}

QStringList LineflowCanvas::_visualOrder() const
{
    auto items = _items();
    items.removeIf([](const Item& item) { return item.index.parent().isValid(); });
    std::stable_sort(items.begin(), items.end(), [](const Item& left, const Item& right) {
        return left.rect.top() < right.rect.top();
    });
    QStringList order;
    for (const auto& item : items)
    {
        order.append(item.index.data(LineflowEditorModel::NodeRole).toString());
    }
    return order;
}

int LineflowCanvas::_rowAt(int y, const QString& lane, const QStringList& order) const
{
    QMap<QString, Item> items;
    for (const auto& item : _items())
    {
        if (!item.index.parent().isValid())
        {
            items.insert(item.index.data(LineflowEditorModel::NodeRole).toString(), item);
        }
    }
    for (int row = 0; row < order.size(); ++row)
    {
        const auto item     = items.value(order[row]);
        const auto itemLane = item.index.data(LineflowEditorModel::LaneRole).toString();
        if ((lane == "duplex" || itemLane == lane || itemLane == "duplex") && y < item.rect.center().y())
        {
            return row;
        }
    }
    return int(order.size());
}

int LineflowCanvas::_insertionY(int row, const QString& lane, const QStringList& order) const
{
    QMap<QString, Item> items;
    for (const auto& item : _items())
    {
        if (!item.index.parent().isValid())
        {
            items.insert(item.index.data(LineflowEditorModel::NodeRole).toString(), item);
        }
    }
    int y = 58 - _stageGap() / 2;
    for (int index = 0; index < row; ++index)
    {
        const auto item     = items.value(order[index]);
        const auto itemLane = item.index.data(LineflowEditorModel::LaneRole).toString();
        if (lane == "duplex" || itemLane == lane || itemLane == "duplex")
        {
            y = qMax(y, item.rect.bottom() + _stageGap() / 2);
        }
    }
    return y;
}

QString LineflowCanvas::_laneAt(const QMimeData* data, int x) const
{
    QString target;
    if (data->hasFormat("application/x-termaster-processor"))
    {
        target = QString::fromUtf8(data->data("application/x-termaster-processor"));
    }
    else
    {
        const auto
            map = QJsonDocument::fromJson(data->data("application/x-termaster-lineflow-node")).toVariant().toMap();
        const auto entry = _draft.node(_draft.selectedBlock(), map.value("name").toString());
        target           = entry.value(entry.contains("reference") ? "reference" : "type").toString();
    }
    const auto shape = _draft.processorShape(target);
    if (shape.first == 2 && shape.second == 2)
    {
        return "duplex";
    }
    const int boundary = (_laneRect("rx", 0, 1).right() + _laneRect("tx", 0, 1).left()) / 2;
    return _draft.blockChannels(_draft.selectedBlock()) == 2 && x > boundary ? "tx" : "rx";
}

bool LineflowCanvas::_updateDrop(const QMimeData* data, const QPoint& point)
{
    const auto lane  = _laneAt(data, point.x());
    const auto order = _visualOrder();
    const int  row   = _rowAt(point.y() + verticalOffset(), lane, order);
    if (!_draft.canDropAt(data, row, lane, order))
    {
        _clearDrop();
        return false;
    }
    _dropRow   = row;
    _dropLane  = lane;
    _dropOrder = order;
    _dropY     = _insertionY(row, lane, order);
    viewport()->update();
    return true;
}

void LineflowCanvas::_clearDrop()
{
    _dropRow = -1;
    _dropLane.clear();
    _dropOrder.clear();
    viewport()->update();
}
