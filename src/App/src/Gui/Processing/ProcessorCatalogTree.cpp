#include "ProcessorCatalogTree.h"

#include "ProcessorFavorites.h"

#include <QMenu>
#include <QPainter>
#include <QToolTip>
#include <QTreeWidgetItemIterator>
#include <Termaster/Core/Gui/Icons.h>
#include <Termaster/Core/Gui/SelectionIconDelegate.h>

namespace
{
class CatalogDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (!index.data(ProcessorCatalogTree::SeparatorRole).toBool())
        {
            _icons.paint(painter, option, index);
            return;
        }
        painter->save();
        painter->setPen(QPen(option.palette.color(QPalette::Mid), 1, Qt::DashLine));
        painter->drawLine(option.rect.left() + 4,
                          option.rect.center().y(),
                          option.rect.right() - 4,
                          option.rect.center().y());
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        return index.data(ProcessorCatalogTree::SeparatorRole).toBool() ? QSize(1, 10) :
                                                                          QStyledItemDelegate::sizeHint(option, index);
    }

private:
    SelectionIconDelegate _icons;
};
} // namespace

ProcessorCatalogTree::ProcessorCatalogTree(QWidget* parent) : QTreeWidget(parent)
{
    setIndentation(12);

    setItemDelegate(new CatalogDelegate(this));
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &ProcessorCatalogTree::_menu);
}

void ProcessorCatalogTree::setFavorites(ProcessorFavorites* favorites)
{
    if (_favorites)
    {
        disconnect(_favorites, nullptr, this, nullptr);
    }
    _favorites = favorites;
    if (_favorites)
    {
        connect(_favorites, &ProcessorFavorites::changed, this, &ProcessorCatalogTree::favoritesChanged);
    }
}

void ProcessorCatalogTree::prependFavorites(const QString& query, const QStringList& excluded)
{
    const auto ids     = _favorites ? _favorites->ids() : QStringList {"native.bypass"};
    const auto current = currentItem() ? currentItem()->data(0, Qt::UserRole).toString() : QString {};
    QMap<QString, QTreeWidgetItem*> items;
    for (QTreeWidgetItemIterator it(this); *it; ++it)
    {
        const auto id = (*it)->data(0, Qt::UserRole).toString();
        if (!id.isEmpty())
        {
            items.insert(id, *it);
        }
    }
    int row = 0;
    for (const auto& id : ids)
    {
        if (excluded.contains(id))
        {
            continue;
        }

        auto* source = items.value(id);
        if (!source && !query.isEmpty() && !id.contains(query, Qt::CaseInsensitive))
        {
            continue;
        }
        auto* item = source ? new QTreeWidgetItem(*source) :
                              new QTreeWidgetItem(QStringList {tr("Unavailable: %1").arg(id)});
        item->setData(0, Qt::UserRole, id);
        item->setData(0, FavoriteRole, true);
        if (!source)
        {
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setIcon(0, Icons::icon("triangle-exclamation-point"));
            item->setToolTip(0, tr("Processor is unavailable: %1").arg(id));
        }
        insertTopLevelItem(row++, item);
        if (id == current)
        {
            setCurrentItem(item);
        }
    }
    if (row > 0)
    {
        auto* separator = new QTreeWidgetItem;
        separator->setFlags(Qt::NoItemFlags);
        separator->setData(0, SeparatorRole, true);
        insertTopLevelItem(row, separator);
    }
}

void ProcessorCatalogTree::_menu(const QPoint& position)
{
    if (!_favorites)
    {
        return;
    }
    auto* item = itemAt(position);
    if (!item)
    {
        return;
    }
    const auto id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty())
    {
        return;
    }
    const bool                           favorite = _favorites->contains(id);
    const QPointer<ProcessorCatalogTree> self(this);
    const QPointer<ProcessorFavorites>   favorites      = _favorites;
    const auto                           globalPosition = viewport()->mapToGlobal(position);
    QMenu                                menu;
    auto*    toggle = menu.addAction(favorite ? tr("Remove from favorites") : tr("Add to favorites"));
    QAction* up     = nullptr;
    QAction* down   = nullptr;
    if (favorite)
    {
        menu.addSeparator();
        const auto index = _favorites->ids().indexOf(id);
        up               = menu.addAction(tr("Move favorite up"));
        down             = menu.addAction(tr("Move favorite down"));
        up->setEnabled(index > 0);
        down->setEnabled(index + 1 < _favorites->ids().size());
    }
    auto* selected = menu.exec(globalPosition);
    if (!self || !favorites)
    {
        return;
    }
    QString error;
    bool    success = true;
    if (selected == toggle)
    {
        success = _favorites->setFavorite(id, !favorite, &error);
    }
    else if (selected && (selected == up || selected == down))
    {
        success = _favorites->move(id, selected == up ? -1 : 1, &error);
    }
    if (!self)
    {
        return;
    }
    if (!success)
    {
        QToolTip::showText(globalPosition, error, this);
    }
}
