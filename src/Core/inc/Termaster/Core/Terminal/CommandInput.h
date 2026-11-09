#pragma once
#include <QByteArray>
#include <QString>
#include <optional>

/** Stateless bounded preparation of command bytes, independent of display paste modes. */
class CommandInput
{
public:
    static constexpr qsizetype MaximumBytes = 4 * 1024 * 1024;

    /** Commands are encoded as UTF-8.
     * Custom terminators accept UTF-8 literals and \\r, \\n, \\0, \\\\, \\xHH escapes.
     */
    static std::optional<QByteArray> prepare(const QString& text,
                                             const QString& terminator,
                                             const QString& custom,
                                             QString*       error = nullptr);
};
