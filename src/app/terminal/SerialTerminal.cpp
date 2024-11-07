#include "SerialTerminal.h"

#include "core/utils.h"

const EnumReflection<QSerialPort::DataBits> SerialPortEnumReflection::dataBits = EnumReflection<QSerialPort::DataBits>({
    {"8 bits", QSerialPort::DataBits::Data8},
    {"7 bits", QSerialPort::DataBits::Data7},
    {"6 bits", QSerialPort::DataBits::Data6},
    {"5 bits", QSerialPort::DataBits::Data5},
});

const EnumReflection<QSerialPort::StopBits> SerialPortEnumReflection::stopBits = EnumReflection<QSerialPort::StopBits>({
    {   "1 bit",        QSerialPort::StopBits::OneStop},
    {"1.5 bits", QSerialPort::StopBits::OneAndHalfStop},
    {  "2 bits",        QSerialPort::StopBits::TwoStop},
});

const EnumReflection<QSerialPort::Parity> SerialPortEnumReflection::parity = EnumReflection<QSerialPort::Parity>({
    { "None",    QSerialPort::Parity::NoParity},
    { "Even",  QSerialPort::Parity::EvenParity},
    {  "Odd",   QSerialPort::Parity::OddParity},
    {"Space", QSerialPort::Parity::SpaceParity},
    { "Mark",  QSerialPort::Parity::MarkParity},
});

const EnumReflection<QSerialPort::FlowControl>
    SerialPortEnumReflection::flowControl = EnumReflection<QSerialPort::FlowControl>({
        {    "None",   QSerialPort::FlowControl::NoFlowControl},
        {"Hardware", QSerialPort::FlowControl::HardwareControl},
        {"Software", QSerialPort::FlowControl::SoftwareControl},
});

template <>
void Settings::setValue(QAnyStringView key, const SerialTerminalConfig& v)
{
    beginGroup(key);
    setValue("port_name", v.portName);
    setValue("baudrate", v.baudRate);
    setValue("data_bits", v.dataBits);
    setValue("stop_bits", v.stopBits);
    setValue("parity", v.parity);
    setValue("flow_control", v.flowControl);
    endGroup();
}

template <>
SerialTerminalConfig Settings::value(QAnyStringView key, const SerialTerminalConfig& fallback)
{
    SerialTerminalConfig res;

    res.portName = value<QString>("port_name", fallback.portName);
    res.baudRate = value("baudrate", fallback.baudRate);

    res.dataBits = SerialPortEnumReflection::dataBits.fixEnumValue(value("data_bits", fallback.dataBits),
                                                                   fallback.dataBits);

    res.stopBits = SerialPortEnumReflection::stopBits.fixEnumValue(value("stop_bits", fallback.stopBits),
                                                                   fallback.stopBits);

    res.parity = SerialPortEnumReflection::parity.fixEnumValue(value("parity", fallback.parity), fallback.parity);

    res.flowControl = SerialPortEnumReflection::flowControl.fixEnumValue(value("flow_control", fallback.flowControl),
                                                                         fallback.flowControl);

    return res;
}

uint qHash(const QSerialPortInfo& key)
{
    return qHash(key.portName());
}

bool operator==(const QSerialPortInfo& a, const QSerialPortInfo& b)
{
    return a.portName() == b.portName();
}

//------------------------------------------------------------------------

SerialTerminal::SerialTerminal(QObject* parent) : ITerminal(parent)
{
    auto dataReady = [this]() { emit dataRead(_serialPort.readAll()); };

    connect(&_serialPort, &QSerialPort::readyRead, this, dataReady);
}

SerialTerminal::~SerialTerminal()
{
}

void SerialTerminal::setConfig(const SerialTerminalConfig& config)
{
    if (_config != config)
    {
        _config = config;
        _serialPort.setPortName(_config.portName);
        _serialPort.setBaudRate(_config.baudRate);
        _serialPort.setDataBits(_config.dataBits);
        _serialPort.setStopBits(_config.stopBits);
        _serialPort.setParity(_config.parity);
        _serialPort.setFlowControl(_config.flowControl);
    }
}

bool SerialTerminal::open()
{
    bool ret = true;

    if (!_serialPort.isOpen())
    {
        _setState(State::Openning);
        if (_serialPort.open(QIODevice::ReadWrite))
        {
            _setState(State::Opened);
        }
        else
        {
            ret = false;
            _setState(State::Closed);
        }
    }

    return ret;
}

bool SerialTerminal::close()
{
    bool ret = true;

    if (_serialPort.isOpen())
    {
        _setState(State::Closing);
        _serialPort.close();
        _setState(State::Closed);
    }

    return ret;
}

bool SerialTerminal::dataWrite(QByteArray data)
{
    return _serialPort.write(data);
}
