#pragma once
#include <QVariantMap>

/** Structural validation for workspace-local, reusable processor block definitions.
 * Unknown properties remain owned by the settings document. Type availability and
 * configuration schemas are resolved when a processing line is prepared.
 */
class ProcessorBlocks
{
public:
    static bool validate(const QVariantMap& blocks, QString* error = nullptr);
};
