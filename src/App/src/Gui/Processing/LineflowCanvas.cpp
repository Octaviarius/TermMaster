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
    const int  left  = paired ? _contentWidth() / 4 : _contentWidth() / 2;
    const int  right = _contentWidth() * 3 / 4;
    const auto wire  = palette().color(QPalette::Mid);
    painter.setPen(QPen(wire, 2));
    painter.drawLine(left, 40, left, bottom);
    if (paired)
    {
        painter.drawLine(right, 40, right, bottom);
    }
    const auto endpoint = [&](int y, const QString& icon) {
        const QRect rect(_contentWidth() / 2 - 12, y - 12, 24, 24);
        painter.fillRect(rect.adjusted(-8, -2, 8, 2), palette().brush(QPalette::Base));
        Icons::icon(icon).paint(&painter, rect);
    };
    endpoint(22, "cable");
    endpoint(bottom + 16, "terminal");
    const auto arrow = [&](int x, int y, bool down) {
        const int direction = down ? 1 : -1;
        painter.setPen(QPen(wire, 2));
        painter.drawLine(x - 4, y - direction * 4, x, y);
        painter.drawLine(x + 4, y - direction * 4, x, y);
    };
    arrow(left, 50, true);
    arrow(left, bottom - 4, true);
    if (paired)
    {
        arrow(right, 50, false);
        arrow(right, bottom - 4, false);
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
        auto       background = selected ? palette().color(QPalette::Highlight) :
                                linked   ? palette().color(QPalette::AlternateBase) :
                                           palette().color(QPalette::Button);
        // Native styles may expose transparent button colors. A node masks the
        // transport wire beneath it even when its palette role is translucent.
        background.setAlpha(255);
        painter.setBrush(background);
        painter.setPen(QPen(selected ? palette().color(QPalette::Highlight) : palette().color(QPalette::Mid), 1));
        painter.drawRoundedRect(item.rect.adjusted(0, 0, -1, -1), 4, 4);
        const auto foreground = selected ? palette().color(QPalette::HighlightedText) : palette().color(QPalette::Text);
        painter.setPen(foreground);
        const int  titleHeight = fontMetrics().height() + 4;
        const int  contentTop  = item.rect.top() + (linked ? 8 : fontMetrics().height() + 16);
        const auto icon        = qvariant_cast<QIcon>(item.index.data(Qt::DecorationRole));
        icon.paint(&painter,
                   QRect(item.rect.left() + 10, contentTop + (titleHeight - 18) / 2, 18, 18),
                   Qt::AlignCenter,
                   selected ? QIcon::Selected : QIcon::Normal);
        const bool flipped    = item.index.data(LineflowEditorModel::FlippedRole).toBool();
        const int  titleWidth = qMax(20, item.rect.width() - (flipped && !linked ? 70 : 48));
        auto       font       = painter.font();
        font.setBold(!linked);
        painter.setFont(font);
        const auto titleRect = QRect(item.rect.left() + 36, contentTop, titleWidth, titleHeight);
        painter.drawText(titleRect,
                         Qt::AlignVCenter,
                         QFontMetrics(font).elidedText(item.index.data().toString(), Qt::ElideMiddle, titleWidth));
        if (!linked)
        {
            font.setBold(false);
            painter.setFont(font);
            painter.drawText(QRect(item.rect.left() + 12,
                                   contentTop + 4 + titleHeight,
                                   item.rect.width() - 24,
                                   fontMetrics().height() + 4),
                             Qt::AlignVCenter,
                             fontMetrics().elidedText(item.index.siblingAtColumn(1).data().toString(),
                                                      Qt::ElideRight,
                                                      item.rect.width() - 24));
            if (flipped)
            {
                Icons::icon("rotate-cw")
                    .paint(&painter,
                           QRect(item.rect.right() - 25, contentTop + (titleHeight - 16) / 2, 16, 16),
                           Qt::AlignCenter,
                           selected ? QIcon::Selected : QIcon::Normal);
            }
            for (const auto& port : _ports(item.index, item.rect))
            {
                const int direction = (port.top ? port.input : !port.input) ? 1 : -1;
                const int markerY   = port.marker.center().y();
                painter.setPen(QPen(palette().color(QPalette::Text), 1.5));
                painter.drawLine(port.point.x() - 4, markerY - direction * 3, port.point.x(), markerY + direction * 3);
                painter.drawLine(port.point.x(), markerY + direction * 3, port.point.x() + 4, markerY - direction * 3);
                painter.setPen(foreground);
                painter.setFont(this->font());
                painter.drawText(port.label,
                                 Qt::AlignCenter,
                                 fontMetrics().elidedText(port.name, Qt::ElideMiddle, port.label.width()));
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
        height = qMax(height, item.rect.bottom() + 80);
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
        auto* help = static_cast<QHelpEvent*>(event);
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
        const int     markerY = point.y() + (top ? -6 : 6);
        const QRect   marker(point.x() - 4, markerY - 3, 9, 7);
        result.append({name, physical, input, top, point, marker, label});
    };
    if (duplex)
    {
        const int left  = _contentWidth() / 4;
        const int right = _contentWidth() * 3 / 4;
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
    return qMax(28, fontMetrics().height() + 10);
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
    int         yRx    = 74;
    int         yTx    = yRx;
    for (int row = 0; row < _draft.rowCount(); ++row)
    {
        const auto index        = _draft.index(row, 0);
        const auto lane         = index.data(LineflowEditorModel::LaneRole).toString();
        const bool duplex       = paired && lane == "duplex";
        const bool tx           = paired && lane == "tx";
        const int  y            = duplex ? qMax(yRx, yTx) : tx ? yTx : yRx;
        const int  cardHeight   = qMax(104, fontMetrics().height() * 4 + 44);
        const int  linkedHeight = qMax(38, fontMetrics().height() + 16);
        const int  height       = cardHeight + _linkedCount(index) * (linkedHeight + 4);
        const auto rect         = _laneRect(lane, y, height);
        result.append({index, rect});
        int childY = y + cardHeight - fontMetrics().height() - 14;
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
    const int width  = _contentWidth();
    const int margin = 24;
    if (_draft.blockChannels(_draft.selectedBlock()) != 2 || lane == "duplex")
    {
        return QRect(margin, y, qMax(30, width - 2 * margin), height);
    }
    const int half = width / 2;
    return QRect(lane == "tx" ? half + margin : margin, y, qMax(30, half - 2 * margin), height);
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
    int y = 74 - _stageGap() / 2;
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
    return _draft.blockChannels(_draft.selectedBlock()) == 2 && x > _contentWidth() / 2 ? "tx" : "rx";
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
