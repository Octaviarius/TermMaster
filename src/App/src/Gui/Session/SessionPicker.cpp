#include "SessionPicker.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace
{
class SessionDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        return {460, option.fontMetrics.height() * 2 + 12};
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        auto item = option;
        initStyleOption(&item, index);
        item.text.clear();
        const bool selected = item.state.testFlag(QStyle::State_Selected);
        if (selected)
        {
            item.backgroundBrush  = item.palette.highlight();
            item.state           &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        }
        const auto* style = item.widget ? item.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
        const auto   foreground = item.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        const auto   background = item.palette.color(selected ? QPalette::Highlight : QPalette::Base);
        const QColor secondary  = selected ? foreground :
                                             QColor((foreground.red() * 3 + background.red()) / 4,
                                                    (foreground.green() * 3 + background.green()) / 4,
                                                    (foreground.blue() * 3 + background.blue()) / 4);
        auto         rect       = item.rect.adjusted(8, 4, -8, -4);
        painter->save();
        painter->setFont(item.font);
        painter->setPen(foreground);
        painter->drawText(rect,
                          Qt::AlignTop | Qt::AlignLeft,
                          item.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, rect.width()));
        painter->setPen(secondary);
        painter->drawText(rect,
                          Qt::AlignBottom | Qt::AlignLeft,
                          item.fontMetrics.elidedText(index.data(Qt::UserRole).toString(),
                                                      Qt::ElideRight,
                                                      rect.width()));
        painter->restore();
    }
};
} // namespace

SessionPicker::SessionPicker(const QList<Entry>& entries, QWidget* parent) :
    QDialog(parent), _sessions(new QComboBox(this))
{
    setObjectName("sessionPicker");
    setWindowTitle(tr("Open session"));
    auto* layout = new QVBoxLayout(this);
    auto* label  = new QLabel(tr("Session:"), this);
    label->setBuddy(_sessions);
    layout->addWidget(label);
    _sessions->setObjectName("sessionChoice");
    _sessions->setItemDelegate(new SessionDelegate(_sessions));
    _sessions->setLabelDrawingMode(QComboBox::LabelDrawingMode::UseDelegate);
    _sessions->setMinimumHeight(fontMetrics().height() * 2 + 16);
    _sessions->setMinimumContentsLength(40);
    for (const auto& entry : entries)
    {
        _sessions->addItem(entry.name, entry.id);
        _sessions->setItemData(_sessions->count() - 1, entry.name + "\n" + entry.id, Qt::ToolTipRole);
        _sessions->setItemData(_sessions->count() - 1, entry.name + ", " + entry.id, Qt::AccessibleTextRole);
    }
    layout->addWidget(_sessions);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(540, sizeHint().height());
}
QString SessionPicker::selectedSession() const
{
    return _sessions->currentData().toString();
}
