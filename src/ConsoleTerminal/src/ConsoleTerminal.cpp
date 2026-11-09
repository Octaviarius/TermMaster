#include "ConsoleDevice.h"

#include <Termaster/ConsoleTerminal/ConsoleTerminal.h>

std::unique_ptr<SerialDevice> createConsoleDevice()
{
    return std::make_unique<ConsoleDevice>();
}
