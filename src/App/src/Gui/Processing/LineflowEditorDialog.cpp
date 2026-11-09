#include "LineflowEditorDialog.h"

#include <QAction>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDrag>
#include <QEvent>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Icons.h>
#include <Termaster/Core/Gui/SelectionIconDelegate.h>
#include <Termaster/Core/Gui/Settings/SettingsFormWidget.h>
#include <algorithm>
#include <exception>

namespace
{
class CatalogList final : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    void startDrag(Qt::DropActions) override
    {
        if (!currentItem() || currentItem()->data(0, Qt::UserRole).toString().isEmpty())
        {
            return;
        }
        auto* mime = new QMimeData;
        mime->setData("application/x-termaster-processor", currentItem()->data(0, Qt::UserRole).toString().toUtf8());
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};
QAction* tool(QToolBar* bar, const QString& icon, const QString& text, const QString& name)
{
    auto* action = bar->addAction(Icons::icon(icon), text);
    action->setToolTip(text);
    action->setObjectName(name);
    return action;
}
} // namespace
LineflowEditorDialog::LineflowEditorDialog(QVariantMap             definitions,
                                           ProcessorRegistry&      registry,
                                           SettingsWidgetRegistry& widgets,
                                           Apply                   apply,
                                           QWidget*                parent) :
    QDialog(parent), _registry(registry), _widgets(widgets), _apply(std::move(apply))
{
    setObjectName("lineflowEditor");
    setWindowTitle(tr("Workspace Lineflow editor"));
    resize(1100, 760);
    _model        = new LineflowEditorModel(std::move(definitions), registry, this);
    auto* layout  = new QVBoxLayout(this);
    auto* heading = new QLabel(tr("Lineflow blocks"), this);
    auto  font    = heading->font();
    font.setPointSizeF(font.pointSizeF() + 3);
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);
    auto* blockBar = new QToolBar(this);
    blockBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    _blocks = new QComboBox(this);
    _blocks->setObjectName("blockSelector");
    _blocks->setMinimumContentsLength(28);
    blockBar->addWidget(_blocks);
    blockBar->addWidget(new QLabel(tr("Group:"), this));
    _groups = new QComboBox(this);
    _groups->setObjectName("blockGroup");
    _groups->setEditable(true);
    _groups->setMinimumContentsLength(14);
    _groups->lineEdit()->setMaxLength(128);
    _groups->lineEdit()->setPlaceholderText(tr("General"));
    blockBar->addWidget(_groups);
    connect(_groups->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        if (_updating)
        {
            return;
        }
        QString error;
        _model->setBlockGroup(_model->selectedBlock(), _groups->currentText(), &error);
        _message(error);
    });
    auto* addBlock    = tool(blockBar, "plus", tr("Create block"), "addBlock");
    auto* duplicate   = tool(blockBar, "copy", tr("Duplicate block"), "duplicateBlock");
    auto* rename      = tool(blockBar, "sliders-horizontal", tr("Rename block"), "renameBlock");
    auto* removeBlock = tool(blockBar, "trash", tr("Remove block definition; keep references"), "removeBlock");
    layout->addWidget(blockBar);
    auto* splitter = new QSplitter(this);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);
    auto* left       = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto* operations = new QToolBar(left);
    operations->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto* add    = tool(operations, "arrow-right", tr("Insert selected processor"), "addOperation");
    auto* remove = tool(operations, "trash", tr("Remove selected operation"), "removeOperation");
    auto* up     = tool(operations, "chevron-up", tr("Move operation up"), "moveUp");
    auto* down   = tool(operations, "chevron-down", tr("Move operation down"), "moveDown");
    leftLayout->addWidget(operations);
    _tree = new QTreeView(left);
    _tree->setObjectName("nodeTree");
    _tree->setModel(_model);
    _tree->setItemDelegate(new SelectionIconDelegate(_tree));
    _tree->setAlternatingRowColors(true);
    _tree->setDragEnabled(true);
    _tree->setAcceptDrops(true);
    _tree->setDropIndicatorShown(true);
    _tree->setDragDropMode(QAbstractItemView::DragDrop);
    _tree->setDefaultDropAction(Qt::MoveAction);
    _tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    _tree->setSelectionMode(QAbstractItemView::SingleSelection);
    _tree->setTextElideMode(Qt::ElideMiddle);
    _tree->viewport()->installEventFilter(this);
    connect(_model, &QAbstractItemModel::columnsAboutToBeInserted, this, [this] { _restoringColumns = true; });
    connect(_model, &QAbstractItemModel::columnsInserted, this, [this] {
        _restoringColumns = false;
        _sizeColumns();
    });
    connect(_tree->header(), &QHeaderView::sectionResized, this, [this](int section, int, int size) {
        if (!_restoringColumns && !_resetting && section == 1 && size > 0)
        {
            _processorColumnWidth = size;
            _sizeColumns();
        }
    });
    _sizeColumns();
    leftLayout->addWidget(_tree, 2);
    leftLayout->addWidget(new QLabel(tr("Processor catalog"), left));
    _search = new QLineEdit(left);
    _search->setObjectName("catalogSearch");
    _search->setPlaceholderText(tr("Find a processor or reusable block"));
    leftLayout->addWidget(_search);
    _catalog = new CatalogList(left);
    _catalog->setObjectName("processorCatalog");
    _catalog->setItemDelegate(new SelectionIconDelegate(_catalog));
    _catalog->setDragEnabled(true);
    _catalog->setHeaderHidden(true);
    _catalog->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(_catalog, 1);
    auto* right       = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(12, 0, 0, 0);
    rightLayout->addWidget(new QLabel(tr("Operation properties"), right));
    auto* nameForm = new QFormLayout;
    nameForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    _name = new QLineEdit(right);
    _name->setObjectName("nodeName");
    nameForm->addRow(tr("Name:"), _name);
    rightLayout->addLayout(nameForm);
    _properties = new QWidget(right);
    _properties->setObjectName("nodeProperties");
    _propertyLayout = new QVBoxLayout(_properties);
    _propertyLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(_properties, 1);
    splitter->setSizes({440, 620});
    _error = new QLabel(this);
    _error->setObjectName("editorError");
    _error->setWordWrap(true);
    _error->setStyleSheet("color: #b54a35;");
    layout->addWidget(_error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Apply)->setObjectName("applyButton");
    buttons->button(QDialogButtonBox::Cancel)->setToolTip(tr("Discard unapplied changes and close."));
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        QString error;
        applyDraft(&error);
        _message(error);
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_blocks, &QComboBox::currentIndexChanged, this, [this] {
        if (_updating)
        {
            return;
        }
        _select();
        _model->setSelectedBlock(_blocks->currentData().toString());
        _tree->expandAll();
        _showProperties({});
    });
    connect(_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
        if (!_updating && !_resetting)
        {
            _select();
            _showProperties(index);
        }
    });
    connect(_model, &QAbstractItemModel::modelAboutToBeReset, this, [this] { _resetting = true; });
    connect(_model, &QAbstractItemModel::modelReset, this, [this] {
        _resetting = false;
        _showProperties({});
    });
    connect(_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        const auto target = index.data(LineflowEditorModel::CatalogIdRole).toString();
        if (_model->blockIds().contains(target))
        {
            _blocks->setCurrentIndex(_blocks->findData(target));
        }
    });
    connect(_catalog, &QTreeWidget::currentItemChanged, this, [this] { _refreshActions(); });
    connect(_catalog, &QTreeWidget::itemDoubleClicked, this, [this] { _addOperation(); });
    connect(_search, &QLineEdit::textChanged, this, [this] { _refreshCatalog(); });
    connect(add, &QAction::triggered, this, &LineflowEditorDialog::_addOperation);
    connect(_name, &QLineEdit::editingFinished, this, &LineflowEditorDialog::_renameOperation);
    connect(addBlock, &QAction::triggered, this, [this] {
        bool       ok   = false;
        const auto name = QInputDialog::getText(this,
                                                tr("Create block"),
                                                tr("Name:"),
                                                QLineEdit::Normal,
                                                tr("New block"),
                                                &ok);
        if (!ok)
        {
            return;
        }
        QString error;
        _model->createBlock(name, &error);
        _message(error);
    });
    connect(duplicate, &QAction::triggered, this, [this] {
        const auto id = _blocks->currentData().toString();
        QString    error;
        _model->duplicateBlock(id, tr("%1 copy").arg(_model->blockName(id)), &error);
        _message(error);
    });
    connect(rename, &QAction::triggered, this, [this] {
        const auto id   = _blocks->currentData().toString();
        bool       ok   = false;
        const auto name = QInputDialog::getText(this,
                                                tr("Rename block"),
                                                tr("Name:"),
                                                QLineEdit::Normal,
                                                _model->blockName(id),
                                                &ok);
        if (ok)
        {
            QString error;
            _model->renameBlock(id, name, &error);
            _message(error);
        }
    });
    connect(removeBlock, &QAction::triggered, this, [this] {
        QString error;
        _model->removeBlock(_blocks->currentData().toString(), &error);
        _message(error);
    });
    connect(remove, &QAction::triggered, this, [this] {
        const auto index = _tree->currentIndex();
        if (!index.isValid() || index.data(LineflowEditorModel::LinkedRole).toBool())
        {
            return;
        }
        const auto key = index.data(LineflowEditorModel::BlockRole).toString() + '\n'
                       + index.data(LineflowEditorModel::NodeRole).toString();
        _drafts.erase(key);
        QString error;
        _model->removeNode(index.data(LineflowEditorModel::BlockRole).toString(),
                           index.data(LineflowEditorModel::NodeRole).toString(),
                           &error);
        _message(error);
    });
    const auto move = [this](int offset) {
        const auto index = _tree->currentIndex();
        if (!index.isValid() || index.data(LineflowEditorModel::LinkedRole).toBool())
        {
            return;
        }
        QString   error;
        const int row = index.row() + offset;
        if (_model->moveNode(_model->selectedBlock(), index.row(), row, &error))
        {
            _tree->setCurrentIndex(_model->index(row, 0));
        }
        _message(error);
    };
    connect(up, &QAction::triggered, this, [move] { move(-1); });
    connect(down, &QAction::triggered, this, [move] { move(1); });
    connect(_model, &LineflowEditorModel::rejected, this, &LineflowEditorDialog::_message);
    connect(_model, &LineflowEditorModel::draftChanged, this, [this] {
        _refreshBlocks();
        _refreshCatalog();
        _tree->expandAll();
    });
    connect(&_registry, &ProcessorRegistry::changed, this, [this] {
        _refreshCatalog();
        _clearProperties();
        _showProperties(_tree->currentIndex());
    });
    _refreshBlocks();
    _refreshCatalog();
    _showProperties({});
    _tree->expandAll();
}
LineflowEditorDialog::~LineflowEditorDialog()
{
    _clearProperties();
}
void LineflowEditorDialog::setVisible(bool visible)
{
    if (visible && !_geometryInitialized)
    {
        _geometryInitialized = true;
        auto* target         = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
        if (target && minimumSize() != maximumSize())
        {
            const auto available = target->availableGeometry().size() - QSize(40, 80);
            resize(size().boundedTo(available).expandedTo(minimumSizeHint()));
        }
    }

    QDialog::setVisible(visible);
}

LineflowEditorModel& LineflowEditorDialog::model()
{
    return *_model;
}
void LineflowEditorDialog::_message(const QString& message)
{
    _error->setText(message);
}
bool LineflowEditorDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _tree->viewport() && event->type() == QEvent::Resize)
    {
        QTimer::singleShot(0, this, [this] { _sizeColumns(); });
    }
    return QDialog::eventFilter(watched, event);
}

void LineflowEditorDialog::_sizeColumns()
{
    if (_model->columnCount() < 2 || _restoringColumns)
    {
        return;
    }
    _restoringColumns = true;
    auto* header      = _tree->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::Interactive);
    const auto maximum = qMax(90, _tree->viewport()->width() * 2 / 5);
    header->resizeSection(1, std::clamp(_processorColumnWidth, 90, maximum));
    _restoringColumns = false;
}

void LineflowEditorDialog::_refreshBlocks()
{
    const QSignalBlocker blocker(_blocks);
    _blocks->clear();
    const QSignalBlocker groupBlocker(_groups);
    _groups->clear();
    QSet<QString> groups;
    for (const auto& definition : _model->definitions())
    {
        const auto value = definition.toMap().value("group").toString();
        if (!value.isEmpty())
        {
            groups.insert(value);
        }
    }
    auto sortedGroups = groups.values();
    sortedGroups.sort(Qt::CaseInsensitive);
    _groups->addItems(sortedGroups);
    _groups->setEditText(_model->blockGroup(_model->selectedBlock()));
    _groups->setEnabled(!_model->selectedBlock().isEmpty());
    for (const auto& id : _model->blockIds())
    {
        _blocks->addItem(_model->blockName(id), id);
    }
    _blocks->setCurrentIndex(_blocks->findData(_model->selectedBlock()));
    _refreshActions();
}
void LineflowEditorDialog::_refreshCatalog()
{
    const auto    current = _catalog->currentItem() ? _catalog->currentItem()->data(0, Qt::UserRole).toString() :
                                                      QString {};
    QSet<QString> collapsed;
    QSet<QString> seen;
    for (QTreeWidgetItemIterator it(_catalog); *it; ++it)
    {
        if ((*it)->data(0, Qt::UserRole).toString().isEmpty())
        {
            const auto key = (*it)->data(0, Qt::UserRole + 1).toString();
            seen.insert(key);
            if (!(*it)->isExpanded())
            {
                collapsed.insert(key);
            }
        }
    }
    _catalog->clear();
    const auto             definitions = _model->definitions();
    QMap<QString, QString> entries;
    for (const auto& type : _registry.types())
    {
        if (type.origin != "workspace" || definitions.contains(type.id))
        {
            entries.insert(type.id, type.title);
        }
    }
    for (auto it = definitions.cbegin(); it != definitions.cend(); ++it)
    {
        entries.insert(it.key(), it.value().toMap().value("name").toString());
    }
    QMap<QString, QTreeWidgetItem*> roots;
    QMap<QString, QTreeWidgetItem*> subgroups;
    for (const auto& origin : {QStringLiteral("Native"), QStringLiteral("Workspace")})
    {
        auto* root = new QTreeWidgetItem(_catalog, {origin});
        root->setFlags(Qt::ItemIsEnabled);
        root->setData(0, Qt::UserRole + 1, origin);
        auto font = root->font(0);
        font.setBold(true);
        root->setFont(0, font);
        root->setExpanded(!collapsed.contains(origin) || !_search->text().isEmpty());
        roots.insert(origin, root);
    }
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
    {
        const auto type   = _registry.type(it.key());
        const bool local  = definitions.contains(it.key()) || (type && type->origin == "workspace");
        const auto origin = local ? QStringLiteral("Workspace") : QStringLiteral("Native");
        const auto group  = local ? definitions.value(it.key()).toMap().value("group").toString() :
                                    (type ? type->group : QString {});
        const auto label  = group.isEmpty() ? tr("General") : group;
        if (!(origin + ' ' + label + ' ' + it.value() + ' ' + it.key()).contains(_search->text(), Qt::CaseInsensitive))
        {
            continue;
        }
        const auto key    = origin + QChar::Null + label;
        auto*      parent = subgroups.value(key);
        if (!parent)
        {
            parent = new QTreeWidgetItem(roots.value(origin), {label});
            parent->setFlags(Qt::ItemIsEnabled);
            parent->setData(0, Qt::UserRole + 1, key);
            parent->setExpanded((seen.contains(key) && !collapsed.contains(key)) || !_search->text().isEmpty());
            subgroups.insert(key, parent);
        }
        auto* item = new QTreeWidgetItem(parent, {it.value()});
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        item->setData(0, Qt::UserRole, it.key());
        const auto reason = _model->unavailableReason(it.key());
        item->setIcon(0, Icons::icon(!reason.isEmpty() ? "triangle-exclamation-point" : local ? "blocks" : "workflow"));
        item->setToolTip(0, reason.isEmpty() ? it.key() : reason);
        if (it.key() == current)
        {
            _catalog->setCurrentItem(item);
        }
    }
    for (auto* root : roots)
    {
        root->setHidden(!_search->text().isEmpty() && root->childCount() == 0);
        root->sortChildren(0, Qt::AscendingOrder);
        for (int index = 0; index < root->childCount(); ++index)
        {
            root->child(index)->sortChildren(0, Qt::AscendingOrder);
        }
    }
}
void LineflowEditorDialog::_clearProperties()
{
    _formWidget = nullptr;
    while (auto* item = _propertyLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
}
void LineflowEditorDialog::_select()
{
    if (_active.isEmpty())
    {
        return;
    }
    const auto it = _drafts.find(_active);
    if (it != _drafts.end())
    {
        QString error;
        _flush(*it->second, &error);
        if (!error.isEmpty())
        {
            _message(error);
        }
    }
}
bool LineflowEditorDialog::_flush(Draft& draft, QString* error)
{
    if (!_model->node(draft.block, draft.node).contains("name"))
    {
        return true;
    }
    if (!draft.form || !draft.form->apply(error))
    {
        return false;
    }
    const bool previous = _updating;
    _updating           = true;
    const bool result   = _model->setNodeConfiguration(draft.block,
                                                       draft.node,
                                                       draft.settings->value("configuration").toMap(),
                                                       error);
    _updating           = previous;
    return result;
}
void LineflowEditorDialog::_showProperties(const QModelIndex& index)
{
    _refreshActions();
    _clearProperties();
    _active.clear();
    _name->setEnabled(false);
    _name->clear();
    if (!index.isValid())
    {
        auto* label = new QLabel(
            tr("Select an operation to edit its settings. Drag catalog entries into the sequence to add operations."),
            _properties);
        label->setWordWrap(true);
        _propertyLayout->addWidget(label);
        _propertyLayout->addStretch();
        return;
    }
    const auto block  = index.data(LineflowEditorModel::BlockRole).toString();
    const auto name   = index.data(LineflowEditorModel::NodeRole).toString();
    const auto node   = _model->node(block, name);
    const auto target = index.data(LineflowEditorModel::CatalogIdRole).toString();
    _name->setText(name);
    const bool linked = index.data(LineflowEditorModel::LinkedRole).toBool();
    _name->setEnabled(!linked);
    auto* info    = new QLabel(target, _properties);
    auto  subdued = info->palette();
    subdued.setColor(QPalette::WindowText, subdued.color(QPalette::PlaceholderText));
    info->setPalette(subdued);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info->setWordWrap(true);
    _propertyLayout->addWidget(info);
    if (linked || node.contains("reference") || _model->blockIds().contains(target))
    {
        auto* label = new QLabel(
            tr("This is a linked view of a reusable block. Open its definition to edit shared operations."),
            _properties);
        label->setWordWrap(true);
        _propertyLayout->addWidget(label);
        const auto definition = linked ? block : target;
        if (_model->blockIds().contains(definition))
        {
            auto* open = new QPushButton(tr("Open block definition"), _properties);
            connect(open, &QPushButton::clicked, this, [this, definition] {
                _blocks->setCurrentIndex(_blocks->findData(definition));
            });
            _propertyLayout->addWidget(open);
        }
        _propertyLayout->addStretch();
        return;
    }
    const auto metadata = _registry.type(target);
    if (!metadata || !metadata->available)
    {
        auto* label = new QLabel(metadata ? metadata->unavailableReason :
                                            tr("Processor is unavailable. Its saved configuration is preserved."),
                                 _properties);
        label->setWordWrap(true);
        _propertyLayout->addWidget(label);
        _propertyLayout->addStretch();
        return;
    }
    const auto key = block + '\n' + name;
    auto       it  = _drafts.find(key);
    if (it == _drafts.end())
    {
        try
        {
            auto draft      = std::make_unique<Draft>();
            draft->block    = block;
            draft->node     = name;
            draft->schema   = std::make_shared<SettingsSchema>(metadata->settingsSchema);
            draft->defaults = std::make_unique<Settings>(QString {});
            draft->defaults->setValue("configuration", metadata->defaults);
            draft->settings = std::make_unique<Settings>(QString {});
            draft->settings->setValue("configuration", node.value("configuration").toMap());
            const SettingsOverlay overlay(draft->defaults->node("configuration"),
                                          draft->settings->node("configuration"));
            draft->form = new SettingsFormModel(draft->schema, overlay, this);
            it          = _drafts.emplace(key, std::move(draft)).first;
        }
        catch (const std::exception& exception)
        {
            _message(QString::fromUtf8(exception.what()));
            return;
        }
    }
    QString refreshError;
    if (!_refreshDraft(*it->second, *metadata, &refreshError))
    {
        _message(refreshError);
        auto* label = new QLabel(tr("The updated settings descriptor is unavailable. The existing draft is preserved."),
                                 _properties);
        label->setWordWrap(true);
        _propertyLayout->addWidget(label);
        return;
    }
    _active = key;
    try
    {
        _formWidget = new SettingsFormWidget(*it->second->form, _widgets, _properties);
        _formWidget->setObjectName("nodeSettingsForm");
        _propertyLayout->addWidget(_formWidget);
    }
    catch (const std::exception& exception)
    {
        _message(QString::fromUtf8(exception.what()));
        auto* label = new QLabel(tr("The settings editor is unavailable. Its configuration draft is preserved."),
                                 _properties);
        label->setWordWrap(true);
        _propertyLayout->addWidget(label);
        _propertyLayout->addStretch();
    }
    catch (...)
    {
        _message(tr("The settings editor factory failed."));
    }
}
void LineflowEditorDialog::_addOperation()
{
    if (!_catalog->currentItem() || _catalog->currentItem()->data(0, Qt::UserRole).toString().isEmpty())
    {
        return;
    }
    _select();
    const auto selected = _tree->currentIndex();
    const int  row      = selected.isValid() && !selected.parent().isValid() ? selected.row() + 1 : _model->rowCount();
    QString    error;
    const auto name = _model->insertNode(_model->selectedBlock(),
                                         row,
                                         _catalog->currentItem()->data(0, Qt::UserRole).toString(),
                                         &error);
    _message(error);
    if (!name.isEmpty())
    {
        _tree->setCurrentIndex(_model->index(row, 0));
    }
}
void LineflowEditorDialog::_renameOperation()
{
    const auto index = _tree->currentIndex();
    if (_updating || !index.isValid() || index.data(LineflowEditorModel::LinkedRole).toBool())
    {
        return;
    }
    const auto block = index.data(LineflowEditorModel::BlockRole).toString();
    const auto old   = index.data(LineflowEditorModel::NodeRole).toString();
    const auto name  = _name->text();
    if (old == name)
    {
        return;
    }
    _select();
    const auto key = block + '\n' + old;
    QString    error;
    const int  row = index.row();
    if (_model->renameNode(block, old, name, &error))
    {
        auto item = _drafts.extract(key);
        if (!item.empty())
        {
            item.key()          = block + '\n' + name;
            item.mapped()->node = name;
            _drafts.insert(std::move(item));
        }
        _active = block + '\n' + name;
        _tree->setCurrentIndex(_model->index(row, 0));
    }
    _message(error);
}
bool LineflowEditorDialog::applyDraft(QString* error)
{
    const auto selected      = _tree->currentIndex();
    const auto selectedBlock = selected.data(LineflowEditorModel::BlockRole).toString();
    const auto selectedNode  = selected.data(LineflowEditorModel::NodeRole).toString();
    if (_groups->isEnabled() && !_model->setBlockGroup(_model->selectedBlock(), _groups->currentText(), error))
    {
        if (error)
        {
            _message(*error);
        }
        return false;
    }
    for (auto& [key, draft] : _drafts)
    {
        if (!_flush(*draft, error))
        {
            if (error)
            {
                _message(*error);
            }
            return false;
        }
    }
    if (!_model->validate(error))
    {
        if (error)
        {
            _message(*error);
        }
        return false;
    }
    if (!_apply)
    {
        if (error)
        {
            *error = tr("Workspace persistence is unavailable.");
        }
        return false;
    }
    const auto result = _apply(_model->baseline(), _model->definitions(), _model->renames(), error);
    if (!result.success)
    {
        if (error)
        {
            _message(*error);
        }
        return false;
    }
    if (result.definitions)
    {
        _updating = true;
        _active.clear();
        _clearProperties();
        _drafts.clear();
        _model->markApplied(*result.definitions);
        _updating = false;
        _tree->expandAll();
        std::function<QModelIndex(const QModelIndex&)> find = [&](const QModelIndex& parent) -> QModelIndex {
            for (int row = 0; row < _model->rowCount(parent); ++row)
            {
                const auto index = _model->index(row, 0, parent);
                if (index.data(LineflowEditorModel::BlockRole).toString() == selectedBlock
                    && index.data(LineflowEditorModel::NodeRole).toString() == selectedNode)
                {
                    return index;
                }
                const auto child = find(index);
                if (child.isValid())
                {
                    return child;
                }
            }
            return {};
        };
        _tree->setCurrentIndex(find({}));
    }
    else
    {
        _model->markApplied();
    }
    _message(tr("Changes applied."));
    return true;
}

void LineflowEditorDialog::_refreshActions()
{
    const auto index    = _tree->currentIndex();
    const bool editable = index.isValid() && !index.data(LineflowEditorModel::LinkedRole).toBool();
    const bool block    = !_model->selectedBlock().isEmpty();
    const auto enable   = [this](const char* name, bool enabled) {
        if (auto* action = findChild<QAction*>(name))
        {
            action->setEnabled(enabled);
        }
    };
    enable("duplicateBlock", block);
    enable("renameBlock", block);
    enable("removeBlock", block);
    enable("addOperation",
           block && _catalog->currentItem() && !_catalog->currentItem()->data(0, Qt::UserRole).toString().isEmpty());
    enable("removeOperation", editable);
    enable("moveUp", editable && index.row() > 0);
    enable("moveDown", editable && index.row() + 1 < _model->rowCount());
}

void LineflowEditorDialog::releaseEditors()
{
    _select();
    _clearProperties();
    _active.clear();
    _name->setEnabled(false);
    auto* label = new QLabel(tr("Settings editors were released. Select an operation to reopen its available editor."),
                             _properties);
    label->setWordWrap(true);
    _propertyLayout->addWidget(label);
    _propertyLayout->addStretch();
}

bool LineflowEditorDialog::_refreshDraft(Draft& draft, const ProcessorCatalog::Type& metadata, QString* error)
{
    const auto fingerprint = QCryptographicHash::hash(metadata.settingsSchema + '\0'
                                                          + QJsonDocument::fromVariant(metadata.defaults)
                                                                .toJson(QJsonDocument::Compact),
                                                      QCryptographicHash::Sha256);
    if (draft.fingerprint == fingerprint)
    {
        return true;
    }
    try
    {
        auto schema   = std::make_shared<SettingsSchema>(metadata.settingsSchema);
        auto settings = std::make_unique<Settings>(QString {});
        if (!settings->setValue("configuration", draft.settings->value("configuration"), error))
        {
            return false;
        }
        QVariantMap values;
        QStringList removed;
        for (const auto& field : draft.schema->fields())
        {
            if (draft.form->hasOverride(field.key))
            {
                values.insert(field.settingsPath, draft.form->value(field.key));
            }
            else
            {
                removed.append(field.settingsPath);
            }
        }
        auto target = settings->node("configuration");
        if (!target.applyValues(values, removed, error))
        {
            return false;
        }
        auto defaults = std::make_unique<Settings>(QString {});
        if (!defaults->setValue("configuration", metadata.defaults, error))
        {
            return false;
        }
        const SettingsOverlay overlay(defaults->node("configuration"), target);
        auto*                 form = new SettingsFormModel(schema, overlay, this);
        delete draft.form.data();
        draft.form        = form;
        draft.settings    = std::move(settings);
        draft.defaults    = std::move(defaults);
        draft.schema      = std::move(schema);
        draft.fingerprint = fingerprint;
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error)
        {
            *error = QString::fromUtf8(exception.what());
        }
        return false;
    }
}
