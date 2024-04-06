#include "ConfigSerialWidget.h"

#include "ui_ConfigSerialWidget.h"
#include "utils.h"

#include <QButtonGroup>
#include <QIntValidator>
#include <QSerialPort>

static const auto _idx2Databits    = qVectorToQMap(QVector<QSerialPort::DataBits>({
    QSerialPort::DataBits::Data8,
    QSerialPort::DataBits::Data7,
    QSerialPort::DataBits::Data6,
    QSerialPort::DataBits::Data5,
}));
static const auto _databits2Idx    = inverseQMap(_idx2Databits);
static const auto _databitsStrings = QStringList({"8 bit", "7 bit", "6 bit", "5 bit"});

static const auto _idx2Stopbits    = qVectorToQMap(QVector<QSerialPort::StopBits>({
    QSerialPort::StopBits::OneStop,
    QSerialPort::StopBits::OneAndHalfStop,
    QSerialPort::StopBits::TwoStop,
}));
static const auto _stopbits2Idx    = inverseQMap(_idx2Stopbits);
static const auto _stopbitsStrings = QStringList({"1 bit", "1.5 bit", "2 bit"});

static const auto _idx2Parity    = qVectorToQMap(QVector<QSerialPort::Parity>({
    QSerialPort::Parity::NoParity,
    QSerialPort::Parity::EvenParity,
    QSerialPort::Parity::OddParity,
    QSerialPort::Parity::MarkParity,
    QSerialPort::Parity::SpaceParity,
}));
static const auto _parity2Idx    = inverseQMap(_idx2Parity);
static const auto _parityStrings = QStringList({"None", "Even", "Odd", "Mark", "Space"});

static const auto _idx2Flowcontrol    = qVectorToQMap(QVector<QSerialPort::FlowControl>({
    QSerialPort::FlowControl::NoFlowControl,
    QSerialPort::FlowControl::HardwareControl,
    QSerialPort::FlowControl::SoftwareControl,
}));
static const auto _flowcontrol2Idx    = inverseQMap(_idx2Flowcontrol);
static const auto _flowcontrolStrings = QStringList({"None", "Hardware", "Software"});

ConfigSerialWidget::ConfigSerialWidget(Registree config, QWidget* parent) :
    ConfigBaseWidget(config, parent), ui(new Ui::ConfigSerialWidget)
{
    ui->setupUi(this);

    // buttons
    connect(ui->btnUpdate, &QPushButton::clicked, this, &ConfigSerialWidget::updatePorts);

    // table
    ui->tableParams->setEditTriggers(QTableWidget::NoEditTriggers);
    ui->tableParams->setSelectionBehavior(QTableWidget::SelectionBehavior::SelectRows);

    // lists
    ui->listDataSize->addItems(_databitsStrings);
    ui->listStopBits->addItems(_stopbitsStrings);
    ui->listParity->addItems(_parityStrings);
    ui->listFlowControl->addItems(_flowcontrolStrings);

    ui->listDataSize->setCurrentRow(_config.get("data_size", 0).toInt());
    ui->listStopBits->setCurrentRow(_config.get("stop_bits", 0).toInt());
    ui->listParity->setCurrentRow(_config.get("parity", 0).toInt());
    ui->listFlowControl->setCurrentRow(_config.get("flow_control", 0).toInt());

    // port names
    updatePorts();

    // baudrates
    ui->cmbBaudrate->setValidator(new QIntValidator());
    ui->cmbBaudrate->clear();
    int baudrate = _config.get("speed", 9600).toInt();
    for (auto speed : QSerialPortInfo::standardBaudRates())
    {
        ui->cmbBaudrate->addItem(QString("%1").arg(speed));
        if (baudrate == speed)
        {
            ui->cmbBaudrate->setCurrentIndex(ui->cmbBaudrate->count());
        }
    }
    ui->cmbBaudrate->setCurrentText(QString("%1").arg(baudrate));
}

ConfigSerialWidget::~ConfigSerialWidget()
{
    delete ui;
}

void ConfigSerialWidget::commit()
{
    _config["speed"]        = ui->cmbBaudrate->currentText().toInt();
    _config["port"]         = _portInfo[ui->cmbBaudrate->currentIndex()].portName();
    _config["data_size"]    = ui->listDataSize->currentIndex();
    _config["stop_bits"]    = ui->listStopBits->currentIndex();
    _config["parity"]       = ui->listParity->currentIndex();
    _config["flow_control"] = ui->listFlowControl->currentIndex();
}

void ConfigSerialWidget::updatePorts()
{
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
}

void ConfigSerialWidget::_cmbNameChanged(int idx)
{
    if (idx < 0 || idx >= _portInfo.size())
    {
        return;
    }

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
