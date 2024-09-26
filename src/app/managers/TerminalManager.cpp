#include "TerminalManager.h"

#include "core/utils.h"
#include "terminal/SerialTerminal.h"

TerminalManager::TerminalManager()
{
    startTimer(1000);
    timerEvent(nullptr);
}

QList<QSerialPortInfo> TerminalManager::availableSerialPorts()
{
    return qSetToList(_serialPorts);
}

void TerminalManager::timerEvent(QTimerEvent* event)
{
    auto newPortInfos = qListToSet(QSerialPortInfo::availablePorts());

    auto toAdd = newPortInfos - _serialPorts;
    auto toDel = _serialPorts - newPortInfos;

    _serialPorts = newPortInfos;

    if (toAdd.count() > 0 || toDel.count() > 0)
    {
        emit serialPortsChanged(_serialPorts, toAdd, toDel);
    }
}
