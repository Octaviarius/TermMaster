#include "SettingsDialog.h"

#include "ui_SettingsDialog.h"

#include <QStyledItemDelegate>

class CustomItemDelegate : public QStyledItemDelegate
{
public:
    CustomItemDelegate(int rowHeight) : _rowHeight(rowHeight)
    {
    }

    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        return QSize(option.rect.width(), 50);
    }

private:
    int _rowHeight;
};

//========================================================================

SettingsDialog::SettingsDialog(SettingsProxy config, QWidget* parent) :
    QDialog(parent), ui(new Ui::SettingsDialog), _config(config)
{
    ui->setupUi(this);

    connect(ui->btnOk, &QPushButton::clicked, this, &SettingsDialog::accept);
    connect(ui->btnCancel, &QPushButton::clicked, this, &SettingsDialog::reject);

    ui->listCat->setItemDelegate(new CustomItemDelegate(40));

    auto listCatChanged = [&](int row) {
        if (row < 0)
        {
            return;
        }

        auto layoutItem = ui->gridLayout->itemAtPosition(1, 1);

        auto newWidget = _tabWidgets[ui->listCat->currentItem()->text()];

        if (layoutItem != nullptr)
        {
            auto oldWidget = layoutItem->widget();
            newWidget->setParent(oldWidget->parentWidget());
            delete ui->gridLayout->replaceWidget(oldWidget, newWidget, Qt::FindChildOption::FindDirectChildrenOnly);
            oldWidget->setVisible(false);
        }
        else
        {
            ui->gridLayout->addWidget(newWidget, 1, 1);
        }

        newWidget->setVisible(true);

        _updatePathLabel();
    };
    connect(ui->listCat, &QListWidget::currentRowChanged, listCatChanged);

    auto sortClicked = [&](int state) {
        ui->listCat->setSortingEnabled(state == Qt::Checked);
        ///
    };
    connect(ui->chbSortCat, &QCheckBox::stateChanged, sortClicked);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

int SettingsDialog::addEntries(QVector<SettingsEntry> entries)
{
    for (auto& it : entries)
    {
        addEntry(it);
    }
}

bool SettingsDialog::addEntry(const SettingsEntry& entry)
{
    auto guiPath = entry.guiPath.split('/');

    if (guiPath.size() < 2)
    {
        return false;
    }

    QTabWidget* tabWidget;

    if (!_tabWidgets.contains(guiPath[0]))
    {
        QIcon icon;
        if (_categories.contains(guiPath[0]))
        {
            icon = QIcon(_categories[guiPath[0]].iconPath);
        }

        auto listItem = new QListWidgetItem(icon, guiPath[0]);
        ui->listCat->addItem(listItem);
        listItem->setSizeHint(QSize(listItem->sizeHint().width(), 100));

        tabWidget               = new QTabWidget();
        _tabWidgets[guiPath[0]] = tabWidget;

        connect(tabWidget, &QTabWidget::currentChanged, this, &SettingsDialog::_updatePathLabel);
    }
    else
    {
        tabWidget = _tabWidgets[guiPath[0]];
    }

    auto configWidget = entry.constructWidget(_config.node(entry.configPath), nullptr);

    tabWidget->addTab(configWidget, guiPath[1]);

    return true;
}

int SettingsDialog::addCategories(QVector<SettingsCategory> cats)
{
    int count = 0;

    for (auto& it : cats)
    {
        count += addCategory(it);
    }

    return count;
}

bool SettingsDialog::addCategory(const SettingsCategory& cat)
{
    if (!_categories.contains(cat.name))
    {
        _categories[cat.name] = cat;
        return true;
    }

    return false;
}

void SettingsDialog::_updatePathLabel()
{
    auto item = ui->listCat->currentItem();

    if (item)
    {
        auto catName    = item->text();
        auto activeTab  = _tabWidgets[catName];
        auto subCatName = activeTab->tabText(activeTab->currentIndex());
        ui->lblPath->setText(QString("%1 / %2").arg(catName).arg(subCatName));
    }
}
