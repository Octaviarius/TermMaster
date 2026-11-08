#include "PortSelector.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCollator>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QVBoxLayout>
#include <algorithm>
#include <tuple>

namespace
{
QColor secondaryColor(const QPalette& palette, bool selected)
{
    const auto foreground = palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    const auto background = palette.color(selected ? QPalette::Highlight : QPalette::Base);
    if (selected)
    {
        return foreground;
    }
    return QColor((foreground.red() * 3 + background.red()) / 4,
                  (foreground.green() * 3 + background.green()) / 4,
                  (foreground.blue() * 3 + background.blue()) / 4);
}

class PortDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(option.fontMetrics.height() * 2 + 12);
        size.setWidth(qMax(size.width(), 320));
        return size;
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const QString primary = item.text;
        item.text.clear();
        const bool selected = item.state.testFlag(QStyle::State_Selected);
        if (selected)
        {
            item.backgroundBrush  = item.palette.highlight();
            item.state           &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        }
        auto* style = item.widget ? item.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
        QRect text = style->subElementRect(QStyle::SE_ItemViewItemText, &item, item.widget);
        text.adjust(0, 4, -6, -4);
        painter->save();
        painter->setFont(item.font);
        painter->setPen(item.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        painter->drawText(text,
                          (index.data(PortModel::SerialRole).toString().isEmpty() ? Qt::AlignVCenter : Qt::AlignTop)
                              | Qt::AlignLeft,
                          item.fontMetrics.elidedText(primary, Qt::ElideRight, text.width()));
        painter->setPen(secondaryColor(item.palette, selected));
        painter->drawText(text,
                          Qt::AlignBottom | Qt::AlignLeft,
                          item.fontMetrics.elidedText(index.data(PortModel::SerialRole).toString(),
                                                      Qt::ElideRight,
                                                      text.width()));
        painter->restore();
    }
};

/** Keep the editable buffer as the raw address; paint identity only while unfocused. */
class PortLineEdit final : public QLineEdit
{
public:
    explicit PortLineEdit(QComboBox* combo) : QLineEdit(combo), _combo(combo)
    {
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        const int row = _combo->findData(text(), Qt::EditRole);
        if (hasFocus() || row < 0 || text().isEmpty())
        {
            QLineEdit::paintEvent(event);
            return;
        }
        QPainter painter(this);
        painter.fillRect(rect(), palette().brush(QPalette::Base));
        const auto index = _combo->model()->index(row, 0);
        QRect      area  = rect().adjusted(3, 2, -3, -2);
        painter.setFont(font());
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(area,
                         Qt::AlignLeft
                             | (index.data(PortModel::SerialRole).toString().isEmpty() ? Qt::AlignVCenter :
                                                                                         Qt::AlignTop),
                         fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, area.width()));
        painter.setPen(secondaryColor(palette(), false));
        painter.drawText(area,
                         Qt::AlignLeft | Qt::AlignBottom,
                         fontMetrics().elidedText(index.data(PortModel::SerialRole).toString(),
                                                  Qt::ElideRight,
                                                  area.width()));
    }

private:
    QComboBox* _combo;
};
} // namespace

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
    if (role == Qt::DisplayRole)
    {
        return entry->description.isEmpty() ? entry->name : entry->name + QStringLiteral(" — ") + entry->description;
    }
    if (role == SerialRole)
    {
        return entry->serial.isEmpty() ? QString() : QStringLiteral("Serial: ") + entry->serial;
    }
    if (role == Qt::EditRole)
    {
        return entry->name;
    }
    if (role == Qt::ToolTipRole)
    {
        QStringList lines {entry->description};
        if (!entry->serial.isEmpty())
        {
            lines.append(tr("Serial: %1").arg(entry->serial));
        }
        lines.append(entry->classification.label());
        if (!entry->classification.evidence.isEmpty())
        {
            lines.append(entry->classification.evidence);
        }
        return Qt::convertFromPlainText(lines.join('\n'));
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
    _combo->setLineEdit(new PortLineEdit(_combo));
    _combo->setItemDelegate(new PortDelegate(_combo));
    _combo->setMinimumHeight(fontMetrics().height() * 2 + 12);
    _combo->setInsertPolicy(QComboBox::NoInsert);
    _combo->setModel(_model);
    _combo->setCompleter(nullptr);
    _combo->setMinimumContentsLength(15);
    _combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto* refreshButton = new QPushButton(QApplication::style()->standardIcon(QStyle::SP_BrowserReload), "", this);
    refreshButton->setToolTip("Refresh serial ports");
    refreshButton->setObjectName("serialPortRefresh");
    _details->setObjectName("serialPortDetails");
    _details->setTextFormat(Qt::PlainText);
    _details->setForegroundRole(QPalette::PlaceholderText);
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
    _combo->setCurrentIndex(_combo->findData(selected, Qt::EditRole));
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
    _combo->lineEdit()->update();
    _updateDetails();
}
void PortSelector::_updateDetails()
{
    const auto* port = _model->port(_combo->findData(_combo->currentText(), Qt::EditRole));
    if (!port)
    {
        _details->setText(_combo->currentText().isEmpty() ? tr("Select a port or enter its name.") :
                                                            tr("Port is not currently available."));
        return;
    }
    QStringList details;
    const auto  add = [&details](const QString& label, const QString& value) {
        if (!value.isEmpty())
        {
            details.append(label + ": " + value);
        }
    };
    add(tr("Location"), port->location);
    add(tr("Description"), port->description);
    add(tr("Manufacturer"), port->manufacturer);
    add(tr("Serial"), port->serial);
    add(tr("VID / PID"), port->identifiers);
    add(tr("Classification"), port->classification.label());
    add(tr("Evidence"), port->classification.evidence);
    _details->setText(details.join('\n'));
}
