#include "SocketDevice.h"

#include <Termaster/SocketTerminal/SocketTerminal.h>

std::unique_ptr<SerialDevice> createSocketDevice()
{
    return std::make_unique<SocketDevice>();
}
