#include "SerialTerminal.h"

SerialTerminal::SerialTerminal()
{
}

void SerialTerminal::onConfigChanged()
{
}

void SerialTerminal::inputData(QByteArray data)
{
}

void SerialTerminal::start()
{
    if (_serialPort.isOpen())
    {
        _serialPort.close();
    }

    _serialPort.setPortName(_config["port"].toString());
    _serialPort.setBaudRate(_config["speed"].toInt());
    _serialPort.setDataBits(static_cast<QSerialPort::DataBits>(_config["data_bits"].toInt()));
    _serialPort.setStopBits(static_cast<QSerialPort::StopBits>(_config["stop_bits"].toInt()));
    _serialPort.setParity(static_cast<QSerialPort::Parity>(_config["parity"].toInt()));
    _serialPort.setFlowControl(static_cast<QSerialPort::FlowControl>(_config["flow_control"].toInt()));
}

void SerialTerminal::stop()
{
}

void SerialTerminal::clearBuffer()
{
}
