#include "SerialByteDevice.h"

#include <Termaster/SerialTerminal/SerialTerminal.h>

std::unique_ptr<SerialDevice> createSerialDevice()
{
    return std::make_unique<SerialByteDevice>();
}
