#include "SettingsDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>
#include <Termaster/Core/gui/settings/SettingsFormModel.h>
#include <Termaster/Core/gui/settings/SettingsFormWidget.h>
#include <Termaster/Core/gui/settings/SettingsWidgetRegistry.h>
#include <stdexcept>

SettingsDialog::SettingsDialog(SettingsWidgetRegistry&       registry,
                               std::function<bool(QString*)> persist,
                               QWidget* parent) : QDialog(parent), _registry(registry), _persist(std::move(persist))
{
    setObjectName("settingsDialog");
    setWindowTitle(tr("Settings"));
    resize(1020, 720);
    auto* layout     = new QVBoxLayout(this);
    auto* body       = new QHBoxLayout;
    auto* navigation = new QWidget(this);
    navigation->setMinimumWidth(200);
    navigation->setMaximumWidth(280);
    navigation->resize(240, 600);
    auto* navigationLayout = new QVBoxLayout(navigation);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    _search = new QLineEdit(navigation);
    _search->setObjectName("settingsSearch");
    _search->setPlaceholderText(tr("Search settings..."));
    _search->setClearButtonEnabled(true);
    navigationLayout->addWidget(_search);
    _tree = new QTreeView(navigation);
    _tree->setObjectName("settingsTree");
    _tree->setHeaderHidden(true);
    _tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _treeModel = new QStandardItemModel(this);
    _filter    = new QSortFilterProxyModel(this);
    _filter->setSourceModel(_treeModel);
    _filter->setFilterRole(SearchRole);
    _filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    _filter->setRecursiveFilteringEnabled(true);
    _filter->setAutoAcceptChildRows(true);
    _tree->setModel(_filter);
    navigationLayout->addWidget(_tree);
    body->addWidget(navigation);
    _stack = new QStackedWidget(this);
    _stack->setObjectName("settingsPages");
    _stack->setMinimumWidth(480);
    _empty = new QLabel(tr("No matching settings."), _stack);
    static_cast<QLabel*>(_empty)->setAlignment(Qt::AlignCenter);
    _stack->addWidget(_empty);
    body->addWidget(_stack, 1);
    layout->addLayout(body, 1);
    _error = new QLabel(this);
    _error->setObjectName("settingsError");
    _error->setTextFormat(Qt::PlainText);
    _error->setWordWrap(true);
    layout->addWidget(_error);
    _buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    layout->addWidget(_buttons);
    connect(_buttons, &QDialogButtonBox::accepted, this, [this] {
        if (_apply())
        {
            accept();
        }
    });
    connect(_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { _apply(); });
    connect(_search, &QLineEdit::textChanged, this, &SettingsDialog::_filterPages);
    connect(_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, &SettingsDialog::_showSelection);
    _updateButtons();
}

void SettingsDialog::addPage(const QString&                        id,
                             const QString&                        category,
                             const QString&                        title,
                             const QString&                        description,
                             std::shared_ptr<const SettingsSchema> schema,
                             Settings::Node                        target,
                             bool                                  preview)
{
    _checkPage(id, category, title, schema);
    if (!target.isValid())
    {
        throw std::invalid_argument("Settings page target is invalid");
    }
    _addPage(id,
             category,
             title,
             description,
             new SettingsFormModel(std::move(schema), std::move(target), this),
             preview);
}

void SettingsDialog::addPage(const QString&                        id,
                             const QString&                        category,
                             const QString&                        title,
                             const QString&                        description,
                             std::shared_ptr<const SettingsSchema> schema,
                             SettingsOverlay                       target,
                             bool                                  preview)
{
    _checkPage(id, category, title, schema);
    if (!target.isValid())
    {
        throw std::invalid_argument("Settings page target is invalid");
    }
    _addPage(id,
             category,
             title,
             description,
             new SettingsFormModel(std::move(schema), std::move(target), this),
             preview);
}

bool SettingsDialog::selectPage(const QString& id)
{
    for (int row = 0; row < _treeModel->rowCount(); ++row)
    {
        auto* category = _treeModel->item(row);
        for (int child = 0; child < category->rowCount(); ++child)
        {
            auto* item = category->child(child);
            if (item->data(PageIdRole).toString() == id)
            {
                const auto index = _filter->mapFromSource(item->index());
                if (!index.isValid())
                {
                    return false;
                }
                _tree->setCurrentIndex(index);
                _tree->scrollTo(index);
                return true;
            }
        }
    }
    return false;
}

QString SettingsDialog::currentPage() const
{
    const auto index = _tree->currentIndex();
    return index.isValid() ? index.data(PageIdRole).toString() : QString {};
}

SettingsFormModel* SettingsDialog::pageModel(const QString& id) const
{
    for (const auto& page : _pages)
    {
        if (page.id == id)
        {
            return page.model;
        }
    }
    return nullptr;
}

Settings& SettingsDialog::previewSettings()
{
    return _previewSettings;
}

void SettingsDialog::_checkPage(const QString&                               id,
                                const QString&                               category,
                                const QString&                               title,
                                const std::shared_ptr<const SettingsSchema>& schema) const
{
    if (id.trimmed().isEmpty() || category.trimmed().isEmpty() || title.trimmed().isEmpty() || !schema || pageModel(id))
    {
        throw std::invalid_argument("Settings pages require a unique nonempty ID, category, title and schema");
    }
}

void SettingsDialog::_addPage(const QString&     id,
                              const QString&     category,
                              const QString&     title,
                              const QString&     description,
                              SettingsFormModel* model,
                              bool               preview)
{
    auto* widget  = new QWidget(_stack);
    auto* layout  = new QVBoxLayout(widget);
    auto* heading = new QLabel(title, widget);
    heading->setTextFormat(Qt::PlainText);
    auto font = heading->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() + 2);
    heading->setFont(font);
    layout->addWidget(heading);
    auto* detail = new QLabel(description, widget);
    detail->setTextFormat(Qt::PlainText);
    detail->setWordWrap(true);
    detail->setVisible(!description.isEmpty());
    layout->addWidget(detail);
    if (preview)
    {
        auto* notice = new QLabel(tr("Preview — example values. Changes are not saved and no devices are accessed."),
                                  widget);
        notice->setTextFormat(Qt::PlainText);
        notice->setWordWrap(true);
        layout->addWidget(notice);
    }
    auto* form = new SettingsFormWidget(*model, _registry, widget);
    layout->addWidget(form, 1);
    _stack->addWidget(widget);
    _pages.append({id, category, title, description, model, form, widget, preview});

    QStandardItem* parent = nullptr;
    for (int row = 0; row < _treeModel->rowCount(); ++row)
    {
        if (_treeModel->item(row)->text() == category)
        {
            parent = _treeModel->item(row);
            break;
        }
    }
    if (!parent)
    {
        parent = new QStandardItem(category);
        parent->setData(category, SearchRole);
        _treeModel->appendRow(parent);
    }
    auto* item = new QStandardItem(title);
    item->setData(id, PageIdRole);
    QStringList search {category, title, description};
    for (const auto& field : model->schema().fields())
    {
        search << field.key << field.title << field.settingsPath << field.description << field.group;
    }
    for (const auto& group : model->schema().groups())
    {
        search << group.id << group.title;
    }
    item->setData(search.join('\n'), SearchRole);
    parent->appendRow(item);
    connect(model, &SettingsFormModel::validationChanged, this, &SettingsDialog::_updateButtons);
    connect(model, &SettingsFormModel::dirtyChanged, this, &SettingsDialog::_updateButtons);
    _filterPages();
    _updateButtons();
}

void SettingsDialog::_filterPages()
{
    const QString selected = currentPage();
    const QString query    = _search->text().trimmed();
    for (const auto& page : _pages)
    {
        const bool wholePage = page.category.contains(query, Qt::CaseInsensitive)
                            || page.title.contains(query, Qt::CaseInsensitive)
                            || page.description.contains(query, Qt::CaseInsensitive);
        page.form->setFilter(wholePage ? QString {} : query);
    }
    _filter->setFilterFixedString(query);
    _tree->expandAll();
    if (!selected.isEmpty() && selectPage(selected))
    {
        return;
    }
    for (int row = 0; row < _filter->rowCount(); ++row)
    {
        const auto category = _filter->index(row, 0);
        if (_filter->rowCount(category) > 0)
        {
            _tree->setCurrentIndex(_filter->index(0, 0, category));
            return;
        }
    }
    _tree->setCurrentIndex({});
    _stack->setCurrentWidget(_empty);
}

void SettingsDialog::_showSelection()
{
    auto index = _tree->currentIndex();
    if (index.isValid() && index.data(PageIdRole).toString().isEmpty() && _filter->rowCount(index) > 0)
    {
        _tree->setCurrentIndex(_filter->index(0, 0, index));
        return;
    }
    const QString id = currentPage();
    for (const auto& page : _pages)
    {
        if (page.id == id)
        {
            _stack->setCurrentWidget(page.widget);
            _updateButtons();
            return;
        }
    }
    _stack->setCurrentWidget(_empty);
    _updateButtons();
}

bool SettingsDialog::_apply()
{
    QList<const Page*> targets;
    // Pages are registered from base to derived scopes. Apply derived drafts first
    // so this dialog's base edits do not appear as concurrent inherited changes.
    for (auto page = _pages.crbegin(); page != _pages.crend(); ++page)
    {
        if (page->model->isDirty())
        {
            targets.append(&*page);
        }
    }
    if (targets.isEmpty() && !_pendingPersistence)
    {
        for (const auto& page : _pages)
        {
            if (page.id == currentPage())
            {
                targets.append(&page);
                break;
            }
        }
    }
    for (const auto* page : targets)
    {
        if (!page->model->isValid())
        {
            _search->clear();
            selectPage(page->id);
            const auto errors = page->model->errors();
            _error->setText(errors.isEmpty() ? tr("Correct invalid settings before applying.") :
                                               errors.first().message);
            _updateButtons();
            return false;
        }
    }

    QString error;
    for (const auto* page : targets)
    {
        if (!page->model->apply(&error))
        {
            _search->clear();
            selectPage(page->id);
            _error->setText(error);
            _updateButtons();
            return false;
        }
        _pendingPersistence = _pendingPersistence || !page->preview;
    }
    if (_pendingPersistence && _persist && !_persist(&error))
    {
        _pendingPersistence = true;
        _error->setText(error);
        _updateButtons();
        return false;
    }
    _pendingPersistence = false;
    for (const auto& page : _pages)
    {
        if (!page.model->isDirty())
        {
            page.model->reload();
        }
    }
    _error->clear();
    _updateButtons();
    emit applied();
    return true;
}

void SettingsDialog::_updateButtons()
{
    bool dirty = false;
    for (const auto& page : _pages)
    {
        dirty = dirty || page.model->isDirty();
    }
    _buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    _buttons->button(QDialogButtonBox::Apply)->setEnabled(dirty || _pendingPersistence);
}
