#pragma once

#include <Termaster/Core/Processing/Processor.h>
#include <memory>
#include <optional>

/** Private Lineflow boundary recorder. Configuration/preparation never performs file I/O. */
class LineflowEndpoint
{
public:
    /** Flush an owned recorder before cancellation to report final persistence errors. */
    static bool                       flush(Processor& recorder, QString* error = nullptr);
    static QByteArray                 settingsSchema(int channels, bool allowLines = true);
    static QVariantMap                defaults(int channels);
    static std::optional<QVariantMap> resolveConfiguration(const QVariantMap& stored,
                                                           const QVariantMap& overrides,
                                                           int                channels,
                                                           QString*           error = nullptr);
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             int                channels,
                                             Processor::Limits  limits,
                                             QString*           error = nullptr);
};
