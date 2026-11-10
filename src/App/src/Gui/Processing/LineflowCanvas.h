#pragma once

#include <QAbstractItemView>

class LineflowEditorModel;

/** Native-style canvas over the authoritative draft model. Layout is derived, never persisted. */
class LineflowCanvas final : public QAbstractItemView
{
    Q_OBJECT
public:
    struct Port
    {
        QString name;
        int     index = 0;
        bool    input = false;
        bool    top   = false;
        QPoint  point;
        QRect   marker;
        QRect   label;
    };
    explicit LineflowCanvas(LineflowEditorModel& model, QWidget* parent = nullptr);
    QRect       visualRect(const QModelIndex& index) const override;
    void        scrollTo(const QModelIndex& index, ScrollHint hint = EnsureVisible) override;
    QModelIndex indexAt(const QPoint& point) const override;
    QList<Port> ports(const QModelIndex& index) const;
    bool        dropIndicatorVisible() const;
    void        expandAll();

protected:
    QModelIndex moveCursor(CursorAction action, Qt::KeyboardModifiers modifiers) override;
    int         horizontalOffset() const override;
    int         verticalOffset() const override;
    bool        isIndexHidden(const QModelIndex& index) const override;
    void        setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags flags) override;
    QRegion     visualRegionForSelection(const QItemSelection& selection) const override;
    void        paintEvent(QPaintEvent* event) override;
    void        resizeEvent(QResizeEvent* event) override;
    void        updateGeometries() override;
    void        startDrag(Qt::DropActions actions) override;
    void        dragEnterEvent(QDragEnterEvent* event) override;
    void        dragMoveEvent(QDragMoveEvent* event) override;
    void        dragLeaveEvent(QDragLeaveEvent* event) override;
    void        dropEvent(QDropEvent* event) override;
    void        mousePressEvent(QMouseEvent* event) override;
    void        mouseDoubleClickEvent(QMouseEvent* event) override;
    void        keyPressEvent(QKeyEvent* event) override;
    bool        viewportEvent(QEvent* event) override;

private:
    struct Item
    {
        QModelIndex index;
        QRect       rect;
    };
    LineflowEditorModel& _draft;
    int                  _dropRow = -1;
    QString              _dropLane;
    QStringList          _dropOrder;
    int                  _dropY = 0;
    QList<Port>          _ports(const QModelIndex& index, const QRect& rect) const;
    int                  _stageGap() const;
    int                  _bodyHeight() const;
    QRect                _flipRect(const QRect& rect) const;
    int                  _contentWidth() const;
    QList<Item>          _items() const;
    void                 _appendLinked(QList<Item>& items, const QModelIndex& parent, const QRect& card, int& y) const;
    int                  _linkedCount(const QModelIndex& index) const;
    QRect                _laneRect(const QString& lane, int y, int height) const;
    QStringList          _visualOrder() const;
    int                  _rowAt(int y, const QString& lane, const QStringList& order) const;
    int                  _insertionY(int row, const QString& lane, const QStringList& order) const;
    QString              _laneAt(const QMimeData* data, int x) const;
    bool                 _updateDrop(const QMimeData* data, const QPoint& point);
    void                 _clearDrop();
};
