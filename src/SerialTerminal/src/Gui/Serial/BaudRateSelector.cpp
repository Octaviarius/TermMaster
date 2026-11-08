#include "BaudRateSelector.h"

#include <QComboBox>
#include <QDebug>
#include <QIntValidator>
#include <QLineEdit>
#include <QLocale>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

BaudRateSelector::BaudRateSelector(const QVariant& customRates, QWidget* parent) :
    QWidget(parent), _combo(new QComboBox(this))
{
    auto       rates    = QSerialPortInfo::standardBaudRates();
    bool       invalid  = customRates.isValid() && customRates.typeId() != QMetaType::QVariantList;
    const auto custom   = customRates.toList();
    invalid            |= custom.size() > 256;
    for (qsizetype i = 0; i < qMin<qsizetype>(custom.size(), 256); ++i)
    {
        const auto& value  = custom[i];
        const auto  type   = value.typeId();
        bool        ok     = false;
        const auto  number = value.toLongLong(&ok);
        if ((type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
             || type == QMetaType::ULongLong)
            && ok && number > 0 && number <= std::numeric_limits<int>::max())
        {
            rates.append(static_cast<qint32>(number));
        }
        else
        {
            invalid = true;
        }
    }
    if (invalid)
    {
        qWarning("Invalid workspace serial/baudRates suggestions: expected at most 256 positive 32-bit integers.");
    }
    std::sort(rates.begin(), rates.end());
    rates.erase(std::unique(rates.begin(), rates.end()), rates.end());

    _combo->setObjectName("baudRateSelector");
    _combo->setEditable(true);
    _combo->setInsertPolicy(QComboBox::NoInsert);
    _combo->setMaxVisibleItems(16);
    for (const auto rate : rates)
    {
        _combo->addItem(QString::number(rate), rate);
    }
    auto* validator = new QIntValidator(1, std::numeric_limits<int>::max(), _combo);
    auto  locale    = QLocale::c();
    locale.setNumberOptions(QLocale::RejectGroupSeparator);
    validator->setLocale(locale);
    _combo->setValidator(validator);
    _combo->lineEdit()->setMaxLength(10);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_combo);
    connect(_combo, &QComboBox::editTextChanged, this, [this] {
        _untouched = false;
        if (_changed)
        {
            _changed();
        }
    });
}

QWidget* BaudRateSelector::widget()
{
    return this;
}
QVariant BaudRateSelector::value() const
{
    if (_untouched)
    {
        return _value;
    }
    bool       ok   = false;
    const auto rate = _combo->currentText().toInt(&ok);
    return ok && inputError().isEmpty() ? QVariant(rate) : QVariant(_combo->currentText());
}
void BaudRateSelector::setValue(const QVariant& value)
{
    const QSignalBlocker blocker(_combo);
    _value     = value;
    _untouched = true;
    _combo->setEditText(value.toString());
}
void BaudRateSelector::setChangedCallback(std::function<void()> callback)
{
    _changed = std::move(callback);
}
QString BaudRateSelector::inputError() const
{
    return _combo->lineEdit()->hasAcceptableInput() ? QString {} : tr("Baud rate must be between 1 and 2147483647.");
}
