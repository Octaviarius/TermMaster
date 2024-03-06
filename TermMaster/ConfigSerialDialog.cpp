#include "ConfigSerialDialog.h"
#include "ui_ConfigSerialDialog.h"

#include <QButtonGroup>
#include <QSerialPort>

ConfigSerialDialog::ConfigSerialDialog(QWidget* parent, const PathedValue& defaultConfig) :
    QDialog(parent), ui(new Ui::ConfigSerialDialog)
{
    bool ok;

    ui->setupUi(this);
    _config = defaultConfig;

    // buttons
    connect(ui->btnCancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(ui->btnOk, SIGNAL(clicked()), this, SLOT(accept()));

    // table
    ui->tableParams->setEditTriggers(QTableWidget::NoEditTriggers);
    ui->tableParams->setSelectionBehavior(QTableWidget::SelectionBehavior::SelectRows);

    // port names
    _portInfo = QSerialPortInfo::availablePorts();

    auto sorter = [](QSerialPortInfo& a, QSerialPortInfo& b) {
        return a.portName().size() < b.portName().size()  ? true :
               a.portName().size() == b.portName().size() ? a.portName() < b.portName() :
                                                            false;
    };

    std::sort(_portInfo.begin(), _portInfo.end(), sorter);

    ui->cmbName->clear();

    foreach (auto info, _portInfo)
    {
        ui->cmbName->addItem(QString("%1 -- %2").arg(info.portName()).arg(info.description()));
    }
    connect(ui->cmbName, SIGNAL(currentIndexChanged(int)), this, SLOT(_cmbNameChanged(int)));

    auto portName = _config.get("port", "").toString();
    if (!portName.isNull())
    {
        int idx;

        for (idx = 0; idx < _portInfo.size(); idx++)
        {
            if (_portInfo[idx].portName() == portName)
            {
                ui->cmbName->setCurrentIndex(idx);
                break;
            }
        }

        if (idx == _portInfo.size())
        {
            idx = 0;
        }

        if (_portInfo.size() > 0)
        {
            ui->cmbName->setCurrentIndex(idx);
            _cmbNameChanged(idx);
        }
    }
    else
    {
        ui->cmbName->setCurrentIndex(0);
        _cmbNameChanged(0);
    }

    // baudrates
    ui->cmbBaudrate->clear();
    int baudrate = _config.get("speed", 9600).toInt();
    foreach (auto speed, QSerialPortInfo::standardBaudRates())
    {
        ui->cmbBaudrate->addItem(QString("%1").arg(speed));
    }

    // groupboxes
    ui->rbgroupDataSize->setId(ui->rbDataSize5bits, QSerialPort::DataBits::Data5);
    ui->rbgroupDataSize->setId(ui->rbDataSize6bits, QSerialPort::DataBits::Data6);
    ui->rbgroupDataSize->setId(ui->rbDataSize7bits, QSerialPort::DataBits::Data7);
    ui->rbgroupDataSize->setId(ui->rbDataSize8bits, QSerialPort::DataBits::Data8);

    ui->rbgroupStopBits->setId(ui->rbStopBits1, QSerialPort::StopBits::OneStop);
    ui->rbgroupStopBits->setId(ui->rbStopBits1p5, QSerialPort::StopBits::OneAndHalfStop);
    ui->rbgroupStopBits->setId(ui->rbStopBits2, QSerialPort::StopBits::TwoStop);

    ui->rbgroupFlowControl->setId(ui->rbFlowCtlNone, QSerialPort::FlowControl::NoFlowControl);
    ui->rbgroupFlowControl->setId(ui->rbFlowCtlSoft, QSerialPort::FlowControl::SoftwareControl);
    ui->rbgroupFlowControl->setId(ui->rbFlowCtlHard, QSerialPort::FlowControl::HardwareControl);

    ui->rbgroupParity->setId(ui->rbParityNone, QSerialPort::Parity::NoParity);
    ui->rbgroupParity->setId(ui->rbParityEven, QSerialPort::Parity::EvenParity);
    ui->rbgroupParity->setId(ui->rbParityOdd, QSerialPort::Parity::OddParity);
    ui->rbgroupParity->setId(ui->rbParitySpace, QSerialPort::Parity::SpaceParity);
    ui->rbgroupParity->setId(ui->rbParityMark, QSerialPort::Parity::MarkParity);

    auto setGbId = [&](QButtonGroup* bg, int id) {
        auto button = bg->button(id);
        if (button)
        {
            button->setChecked(true);
        }
        else
        {
            bg->buttons()[0]->setChecked(true);
        }
    };

    setGbId(ui->rbgroupDataSize, _config.get("data_size", QSerialPort::DataBits::Data8).toInt());
    setGbId(ui->rbgroupFlowControl, _config.get("flow_control", QSerialPort::FlowControl::NoFlowControl).toInt());
    setGbId(ui->rbgroupParity, _config.get("parity", QSerialPort::Parity::NoParity).toInt());
    setGbId(ui->rbgroupStopBits, _config.get("stop_bits", QSerialPort::StopBits::OneStop).toInt());
}

ConfigSerialDialog::~ConfigSerialDialog()
{
    delete ui;
}

int ConfigSerialDialog::show(PathedValue& config)
{
    auto ret = this->exec();

    if (ret == 1)
    {
        config.merge(_config);

        config["speed"]        = ui->cmbBaudrate->currentText().toInt();
        config["port"]         = _portInfo[ui->cmbBaudrate->currentIndex()].portName();
        config["data_size"]    = ui->rbgroupDataSize->checkedId();
        config["parity"]       = ui->rbgroupParity->checkedId();
        config["stop_bits"]    = ui->rbgroupStopBits->checkedId();
        config["flow_control"] = ui->rbgroupFlowControl->checkedId();
    }

    return ret;
}

void ConfigSerialDialog::_cmbNameChanged(int idx)
{
    const auto& info = _portInfo[idx];

    auto addRow = [&](QString param, QString value) {
        int row = ui->tableParams->rowCount();
        ui->tableParams->setRowCount(row + 1);

        auto c1 = new QTableWidgetItem(param);
        auto c2 = new QTableWidgetItem(value);

        c1->setFlags(c1->flags() ^ (Qt::ItemIsEditable));
        c2->setFlags(c1->flags() ^ (Qt::ItemIsEditable));

        ui->tableParams->setItem(row, 0, c1);
        ui->tableParams->setItem(row, 1, c2);
    };

    ui->tableParams->setRowCount(0);

    addRow("Location", info.systemLocation());
    addRow("Name", info.portName());
    addRow("Description", info.description());
    addRow("Manufacturer", info.manufacturer());

    if (info.serialNumber().size() > 0)
    {
        addRow("Serial", info.serialNumber());
    }

    if (info.hasProductIdentifier())
    {
        addRow("Vendor ID", QString("%1").arg(info.vendorIdentifier()));
    }

    if (info.hasVendorIdentifier())
    {
        addRow("Product ID", QString("%1").arg(info.productIdentifier()));
    }
}
