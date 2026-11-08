#pragma once

#include <QObject>
#include <QSerialPortInfo>
#include <QSet>
#include <Termaster/Core/Singleton.h>

class TerminalManager : public QObject, public Singleton<TerminalManager>
{
    Q_OBJECT
public:
    friend class Singleton<TerminalManager>;

    QList<QSerialPortInfo> availableSerialPorts();

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    TerminalManager();

    QSet<QSerialPortInfo> _serialPorts;

signals:
    void serialPortsChanged(QSet<QSerialPortInfo> availableSerialPorts,
                            QSet<QSerialPortInfo> added,
                            QSet<QSerialPortInfo> removed);
};
