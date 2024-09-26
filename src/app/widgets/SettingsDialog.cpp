#include "SettingsDialog.h"

#include "ui_SettingsDialog.h"

#include <QStyledItemDelegate>
#include <QVBoxLayout>

#define ROW_HEIGHT (40)

class CustomItemDelegate : public QStyledItemDelegate
{
public:
    CustomItemDelegate(int rowHeight) : _rowHeight(rowHeight)
    {
    }

    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        return QSize(option.rect.width(), ROW_HEIGHT);
    }

private:
    int _rowHeight;
};

//========================================================================

SettingsDialog::SettingsDialog(ConfigContainer* configContainer, QWidget* parent) :
    QDialog(parent), ui(new Ui::SettingsDialog), _configContainer(configContainer)
{
    ui->setupUi(this);

    ui->listCat->setIconSize(QSize(ROW_HEIGHT - 8, ROW_HEIGHT - 8));

    connect(ui->btnOk, &QPushButton::clicked, this, &SettingsDialog::accept);
    connect(ui->btnCancel, &QPushButton::clicked, this, &SettingsDialog::reject);

    setWindowTitle("Settings - " + _configContainer->title());

    auto updatePathLabel = [this]() {
        auto item = ui->listCat->currentItem();

        if (item)
        {
            auto catName    = item->data(Qt::UserRole).toString();
            auto activeTab  = _tabWidgets[catName];
            auto subCatName = activeTab->tabText(activeTab->currentIndex());
            ui->lblPath->setText(QString("%1 / %2").arg(catName).arg(subCatName));
        }
    };

    auto containers = _configContainer->containers();

    // create main list
    for (auto root : containers)
    {
        auto listWidgetItem = new QListWidgetItem(root->icon(), root->title());
        listWidgetItem->setData(Qt::UserRole, root->name());

        ui->listCat->addItem(listWidgetItem);

        auto tabWidget            = new QTabWidget(this);
        _tabWidgets[root->name()] = tabWidget;
        tabWidget->setVisible(false);
        connect(tabWidget, &QTabWidget::currentChanged, this, updatePathLabel);

        // create tabs
        std::function<void(ConfigContainer*)> addWidgetsToTab =
            [this, &addWidgetsToTab, tabWidget](ConfigContainer* container) {
                for (auto c : container->containers())
                {
                    addWidgetsToTab(c);
                }

                auto constructors = container->widgetConstructors();

                if (constructors.count() > 0)
                {
                    auto widget   = new QWidget(tabWidget);
                    auto layout   = new QVBoxLayout(widget);
                    auto tabIndex = tabWidget->addTab(widget, container->icon(), container->title());

                    tabWidget->setTabToolTip(tabIndex, container->tooltip());

                    for (auto constructor : constructors)
                    {
                        auto configWindow = constructor(widget);

                        connect(ui->btnOk, &QPushButton::clicked, configWindow, &IConfigWidget::commit);
                        connect(ui->btnApply, &QPushButton::clicked, configWindow, &IConfigWidget::commit);

                        layout->addWidget(configWindow);
                    }
                }
            };

        addWidgetsToTab(root);
    }

    auto sortClicked = [&](int state) { ui->listCat->setSortingEnabled(state == Qt::Checked); };
    connect(ui->chbSortCat, &QCheckBox::stateChanged, sortClicked);

    // create tabs switching
    ui->listCat->setItemDelegate(new CustomItemDelegate(40));
    connect(ui->listCat,
            &QListWidget::currentItemChanged,
            this,
            [this, updatePathLabel](QListWidgetItem* current, QListWidgetItem* previous) {
                auto rowName = current->data(Qt::UserRole).toString();

                auto newWidget = _tabWidgets[rowName];

                auto layoutItem = ui->gridLayout->itemAtPosition(1, 1);

                if (layoutItem)
                {
                    auto oldWidget = layoutItem->widget();
                    newWidget->setParent(oldWidget->parentWidget());
                    delete ui->gridLayout->replaceWidget(oldWidget,
                                                         newWidget,
                                                         Qt::FindChildOption::FindDirectChildrenOnly);
                    oldWidget->setVisible(false);
                }
                else
                {
                    ui->gridLayout->addWidget(newWidget, 1, 1);
                }

                updatePathLabel();

                newWidget->setVisible(true);
            });
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}
