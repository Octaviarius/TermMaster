#include "ProcessorSelector.h"

#include "ProcessorCatalogTree.h"

#include <QApplication>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMap>
#include <QScreen>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Icons.h>
#include <Termaster/Core/Gui/SelectionIconDelegate.h>
#include <algorithm>

ProcessorSelector::ProcessorSelector(QWidget* parent, ProcessorFavorites* favorites) : QComboBox(parent)
{
    _popup = new QFrame(this, Qt::Popup);
    _popup->setObjectName("processorSelectorPopup");
    _popup->setFrameShape(QFrame::StyledPanel);
    auto* layout = new QVBoxLayout(_popup);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    _search = new QLineEdit(_popup);
    _search->setObjectName("processorSelectorSearch");
    _search->setPlaceholderText(tr("Search processors…"));
    _tree = new ProcessorCatalogTree(_popup);
    _tree->setObjectName("processorSelectorTree");
    _tree->setFavorites(favorites);
    connect(_tree, &ProcessorCatalogTree::favoritesChanged, this, &ProcessorSelector::_rebuild);
    _tree->setHeaderHidden(true);
    _tree->setUniformRowHeights(false);
    _tree->setIndentation(16);
    _tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(_search);
    layout->addWidget(_tree);
    _popup->installEventFilter(this);
    _tree->installEventFilter(this);
    _search->installEventFilter(this);
    connect(_search, &QLineEdit::textChanged, this, [this] { _rebuild(); });
    connect(_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole).isValid())
        {
            _accept(item);
        }
        else
        {
            item->setExpanded(!item->isExpanded());
        }
    });
}

void ProcessorSelector::setCatalog(QList<ProcessorCatalog::Type> types, const QString& selectedId)
{
    // Store metadata only: UI catalogs must never retain executable plugin factories.
    for (auto& type : types)
    {
        type.create = {};
    }
    _types = std::move(types);
    std::sort(_types.begin(), _types.end(), [](const auto& left, const auto& right) {
        if ((left.id == "native.bypass") != (right.id == "native.bypass"))
        {
            return left.id == "native.bypass";
        }
        const auto compared = QString::localeAwareCompare(left.title, right.title);
        return compared == 0 ? left.id < right.id : compared < 0;
    });
    clear();
    addItem(Icons::icon("workflow"), tr("Bypass"), QStringLiteral("native.bypass"));
    for (const auto& type : _types)
    {
        if (type.id == "native.bypass")
        {
            continue;
        }
        const bool local = type.origin == "workspace";
        addItem(Icons::icon(!type.available ? "triangle-exclamation-point" :
                            local           ? "blocks" :
                                              "workflow"),
                type.title,
                type.id);
        setItemData(count() - 1, type.available ? type.id : type.unavailableReason, Qt::ToolTipRole);
    }
    auto row = findData(selectedId.isEmpty() ? QStringLiteral("native.bypass") : selectedId);
    if (row < 0)
    {
        addItem(Icons::icon("triangle-exclamation-point"), tr("Unavailable: %1").arg(selectedId), selectedId);
        row = count() - 1;
    }
    setCurrentIndex(row);
    _rebuild();
}

void ProcessorSelector::showPopup()
{
    if (_popup->isVisible())
    {
        return;
    }
    _search->clear();
    _rebuild();
    const auto point  = mapToGlobal(QPoint(0, height()));
    auto*      screen = QGuiApplication::screenAt(point);
    if (!screen)
    {
        screen = this->screen();
    }
    const auto available = screen->availableGeometry().adjusted(2, 2, -2, -2);
    const auto size = QSize(std::min(std::max(width(), 320), available.width()), std::min(360, available.height()));
    auto       top  = point.y();
    if (top + size.height() > available.bottom() && mapToGlobal(QPoint()).y() - size.height() >= available.top())
    {
        top = mapToGlobal(QPoint()).y() - size.height();
    }
    _popup->setGeometry(QRect(QPoint(std::clamp(point.x(), available.left(), available.right() - size.width() + 1),
                                     std::clamp(top, available.top(), available.bottom() - size.height() + 1)),
                              size));
    _popup->show();
    _tree->setFocus(Qt::PopupFocusReason);
}

void ProcessorSelector::hidePopup()
{
    _popup->hide();
    QComboBox::hidePopup();
}

bool ProcessorSelector::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _popup && event->type() == QEvent::Hide)
    {
        QComboBox::hidePopup();
    }
    if ((watched == _tree || watched == _search) && event->type() == QEvent::KeyPress)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape)
        {
            hidePopup();
            setFocus(Qt::PopupFocusReason);
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter
            || (watched == _tree && key->key() == Qt::Key_Space))
        {
            auto* item = _tree->currentItem();
            if (item && item->data(0, Qt::UserRole).isValid())
            {
                _accept(item);
            }
            else if (item)
            {
                item->setExpanded(!item->isExpanded());
            }
            return true;
        }
        if (watched == _search && key->key() == Qt::Key_Down)
        {
            _tree->setFocus();
            return true;
        }
    }
    return QComboBox::eventFilter(watched, event);
}

void ProcessorSelector::_rebuild()
{
    _tree->clear();
    const auto                      selected = currentData().toString();
    const auto                      query    = _search->text().trimmed();
    QTreeWidgetItem*                current  = nullptr;
    QMap<QString, QTreeWidgetItem*> roots;
    QMap<QString, QTreeWidgetItem*> groups;
    for (const auto& origin : {QStringLiteral("Native"), QStringLiteral("Workspace")})
    {
        auto* root = new QTreeWidgetItem(_tree, {origin});
        root->setFlags(Qt::ItemIsEnabled);
        auto font = root->font(0);
        font.setBold(true);
        root->setFont(0, font);
        root->setExpanded(true);
        roots.insert(origin, root);
    }
    for (const auto& type : _types)
    {
        const auto origin = type.origin == "workspace" ? QStringLiteral("Workspace") : QStringLiteral("Native");
        const auto group  = type.group.isEmpty() ? tr("General") : type.group;
        if (!(origin + ' ' + group + ' ' + type.title + ' ' + type.id).contains(query, Qt::CaseInsensitive))
        {
            continue;
        }
        const auto key    = origin + QChar::Null + group;
        auto*      parent = groups.value(key);
        if (!parent)
        {
            parent = new QTreeWidgetItem(roots.value(origin), {group});
            parent->setFlags(Qt::ItemIsEnabled);
            parent->setExpanded(!query.isEmpty());
            groups.insert(key, parent);
        }
        auto* item = new QTreeWidgetItem(parent, {type.title});
        item->setData(0, Qt::UserRole, type.id);
        item->setIcon(0, itemIcon(findData(type.id)));
        item->setToolTip(0, type.available ? type.id : type.unavailableReason);
        if (type.id == selected)
        {
            current = item;
            parent->setExpanded(true);
        }
    }
    for (auto* root : roots)
    {
        root->setHidden(root->childCount() == 0);
        root->sortChildren(0, Qt::AscendingOrder);
    }
    if (!selected.isEmpty() && selected != "native.bypass" && findData(selected) >= 0
        && std::none_of(_types.begin(), _types.end(), [&](const auto& type) { return type.id == selected; }))
    {
        current = new QTreeWidgetItem(_tree, {currentText()});
        current->setData(0, Qt::UserRole, selected);
        current->setIcon(0, Icons::icon("triangle-exclamation-point"));
    }
    if (!current)
    {
        current = nullptr;
        for (QTreeWidgetItemIterator it(_tree); *it; ++it)
        {
            if ((*it)->data(0, Qt::UserRole).isValid())
            {
                current = *it;
                break;
            }
        }
    }
    _tree->setCurrentItem(current);
    _tree->prependFavorites(query);
    if (_tree->currentItem())
    {
        _tree->scrollToItem(_tree->currentItem());
    }
}

void ProcessorSelector::_accept(QTreeWidgetItem* item)
{
    const auto value = item->data(0, Qt::UserRole);
    if (!value.isValid())
    {
        return;
    }
    auto row = findData(value);
    if (row < 0)
    {
        addItem(Icons::icon("triangle-exclamation-point"), item->text(0), value);
        row = count() - 1;
    }
    setCurrentIndex(row);
    hidePopup();
    setFocus(Qt::PopupFocusReason);
    emit activated(row);
}
