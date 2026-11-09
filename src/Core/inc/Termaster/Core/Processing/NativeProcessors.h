#pragma once

#include <QString>
class ProcessorCatalog;
/** Registers grouped Gate, configurable framing and HEX/Base64 codec processors. */
void registerNativeProcessors(ProcessorCatalog& catalog, const QString& owner);
