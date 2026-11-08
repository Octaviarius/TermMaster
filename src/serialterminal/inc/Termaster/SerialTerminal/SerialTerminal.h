#pragma once
#include <Termaster/Core/terminal/SerialDevice.h>
#include <memory>
/** Creates a GUI-thread serial byte transport. No port is opened by construction. */
std::unique_ptr<SerialDevice> createSerialDevice();
