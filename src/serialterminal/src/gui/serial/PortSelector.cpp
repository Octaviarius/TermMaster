#include "PortSelector.h"

#include <QApplication>
#include <QCollator>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <tuple>

bool PortModel::Port::operator==(const Port& other) const
{
    const auto fields = [](const Port& port) {
        const auto& c = port.classification;
        return std::tie(port.name,
                        port.location,
                        port.description,
                        port.manufacturer,
                        port.serial,
                        port.identifiers,
                        c.bus,
                        c.origin,
                        c.evidence,
                        c.instanceId,
                        c.service,
                        c.hardwareIds);
    };
    return fields(*this) == fields(other);
}
PortModel::PortModel(QObject* parent) : QAbstractListModel(parent)
{
}
int PortModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : _ports.size();
}
QVariant PortModel::data(const QModelIndex& index, int role) const
{
    const auto* entry = port(index.row());
    if (!entry || !index.isValid())
    {
        return {};
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        return entry->name;
    }
    if (role == Qt::ToolTipRole)
    {
        return entry->description + "\n" + entry->classification.label() + "\n" + entry->classification.evidence;
    }
    if (role == Qt::DecorationRole)
    {
        auto icon = entry->classification.bus == PortClassification::Bus::Unknown ? QStyle::SP_MessageBoxQuestion :
                                                                                    QStyle::SP_DriveHDIcon;
        if (entry->classification.origin == PortClassification::Origin::Software)
        {
            return QIcon(":/SerialTerminal/device-virtual.svg");
        }
        else if (entry->classification.bus == PortClassification::Bus::Usb)
        {
            return QIcon(":/SerialTerminal/device-usb.svg");
        }
        else if (entry->classification.bus == PortClassification::Bus::Pci)
        {
            return QIcon(":/SerialTerminal/device-pci.svg");
        }
        else if (entry->classification.bus == PortClassification::Bus::Legacy)
        {
            icon = QStyle::SP_ComputerIcon;
        }
        else if (entry->classification.bus == PortClassification::Bus::Bluetooth)
        {
            icon = QStyle::SP_DriveNetIcon;
        }
        return QApplication::style()->standardIcon(icon);
    }
    return {};
}
void PortModel::setPorts(QList<Port> ports, const QString& configured)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(ports.begin(), ports.end(), [&collator](const Port& left, const Port& right) {
        const auto result = collator.compare(left.name, right.name);
        return result == 0 ? left.name < right.name : result < 0;
    });
    ports.erase(std::unique(ports.begin(),
                            ports.end(),
                            [](const Port& left, const Port& right) { return left.name == right.name; }),
                ports.end());
    if (!configured.isEmpty() && std::none_of(ports.begin(), ports.end(), [&configured](const Port& port) {
            return port.name == configured;
        }))
    {
        ports.prepend({configured, {}, "Unavailable configured port", {}, {}, {}});
    }
    if (_ports == ports)
    {
        return;
    }
    beginResetModel();
    _ports = std::move(ports);
    endResetModel();
}
const PortModel::Port* PortModel::port(int row) const
{
    return row >= 0 && row < _ports.size() ? &_ports[row] : nullptr;
}
QList<PortModel::Port> PortModel::availablePorts()
{
    QList<Port> ports;
    const auto  inventory = QSerialPortInfo::availablePorts();
    QStringList names;
    for (const auto& info : inventory)
    {
        names.append(info.portName());
    }
    const auto classifications = PortClassification::inspectMany(names);
    for (const auto& info : inventory)
    {
        QStringList identifiers;
        if (info.hasVendorIdentifier())
        {
            identifiers.append(QString("VID %1").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0')).toUpper());
        }
        if (info.hasProductIdentifier())
        {
            identifiers.append(QString("PID %1").arg(info.productIdentifier(), 4, 16, QLatin1Char('0')).toUpper());
        }
        ports.append({info.portName(),
                      info.systemLocation(),
                      info.description(),
                      info.manufacturer(),
                      info.serialNumber(),
                      identifiers.join(" / "),
                      classifications.value(info.portName().toUpper())});
    }
    return ports;
}
PortSelector::PortSelector(const QVariantMap& options, QWidget* parent, Inventory inventory) :
    QWidget(parent), _model(new PortModel(this)), _combo(new QComboBox(this)), _details(new QLabel(this)),
    _inventory(std::move(inventory))
{
    if (!_inventory)
    {
        _inventory = PortModel::availablePorts;
    }
    _combo->setObjectName("serialPortCombo");
    _combo->setEditable(true);
    _combo->setInsertPolicy(QComboBox::NoInsert);
    _combo->setModel(_model);
    _combo->setMinimumContentsLength(15);
    auto* refreshButton = new QPushButton(QApplication::style()->standardIcon(QStyle::SP_BrowserReload), "", this);
    refreshButton->setToolTip("Refresh serial ports");
    refreshButton->setObjectName("serialPortRefresh");
    _details->setObjectName("serialPortDetails");
    _details->setTextFormat(Qt::PlainText);
    _details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _details->setWordWrap(true);
    auto* row = new QHBoxLayout;
    row->addWidget(_combo, 1);
    row->addWidget(refreshButton);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(row);
    layout->addWidget(_details);
    connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
    connect(_combo, &QComboBox::editTextChanged, this, [this] {
        _updateDetails();
        const auto callback = _changed;
        if (callback)
        {
            callback();
        }
    });
    connect(&_timer, &QTimer::timeout, this, [this] { refresh(); });
    const int interval = qBound(500, options.value("refreshIntervalMs", 1000).toInt(), 60000);
    _timer.start(interval);
    refresh();
}
PortSelector::~PortSelector()
{
    _timer.stop();
    _changed = {};
    if (_worker)
    {
        disconnect(_worker, nullptr, this, nullptr);
        _worker->wait();
        delete _worker;
    }
}
QWidget* PortSelector::widget()
{
    return this;
}
QVariant PortSelector::value() const
{
    return _combo->currentText();
}
void PortSelector::setValue(const QVariant& value)
{
    QSignalBlocker blocker(_combo);
    _combo->setEditText(value.toString());
    _applySnapshot();
}
void PortSelector::setChangedCallback(std::function<void()> callback)
{
    _changed = std::move(callback);
}
void PortSelector::refresh()
{
    if (_worker)
    {
        return;
    }
    struct Snapshot
    {
        QList<PortModel::Port> ports;
        bool                   failed = false;
    };
    const auto snapshot  = std::make_shared<Snapshot>();
    const auto inventory = _inventory;
    _worker              = QThread::create([inventory, snapshot] {
        try
        {
            snapshot->ports = inventory();
        }
        catch (...)
        {
            snapshot->failed = true;
        }
    });
    connect(_worker, &QThread::finished, this, [this, snapshot] {
        // finished precedes final thread cleanup; join before releasing any
        // plugin-instantiated callable, result or thread object.
        _worker->wait();
        delete _worker;
        _worker = nullptr;
        if (snapshot->failed)
        {
            _details->setText("Serial port inventory failed.");
            return;
        }
        _snapshot = snapshot->ports;
        _applySnapshot();
    });
    _worker->start();
}
void PortSelector::_applySnapshot()
{
    const QString        selected        = _combo->currentText();
    auto*                lineEdit        = _combo->lineEdit();
    const int            cursor          = lineEdit->cursorPosition();
    const int            selection       = lineEdit->selectionStart();
    const int            selectionLength = lineEdit->selectedText().size();
    const QSignalBlocker blocker(_combo);
    _model->setPorts(_snapshot, selected);
    _combo->setCurrentIndex(_combo->findText(selected));
    _combo->setEditText(selected);
    if (selection >= 0)
    {
        // Preserve both selection direction and caret during automatic refresh.
        lineEdit->setSelection(cursor == selection ? selection + selectionLength : selection,
                               cursor == selection ? -selectionLength : selectionLength);
    }
    else
    {
        lineEdit->setCursorPosition(cursor);
    }
    _updateDetails();
}
void PortSelector::_updateDetails()
{
    const auto* port = _model->port(_combo->findText(_combo->currentText()));
    if (!port)
    {
        _details->setText("Port is not currently available.");
        return;
    }
    _details->setText(
        QString(
            "Location: %1\nDescription: %2\nManufacturer: %3\nSerial: %4\nVID / PID: %5\nClassification: %6\nEvidence: %7")
            .arg(port->location,
                 port->description,
                 port->manufacturer,
                 port->serial,
                 port->identifiers,
                 port->classification.label(),
                 port->classification.evidence));
}
