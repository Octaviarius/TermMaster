#pragma once

#include <QStyledItemDelegate>

/** Keeps monochrome item icons legible on the style's actual selected background.
 * Unselected and disabled rendering remains with the standard Qt delegate.
 */
class SelectionIconDelegate final : public QStyledItemDelegate
{
public:
    explicit SelectionIconDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
