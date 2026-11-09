#pragma once

#include <Termaster/Core/Terminal/SerialDevice.h>
#include <memory>

/** Creates a GUI-thread TCP/UDP transport without opening a connection. */
std::unique_ptr<SerialDevice> createSocketDevice();
