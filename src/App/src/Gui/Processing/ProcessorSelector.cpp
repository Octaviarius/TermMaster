#include "ProcessorSelector.h"

#include "ProcessorCatalogTree.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMap>
#include <QScreen>
#include <QSignalBlocker>
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
    _tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(_search);
    layout->addWidget(_tree);
    _popup->installEventFilter(this);
    _tree->installEventFilter(this);
    _tree->viewport()->installEventFilter(this);
    _search->installEventFilter(this);
    connect(_search, &QLineEdit::textChanged, this, [this] { _filter(); });
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

void ProcessorSelector::setCatalog(QList<ProcessorCatalog::Type> types,
                                   const QString&                selectedId,
                                   QStringList                   excludedFavorites)
{
    // Store metadata only: UI catalogs must never retain executable plugin factories.
    for (auto& type : types)
    {
        type.create = {};
    }
    _types             = std::move(types);
    _excludedFavorites = std::move(excludedFavorites);
    std::sort(_types.begin(), _types.end(), [](const auto& left, const auto& right) {
        if ((left.id == "native.bypass") != (right.id == "native.bypass"))
        {
            return left.id == "native.bypass";
        }
        const auto compared = QString::localeAwareCompare(left.title, right.title);
        return compared == 0 ? left.id < right.id : compared < 0;
    });
    _refreshSelection(selectedId);
}

void ProcessorSelector::setTopology(int channels)
{
    channels = channels == 2 ? 2 : 1;
    if (_channels == channels)
    {
        return;
    }
    _channels = channels;
    _refreshSelection(currentData().toString());
}

void ProcessorSelector::setInplaceAvailable(bool available)
{
    if (_inplaceAvailable == available)
    {
        return;
    }
    _inplaceAvailable = available;
    _refreshSelection(currentData().toString());
}

bool ProcessorSelector::_supports(const ProcessorCatalog::Type& type) const
{
    return type.inputCount == _channels && type.outputCount == _channels;
}

void ProcessorSelector::_refreshSelection(const QString& selectedId)
{
    const QSignalBlocker blocker(this);
    clear();
    if (_inplaceAvailable)
    {
        addItem(Icons::processorBlock(true, _channels == 2), tr("Inplace"), QStringLiteral("@inplace"));
    }
    for (const auto& type : _types)
    {
        if (!_supports(type))
        {
            continue;
        }
        addItem(!type.available ? Icons::icon("triangle-exclamation-point") :
                                  Icons::processorBlock(type.kind != "native", _channels == 2),
                type.title,
                type.id);
        setItemData(count() - 1, type.available ? type.id : type.unavailableReason, Qt::ToolTipRole);
    }
    const auto selected = selectedId.isEmpty() ? QStringLiteral("native.bypass") : selectedId;
    auto       row      = findData(selected);
    if (row < 0)
    {
        const auto type  = std::find_if(_types.cbegin(), _types.cend(), [&](const auto& value) {
            return value.id == selected;
        });
        const auto title = type == _types.cend() ? selected : type->title;
        addItem(Icons::icon("triangle-exclamation-point"), tr("Unavailable: %1").arg(title), selected);
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
    _filter();
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
    if (watched == _tree->viewport() && event->type() == QEvent::ContextMenu)
    {
        auto* context = static_cast<QContextMenuEvent*>(event);
        auto* item    = _tree->itemAt(context->pos());
        if (item && item->data(0, Qt::UserRole).toString() == "@inplace")
        {
            return true;
        }
    }
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
        if (!_supports(type))
        {
            continue;
        }
        const auto origin = type.origin == "workspace" ? QStringLiteral("Workspace") : QStringLiteral("Native");
        const auto group  = type.group.isEmpty() ? tr("General") : type.group;
        const auto key    = origin + QChar::Null + group;
        auto*      parent = groups.value(key);
        if (!parent)
        {
            parent = new QTreeWidgetItem(roots.value(origin), {group});
            parent->setFlags(Qt::ItemIsEnabled);
            parent->setExpanded(false);
            groups.insert(key, parent);
        }
        auto* item = new QTreeWidgetItem(parent, {type.title});
        item->setData(0, Qt::UserRole, type.id);
        item->setData(0, SearchRole, origin + ' ' + group + ' ' + type.title + ' ' + type.id);
        item->setIcon(0,
                      !type.available ? Icons::icon("triangle-exclamation-point") :
                                        Icons::processorBlock(type.kind != "native", _channels == 2));
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
    if (!selected.isEmpty() && selected != "native.bypass" && selected != "@inplace" && findData(selected) >= 0
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
    auto excluded = _excludedFavorites;
    excluded.append("@inplace");
    for (const auto& type : _types)
    {
        if (!_supports(type))
        {
            excluded.append(type.id);
        }
    }
    _tree->prependFavorites({}, excluded);
    if (_inplaceAvailable)
    {
        auto* item = new QTreeWidgetItem(QStringList {tr("Inplace")});
        item->setData(0, Qt::UserRole, QStringLiteral("@inplace"));
        item->setIcon(0, Icons::processorBlock(true, _channels == 2));
        _tree->insertTopLevelItem(0, item);
        if (selected == "@inplace")
        {
            _tree->setCurrentItem(item);
        }
        bool separatorPresent = false;
        for (int row = 1; row < _tree->topLevelItemCount(); ++row)
        {
            if (_tree->topLevelItem(row)->data(0, ProcessorCatalogTree::SeparatorRole).toBool())
            {
                separatorPresent = true;
                break;
            }
        }
        if (!separatorPresent)
        {
            auto* separator = new QTreeWidgetItem;
            separator->setFlags(Qt::NoItemFlags);
            separator->setData(0, ProcessorCatalogTree::SeparatorRole, true);
            _tree->insertTopLevelItem(1, separator);
        }
    }
    _filtering = false;
    _filter();
}

void ProcessorSelector::_filter()
{
    const auto       query      = _search->text().trimmed();
    const bool       filtering  = !query.isEmpty();
    const auto       selectedId = currentData().toString();
    QTreeWidgetItem* first      = nullptr;
    QTreeWidgetItem* selected   = nullptr;
    bool             shortcuts  = false;
    // Leaf visibility uses the same origin/group/title/ID metadata as catalog creation.
    for (QTreeWidgetItemIterator it(_tree); *it; ++it)
    {
        auto* item = *it;
        if (!item->data(0, Qt::UserRole).isValid())
        {
            continue;
        }
        const auto id     = item->data(0, Qt::UserRole).toString();
        const auto search = item->data(0, SearchRole).isValid() ? item->data(0, SearchRole).toString() :
                            item->data(0, ProcessorCatalogTree::FavoriteRole).toBool() ? id :
                                                                                         item->text(0) + ' ' + id;
        item->setHidden(!search.contains(query, Qt::CaseInsensitive));
        if (!item->isHidden())
        {
            if (!first)
            {
                first = item;
            }
            if (!selected && id == selectedId)
            {
                selected = item;
            }
            shortcuts |= !item->parent()
                      && (item->data(0, ProcessorCatalogTree::FavoriteRole).toBool() || id == "@inplace");
        }
    }
    for (int row = 0; row < _tree->topLevelItemCount(); ++row)
    {
        auto* root = _tree->topLevelItem(row);
        if (root->data(0, ProcessorCatalogTree::SeparatorRole).toBool())
        {
            root->setHidden(!shortcuts);
            continue;
        }
        if (root->data(0, Qt::UserRole).isValid())
        {
            continue;
        }
        bool visible = false;
        for (int groupIndex = 0; groupIndex < root->childCount(); ++groupIndex)
        {
            auto* group    = root->child(groupIndex);
            bool  matching = false;
            for (int child = 0; child < group->childCount(); ++child)
            {
                matching |= !group->child(child)->isHidden();
            }
            if (filtering && !_filtering)
            {
                group->setData(0, ExpandedRole, group->isExpanded());
            }
            if (filtering)
            {
                group->setExpanded(matching);
            }
            else if (_filtering)
            {
                group->setExpanded(group->data(0, ExpandedRole).toBool());
            }
            group->setHidden(!matching);
            visible |= matching;
        }
        if (filtering && !_filtering)
        {
            root->setData(0, ExpandedRole, root->isExpanded());
        }
        if (filtering)
        {
            root->setExpanded(visible);
        }
        else if (_filtering)
        {
            root->setExpanded(root->data(0, ExpandedRole).toBool());
        }
        root->setHidden(!visible);
    }
    _filtering = filtering;
    if (!filtering && selected)
    {
        _tree->setCurrentItem(selected);
    }
    else if (!_tree->currentItem() || _tree->currentItem()->isHidden())
    {
        _tree->setCurrentItem(first);
    }
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
