#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QStyle>
#include <QWidget>
#include <Termaster/Core/Gui/SelectionIconDelegate.h>
#include <algorithm>
#include <cmath>

namespace
{
qreal luminance(const QColor& color)
{
    const auto channel = [](qreal value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}
qreal contrast(const QColor& first, const QColor& second)
{
    const auto left  = luminance(first);
    const auto right = luminance(second);
    return (std::max(left, right) + 0.05) / (std::min(left, right) + 0.05);
}
QColor background(const QStyleOptionViewItem& option, QStyle& style, const QRect& decoration)
{
    const auto group = option.state.testFlag(QStyle::State_Active) ? QPalette::Active : QPalette::Inactive;
    // Render only a tiny sample while retaining the complete style-option geometry.
    QImage sample(3, 3, QImage::Format_ARGB32_Premultiplied);
    sample.fill(option.palette.color(group, QPalette::Base));
    QPainter painter(&sample);
    painter.translate(QPoint(1, 1) - decoration.center());
    style.drawPrimitive(QStyle::PE_PanelItemViewItem, &option, &painter, option.widget);
    painter.end();
    return sample.pixelColor(1, 1);
}
QColor foreground(const QStyleOptionViewItem& option, const QColor& selectedBackground)
{
    const auto group       = option.state.testFlag(QStyle::State_Active) ? QPalette::Active : QPalette::Inactive;
    const auto highlighted = option.palette.color(group, QPalette::HighlightedText);
    if (contrast(highlighted, selectedBackground) >= 3.0)
    {
        return highlighted;
    }
    auto best = highlighted;
    for (const auto& candidate : {option.palette.color(group, QPalette::Text), QColor(Qt::black), QColor(Qt::white)})
    {
        if (contrast(candidate, selectedBackground) > contrast(best, selectedBackground))
        {
            best = candidate;
        }
    }
    return best;
}
} // namespace

SelectionIconDelegate::SelectionIconDelegate(QObject* parent) : QStyledItemDelegate(parent)
{
}

void SelectionIconDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!option.state.testFlag(QStyle::State_Selected) || !option.state.testFlag(QStyle::State_Enabled))
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    auto selected = option;
    initStyleOption(&selected, index);
    if (selected.icon.isNull())
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    auto*      style      = selected.widget ? selected.widget->style() : QApplication::style();
    const auto decoration = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &selected, selected.widget);
    if (!decoration.isEmpty())
    {
        const auto dpr   = painter->device()->devicePixelRatioF();
        const auto state = selected.state.testFlag(QStyle::State_Open) ? QIcon::On : QIcon::Off;
        auto       mask  = selected.icon.pixmap(selected.decorationSize, dpr, QIcon::Normal, state);
        if (!mask.isNull())
        {
            QPainter tint(&mask);
            tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tint.fillRect(QRect(QPoint(), mask.deviceIndependentSize().toSize()),
                          foreground(selected, background(selected, *style, decoration)));
            tint.end();
            QIcon icon;
            icon.addPixmap(mask, QIcon::Normal, state);
            icon.addPixmap(mask, QIcon::Active, state);
            icon.addPixmap(mask, QIcon::Selected, state);
            selected.icon = icon;
        }
    }
    style->drawControl(QStyle::CE_ItemViewItem, &selected, painter, selected.widget);
}
