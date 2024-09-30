#include "ConfigSerialTerminalWidget.h"

#include "core/EnumReflection.h"
#include "core/utils.h"
#include "managers/TerminalManager.h"
#include "terminal/SerialTerminal.h"
#include "ui_ConfigSerialTerminalWidget.h"

#include <QButtonGroup>
#include <QIntValidator>
#include <QSerialPort>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>

class TrueSortProxyModel : public QSortFilterProxyModel
{
public:
    TrueSortProxyModel(QObject* parent) : QSortFilterProxyModel(parent)
    {
    }

    bool lessThan(const QModelIndex& left, const QModelIndex& right) const
    {
        auto role = sortRole();

        QVariant leftData  = sourceModel()->data(left, role);
        QVariant rightData = sourceModel()->data(right, role);

        QString leftString  = leftData.toString();
        QString rightString = rightData.toString();

        return leftString.length() < rightString.length()  ? true :
               leftString.length() == rightString.length() ? leftString < rightString :
                                                             false;
    }
};

ConfigSerialTerminalWidget::ConfigSerialTerminalWidget(QWidget* parent) :
    ui(new Ui::ConfigSerialTerminalWidget),
    _generalSettings(SettingsManager::instance().generalSettings("terminal/serial"))
{
    ui->setupUi(this);

    _devIcons["pci"]     = QIcon(":/icons/device-pci.svg");
    _devIcons["usb"]     = QIcon(":/icons/device-usb.svg");
    _devIcons["virtual"] = QIcon(":/icons/device-virtual.svg");

    auto sorterProxy = new TrueSortProxyModel(ui->cmbName);
    sorterProxy->setSortRole(Qt::UserRole);
    sorterProxy->setSourceModel(ui->cmbName->model());
    ui->cmbName->model()->setParent(sorterProxy);
    ui->cmbName->setModel(sorterProxy);

    // table
    ui->tableParams->setEditTriggers(QTableWidget::NoEditTriggers);
    ui->tableParams->setSelectionBehavior(QTableWidget::SelectionBehavior::SelectRows);

    // lists
    ui->listDataSize->addItems(SerialPortEnumReflection::dataBits.names());
    ui->listStopBits->addItems(SerialPortEnumReflection::stopBits.names());
    ui->listParity->addItems(SerialPortEnumReflection::parity.names());
    ui->listFlowControl->addItems(SerialPortEnumReflection::flowControl.names());

    _serialConfig = _generalSettings.value<SerialTerminalConfig>();

    ui->listDataSize->setCurrentRow(SerialPortEnumReflection::dataBits.indexOfEnum(_serialConfig.dataBits));
    ui->listStopBits->setCurrentRow(SerialPortEnumReflection::stopBits.indexOfEnum(_serialConfig.stopBits));
    ui->listParity->setCurrentRow(SerialPortEnumReflection::parity.indexOfEnum(_serialConfig.parity));
    ui->listFlowControl->setCurrentRow(SerialPortEnumReflection::flowControl.indexOfEnum(_serialConfig.flowControl));

    // baudrates
    ui->cmbBaudrate->setValidator(new QIntValidator(0, INT32_MAX));
    ui->cmbBaudrate->clear();

    for (auto speed :
         QList<uint> {110, 150, 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600})
    {
        ui->cmbBaudrate->addItem(QString::number(speed), speed);

        if (_serialConfig.baudRate == speed)
        {
            ui->cmbBaudrate->setCurrentIndex(ui->cmbBaudrate->count());
        }
    }
    ui->cmbBaudrate->setCurrentText(QString("%1").arg(_serialConfig.baudRate));

    connect(ui->cmbBaudrate, &QComboBox::currentTextChanged, this, [this](QString text) {
        _serialConfig.baudRate = text.toUInt();
    });

    connect(ui->cmbName, &QComboBox::currentIndexChanged, this, [this](int row) {
        if (row >= 0)
        {
            _serialConfig.portName = ui->cmbName->currentData().toString();
        }
    });
    connect(ui->cmbName, &QComboBox::currentIndexChanged, this, &ConfigSerialTerminalWidget::_cmbNameChanged);

    connect(ui->listDataSize, &QListWidget::currentRowChanged, this, [this](int row) {
        _serialConfig.dataBits = SerialPortEnumReflection::dataBits.enumValue(row);
    });
    connect(ui->listStopBits, &QListWidget::currentRowChanged, this, [this](int row) {
        _serialConfig.stopBits = SerialPortEnumReflection::stopBits.enumValue(row);
    });
    connect(ui->listParity, &QListWidget::currentRowChanged, this, [this](int row) {
        _serialConfig.parity = SerialPortEnumReflection::parity.enumValue(row);
    });
    connect(ui->listFlowControl, &QListWidget::currentRowChanged, this, [this](int row) {
        _serialConfig.flowControl = SerialPortEnumReflection::flowControl.enumValue(row);
    });

    connect(&TerminalManager::instance(),
            &TerminalManager::serialPortsChanged,
            this,
            &ConfigSerialTerminalWidget::updatePorts);

    updatePorts(QSet<QSerialPortInfo>(),
                qListToSet(TerminalManager::instance().availableSerialPorts()),
                QSet<QSerialPortInfo>());
}

ConfigSerialTerminalWidget::~ConfigSerialTerminalWidget()
{
    delete ui;
}

void ConfigSerialTerminalWidget::commit()
{
    _generalSettings.setValue("", _serialConfig);
    _generalSettings.sync();
}

void ConfigSerialTerminalWidget::updatePorts(QSet<QSerialPortInfo> availableSerialPorts,
                                             QSet<QSerialPortInfo> toAdd,
                                             QSet<QSerialPortInfo> toDel)
{
    auto addPortInfo = [this](const QSerialPortInfo& info) {
        QIcon icon;

        if (info.hasProductIdentifier())
        {
            icon = _devIcons["usb"];
        }
        else if (info.serialNumber().length() > 0)
        {
            icon = _devIcons["pci"];
        }
        else
        {
            icon = _devIcons["virtual"];
        }

        ui->cmbName->addItem(icon, QString("%1 -- %2").arg(info.portName()).arg(info.description()), info.portName());
    };

    for (const auto& it : toAdd)
    {
        _portInfosMap[it.portName()] = it;
        addPortInfo(it);
    }

    for (const auto& it : toDel)
    {
        _portInfosMap.remove(it.portName());

        auto idx = ui->cmbName->findData(it.portName());

        if (idx >= 0)
        {
            ui->cmbName->removeItem(idx);
        }
    }

    ui->cmbName->model()->sort(0);
}

void ConfigSerialTerminalWidget::_cmbNameChanged(int idx)
{
    if (idx < 0)
    {
        return;
    }

    const auto& info = _portInfosMap[ui->cmbName->currentData().toString()];

    auto insRow = [this](int row, QString param, QString value) {
        int rowCount = ui->tableParams->rowCount();

        if (row >= rowCount)
        {
            ui->tableParams->setRowCount(row + 1);
        }

        auto c1 = new QTableWidgetItem(param);
        auto c2 = new QTableWidgetItem(value);

        c1->setFlags(c1->flags() ^ (Qt::ItemIsEditable));
        c2->setFlags(c1->flags() ^ (Qt::ItemIsEditable));

        ui->tableParams->setItem(row, 0, c1);
        ui->tableParams->setItem(row, 1, c2);
    };

    int rows = 0;

    insRow(rows++, "Location", info.systemLocation());
    insRow(rows++, "Name", info.portName());
    insRow(rows++, "Description", info.description());
    insRow(rows++, "Manufacturer", info.manufacturer());

    if (info.serialNumber().size() > 0)
    {
        insRow(rows++, "Serial", info.serialNumber());
    }

    if (info.hasProductIdentifier())
    {
        insRow(rows++, "Vendor ID", QString("%1").arg(info.vendorIdentifier()));
    }

    if (info.hasVendorIdentifier())
    {
        insRow(rows++, "Product ID", QString("%1").arg(info.productIdentifier()));
    }

    ui->tableParams->setRowCount(rows);
}
