#pragma once

#include <Termaster/Core/Terminal/SerialDevice.h>
#include <memory>

/** Creates a disconnected, GUI-thread native pseudoterminal endpoint. */
std::unique_ptr<SerialDevice> createConsoleDevice();
