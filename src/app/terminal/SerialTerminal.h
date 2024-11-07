#pragma once

#include "core/EnumReflection.h"
#include "core/Settings.h"
#include "terminal/ITerminal.h"

#include <QSerialPort>
#include <qserialportinfo.h>

struct SerialTerminalConfig
{
    QString portName = "";
    uint    baudRate = 9600;

    QSerialPort::Parity      parity      = QSerialPort::Parity ::NoParity;
    QSerialPort::DataBits    dataBits    = QSerialPort::DataBits::Data8;
    QSerialPort::StopBits    stopBits    = QSerialPort::StopBits::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::FlowControl::NoFlowControl;

    bool operator==(const SerialTerminalConfig& other)
    {
        return parity == other.parity && dataBits == other.dataBits && stopBits == other.stopBits
            && flowControl == other.flowControl;
    }

    bool operator!=(const SerialTerminalConfig& other)
    {
        return !(*this == other);
    }
};

struct SerialPortEnumReflection
{
    static const EnumReflection<QSerialPort::DataBits>    dataBits;
    static const EnumReflection<QSerialPort::StopBits>    stopBits;
    static const EnumReflection<QSerialPort::Parity>      parity;
    static const EnumReflection<QSerialPort::FlowControl> flowControl;
};

class SerialTerminal : public ITerminal
{
    Q_OBJECT
public:
    SerialTerminal(QObject* parent);
    virtual ~SerialTerminal();

    void setConfig(const SerialTerminalConfig& config);

    bool open() override;
    bool close() override;

private:
    SerialTerminalConfig _config;
    QSerialPort          _serialPort;

public slots:
    bool dataWrite(QByteArray data) override;
};

template <>
void Settings::setValue(QAnyStringView key, const SerialTerminalConfig& v);
template <>
SerialTerminalConfig Settings::value(QAnyStringView key, const SerialTerminalConfig& fallback) ;

uint qHash(const QSerialPortInfo& key);
bool operator==(const QSerialPortInfo& a, const QSerialPortInfo& b);
