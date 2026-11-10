#pragma once

#include <QByteArray>
#include <Termaster/Core/Formatting/FormatSchema.h>

/** Immutable parsed formatters and literals; independent Values permit concurrent rendering. */
class CompiledFormat
{
public:
    ~CompiledFormat();
    Q_DISABLE_COPY_MOVE(CompiledFormat)

    static std::shared_ptr<const CompiledFormat> compile(const QString&      pattern,
                                                         const FormatSchema& schema,
                                                         QString*            error = nullptr);

    template <typename T>
    bool uses(const FormatSchema::Slot<T>& slot) const
    {
        return _uses(slot._identity, slot._index);
    }

    /** Replaces output with owning bytes on success; preserves it on failure. */
    bool render(const FormatSchema::Values& values,
                QByteArray&                 output,
                qsizetype                   maximumBytes,
                QString*                    error = nullptr) const;

private:
    struct Data;

    std::unique_ptr<Data> _data;

    explicit CompiledFormat(std::unique_ptr<Data> data);
    bool _uses(const std::shared_ptr<const void>& identity, qsizetype index) const;
};
