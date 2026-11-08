#include "PluginManagerWindow.h"

#include "PluginTableModel.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

PluginManagerWindow::PluginManagerWindow(PluginManager& manager, QWidget* parent) :
    QDialog(parent), _manager(&manager), _model(new PluginTableModel(manager, this)),
    _filter(new QSortFilterProxyModel(this)), _table(new QTableView(this)), _search(new QLineEdit(this)),
    _enable(new QPushButton(tr("Enable"), this)), _disable(new QPushButton(tr("Disable"), this)),
    _reload(new QPushButton(tr("Restart"), this)), _refresh(new QPushButton(tr("Refresh"), this)),
    _openFolder(new QPushButton(tr("Open folder"), this)), _summary(new QLabel(this)), _empty(new QLabel(this)),
    _operationMessage(new QLabel(this)), _id(new QLineEdit(this)), _path(new QLineEdit(this)),
    _dependencies(new QPlainTextEdit(this)), _reverseDependencies(new QPlainTextEdit(this)),
    _diagnostic(new QPlainTextEdit(this))
{
    setObjectName("PluginManagerWindow");
    setWindowTitle(tr("Plugins — Termaster"));
    setMinimumSize(760, 480);
    resize(1000, 720);
    setModal(false);

    auto outerLayout = new QVBoxLayout(this);
    auto scroll      = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto content = new QWidget(scroll);
    auto layout  = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(content);
    outerLayout->addWidget(scroll, 1);
    auto folderRow = new QHBoxLayout();
    auto folder    = new QLineEdit(manager.directory(), this);
    folder->setObjectName("pluginFolder");
    folder->setReadOnly(true);
    folder->setToolTip(manager.directory());
    folder->setCursorPosition(0);
    folderRow->addWidget(new QLabel(tr("Plugin folder:"), this));
    folderRow->addWidget(folder, 1);
    folderRow->addWidget(_openFolder);
    layout->addLayout(folderRow);

    auto searchRow = new QHBoxLayout();
    _search->setObjectName("pluginFilter");
    _search->setPlaceholderText(tr("Filter by name, ID, state or dependency"));
    _search->setClearButtonEnabled(true);
    _refresh->setObjectName("refreshPlugins");
    _refresh->setToolTip(tr("Read the plugin folder again."));
    searchRow->addWidget(_search, 1);
    searchRow->addWidget(_refresh);
    layout->addLayout(searchRow);

    connect(_model, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        _rememberSelection();
        _resetting = true;
        _updateActions();
    });
    _filter->setSourceModel(_model);
    _filter->setFilterRole(PluginTableModel::FilterRole);
    _filter->setFilterKeyColumn(0);
    _filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    _filter->setSortCaseSensitivity(Qt::CaseInsensitive);
    _filter->setSortRole(PluginTableModel::SortRole);
    _table->setObjectName("pluginTable");
    _table->setModel(_filter);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setAlternatingRowColors(true);
    _table->setWordWrap(false);
    _table->setSortingEnabled(true);
    _table->sortByColumn(PluginTableModel::Name, Qt::AscendingOrder);
    _table->verticalHeader()->hide();
    _table->horizontalHeader()->setSectionResizeMode(PluginTableModel::Enabled, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(PluginTableModel::Name, QHeaderView::Stretch);
    _table->horizontalHeader()->setSectionResizeMode(PluginTableModel::Version, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(PluginTableModel::Status, QHeaderView::ResizeToContents);
    _table->setColumnWidth(PluginTableModel::Id, 220);
    layout->addWidget(_table, 1);

    _empty->setObjectName("pluginEmptyState");
    _empty->setAlignment(Qt::AlignCenter);
    _empty->setWordWrap(true);
    _empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(_empty, 1);
    _summary->setObjectName("pluginSummary");
    layout->addWidget(_summary);

    auto actions = new QHBoxLayout();
    _enable->setObjectName("enablePlugin");
    _disable->setObjectName("disablePlugin");
    _reload->setObjectName("restartPlugin");
    _enable->setToolTip(tr("Enable this plugin. It starts when all its dependencies are running."));
    _disable->setToolTip(tr("Disable this plugin and stop its dependents. Their enabled preferences are kept."));
    _reload->setToolTip(tr("Restart this plugin and its enabled dependents."));
    actions->addWidget(_enable);
    actions->addWidget(_disable);
    actions->addWidget(_reload);
    actions->addStretch();
    layout->addLayout(actions);

    auto details = new QGroupBox(tr("Selected plugin"), this);
    auto form    = new QFormLayout(details);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    _id->setObjectName("pluginId");
    _path->setObjectName("pluginPath");
    _id->setReadOnly(true);
    _path->setReadOnly(true);
    _dependencies->setObjectName("pluginDependencies");
    _dependencies->setReadOnly(true);
    _dependencies->setMinimumHeight(40);
    _dependencies->setMaximumHeight(48);
    _dependencies->setTabChangesFocus(true);
    _reverseDependencies->setObjectName("pluginReverseDependencies");
    _reverseDependencies->setReadOnly(true);
    _reverseDependencies->setMinimumHeight(40);
    _reverseDependencies->setMaximumHeight(48);
    _reverseDependencies->setTabChangesFocus(true);
    _diagnostic->setObjectName("pluginDiagnostic");
    _diagnostic->setReadOnly(true);
    _diagnostic->setTabChangesFocus(true);
    _diagnostic->setPlaceholderText(tr("No diagnostic messages"));
    _diagnostic->setMinimumHeight(56);
    _diagnostic->setMaximumHeight(88);
    form->addRow(tr("ID:"), _id);
    form->addRow(tr("Library:"), _path);
    form->addRow(tr("Dependencies:"), _dependencies);
    form->addRow(tr("Required by:"), _reverseDependencies);
    form->addRow(tr("Diagnostic:"), _diagnostic);
    layout->addWidget(details);

    _operationMessage->setObjectName("pluginOperationMessage");
    _operationMessage->setTextFormat(Qt::PlainText);
    _operationMessage->setWordWrap(true);
    _operationMessage->setMaximumHeight(80);
    _operationMessage->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _operationMessage->hide();
    outerLayout->addWidget(_operationMessage);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outerLayout->addWidget(buttons);

    connect(_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        _rememberSelection();
        _resetting = true;
        _filter->setFilterFixedString(text);
        _restoreSelection();
    });
    connect(_filter, &QAbstractItemModel::modelReset, this, &PluginManagerWindow::_restoreSelection);
    connect(_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this]() {
        _rememberSelection();
        _updateDetails();
    });
    connect(_model, &PluginTableModel::enabledChangeRequested, this, &PluginManagerWindow::enabledChangeRequested);
    connect(_enable, &QPushButton::clicked, this, [this]() { emit enabledChangeRequested(_selectedId(), true); });
    connect(_disable, &QPushButton::clicked, this, [this]() { emit enabledChangeRequested(_selectedId(), false); });
    connect(_reload, &QPushButton::clicked, this, [this]() { emit reloadRequested(_selectedId()); });
    connect(_refresh, &QPushButton::clicked, this, &PluginManagerWindow::refreshRequested);
    connect(_openFolder, &QPushButton::clicked, this, &PluginManagerWindow::openFolderRequested);
    connect(&manager, &QObject::destroyed, this, [this]() {
        showError(tr("The plugin manager is no longer available."));
        _updateDetails();
    });
    for (auto button : findChildren<QPushButton*>())
    {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    _restoreSelection();
    if (const QScreen* display = screen())
    {
        const QSize available = display->availableGeometry().adjusted(24, 32, -24, -32).size();
        setMinimumSize(QSize(760, 480).boundedTo(available));
        resize(QSize(1000, 720).boundedTo(available));
    }
}

void PluginManagerWindow::setBusy(bool busy)
{
    _busy = busy;
    _updateActions();
}

void PluginManagerWindow::showError(const QString& error)
{
    _operationMessage->setText(tr("Operation failed: %1").arg(error));
    _operationMessage->show();
}

void PluginManagerWindow::showResult(const QString& message)
{
    _operationMessage->setText(message);
    _operationMessage->setVisible(!message.isEmpty());
    _updateDetails();
}

void PluginManagerWindow::_rememberSelection()
{
    const QModelIndex current = _table->currentIndex();
    const QString     path    = current.data(PluginManager::PathRole).toString();
    if (!_resetting && !path.isEmpty())
    {
        _selectedPath = path;
    }
}

void PluginManagerWindow::_restoreSelection()
{
    _resetting = true;
    QModelIndex selected;
    for (int row = 0; row < _filter->rowCount(); ++row)
    {
        const QModelIndex candidate = _filter->index(row, 0);
        if (candidate.data(PluginManager::PathRole).toString() == _selectedPath)
        {
            selected = candidate;
            break;
        }
    }
    if (!selected.isValid() && _filter->rowCount())
    {
        selected = _filter->index(0, 0);
    }
    _table->setCurrentIndex(selected);
    _resetting = false;
    _rememberSelection();
    _updateDetails();
}

void PluginManagerWindow::_updateDetails()
{
    const QModelIndex current = _table->currentIndex();
    _id->setText(current.data(PluginManager::IdRole).toString());
    _path->setText(current.data(PluginManager::PathRole).toString());
    _path->setToolTip(_path->text());
    _path->setCursorPosition(0);
    const QStringList dependencies = current.data(PluginManager::DependenciesRole).toStringList();
    _dependencies->setPlainText(current.isValid() ? (dependencies.isEmpty() ? tr("None") : dependencies.join(", ")) :
                                                    QString());
    _diagnostic->setPlainText(current.data(PluginManager::ErrorRole).toString());
    const QStringList consumers = current.data(PluginManager::ReverseDependenciesRole).toStringList();
    _reverseDependencies->setPlainText(current.isValid() ? (consumers.isEmpty() ? tr("None") : consumers.join(", ")) :
                                                           QString());

    int running = 0;
    for (int row = 0; row < _model->rowCount(); ++row)
    {
        if (_model->index(row, 0).data(PluginManager::StatusRole).toInt()
            == static_cast<int>(PluginManager::Status::Running))
        {
            ++running;
        }
    }
    _summary->setText(
        tr("%1 of %2 plugins · %3 running").arg(_filter->rowCount()).arg(_model->rowCount()).arg(running));
    const bool empty = _filter->rowCount() == 0;
    _table->setVisible(!empty);
    _empty->setVisible(empty);
    _empty->setText(!_manager          ? tr("Plugin manager unavailable") :
                    _model->rowCount() ? tr("No plugins match the filter.") :
                                         tr("No plugins found in this folder."));
    _updateActions();
}

void PluginManagerWindow::_updateActions()
{
    const QModelIndex current   = _table->currentIndex();
    const bool        available = _manager && !_busy && !_resetting;
    const bool        selected  = available && _manager->isActive() && current.isValid() && !_selectedId().isEmpty();
    const bool        valid     = current.data(PluginManager::StatusRole).toInt()
                               != static_cast<int>(PluginManager::Status::Invalid);
    const bool        enabled   = current.data(PluginManager::EnabledRole).toBool();
    _enable->setEnabled(selected && valid && !enabled);
    _disable->setEnabled(selected && enabled);
    _reload->setEnabled(selected && valid && enabled);
    _refresh->setEnabled(available);
    _search->setEnabled(available);
    _table->setEnabled(available && _manager->isActive());
    _openFolder->setEnabled(available && QDir(_manager->directory()).exists());
}

QString PluginManagerWindow::_selectedId() const
{
    return _table->currentIndex().data(PluginManager::IdRole).toString();
}
