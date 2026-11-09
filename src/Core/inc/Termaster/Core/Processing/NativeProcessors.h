#pragma once

#include <QString>
class ProcessorCatalog;
/** Registers native.identity, native.cr-framer and native.hex-decoder. */
void registerNativeProcessors(ProcessorCatalog& catalog, const QString& owner);
