#include "FormatSchemaData.h"

#include <Termaster/Core/Formatting/CompiledFormat.h>
#include <fmt/format.h>
#include <stdexcept>

namespace
{
class BoundedOutput
{
public:
    QByteArray bytes;
    qsizetype  maximum;

    explicit BoundedOutput(qsizetype limit) : maximum(limit)
    {
    }

    void append(QByteArrayView value)
    {
        if (value.size() > maximum - bytes.size())
        {
            throw std::length_error("Formatted output exceeds the byte limit");
        }
        bytes.append(value.data(), value.size());
    }
};

/** Bounds fmt's scratch growth while allowing its geometric allocation policy. */
class BoundedAllocator
{
public:
    using value_type = char;
    size_t maximum;

    explicit BoundedAllocator(qsizetype limit) : maximum(std::max<size_t>(256, size_t(limit)))
    {
    }
    char* allocate(size_t count)
    {
        if (count > maximum)
        {
            throw std::length_error("Formatted output exceeds the byte limit");
        }
        return std::allocator<char>().allocate(count);
    }
    size_t max_size() const noexcept
    {
        return maximum;
    }
    void deallocate(char* pointer, size_t count) noexcept
    {
        std::allocator<char>().deallocate(pointer, count);
    }
};

using Context   = fmt::format_context;
using Scratch   = fmt::basic_memory_buffer<char, 256, BoundedAllocator>;
using Formatter = std::variant<std::monostate,
                               fmt::formatter<qint64>,
                               fmt::formatter<quint64>,
                               fmt::formatter<double>,
                               fmt::formatter<bool>,
                               fmt::formatter<fmt::string_view>>;

template <typename T>
Formatter prepare(const QByteArray& spec)
{
    auto terminated = spec;
    terminated.append('}');
    fmt::parse_context<char> context(fmt::string_view(terminated.data(), size_t(terminated.size())));
    fmt::formatter<T>        formatter;
    const auto               end = formatter.parse(context);
    if (end != terminated.constData() + terminated.size() - 1)
    {
        throw std::invalid_argument("Format specification was not fully consumed");
    }

    return formatter;
}

void validateSpec(const QByteArray& spec)
{
    for (qsizetype index = 0; index < spec.size(); ++index)
    {
        const auto ch = spec[index];
        if (ch == '{' || ch == '}')
        {
            throw std::invalid_argument("Nested and dynamic format fields are unsupported");
        }

        if (ch < '0' || ch > '9')
        {
            continue;
        }

        const bool     precision = index > 0 && spec[index - 1] == '.';
        const unsigned limit     = precision ? 512 : 4096;
        unsigned       number    = 0;
        do
        {
            number = number * 10 + unsigned(spec[index] - '0');
            if (number > limit)
            {
                throw std::invalid_argument(precision ? "Format precision exceeds 512" : "Format width exceeds 4096");
            }
            ++index;
        } while (index < spec.size() && spec[index] >= '0' && spec[index] <= '9');
        --index;
    }
}

int hexDigit(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return -1;
}
} // namespace

struct CompiledFormat::Data
{
    struct Segment
    {
        QByteArray literal;
        qsizetype  slot = -1;
        Formatter  formatter;
        bool       rawBytes = false;
        bool       rawText  = false;
    };

    std::shared_ptr<const FormatSchema::Data> schema;
    std::vector<bool>                         used;
    std::vector<Segment>                      segments;
    qsizetype                                 fixedReserve = 0;
    std::vector<qsizetype>                    variableCounts;
};

CompiledFormat::CompiledFormat(std::unique_ptr<Data> data) : _data(std::move(data))
{
}
CompiledFormat::~CompiledFormat() = default;

std::shared_ptr<const CompiledFormat> CompiledFormat::compile(const QString&      pattern,
                                                              const FormatSchema& schema,
                                                              QString*            error)
{
    if (error)
    {
        error->clear();
    }

    try
    {
        // UTF-16 length first avoids allocating arbitrarily large UTF-8 input.
        if (pattern.size() > 16384)
        {
            throw std::invalid_argument("Format template exceeds 16384 bytes");
        }
        const auto bytes = pattern.toUtf8();
        if (bytes.size() > 16384)
        {
            throw std::invalid_argument("Format template exceeds 16384 bytes");
        }

        schema._data->sealed = true;
        auto data            = std::make_unique<Data>();
        data->schema         = schema._data;
        data->used.resize(schema._data->entries.size());
        QByteArray literal;
        qsizetype  fieldCount   = 0;
        const auto flushLiteral = [&] {
            if (!literal.isEmpty())
            {
                Data::Segment segment;
                segment.literal = std::move(literal);
                data->segments.push_back(std::move(segment));
                literal.clear();
            }
        };

        for (qsizetype index = 0; index < bytes.size(); ++index)
        {
            const auto ch = bytes[index];
            if (ch == '\\')
            {
                if (++index == bytes.size())
                {
                    throw std::invalid_argument("Incomplete byte escape");
                }
                switch (bytes[index])
                {
                    case 'e':
                        literal.append('\x1b');
                        break;
                    case 'r':
                        literal.append('\r');
                        break;
                    case 'n':
                        literal.append('\n');
                        break;
                    case 't':
                        literal.append('\t');
                        break;
                    case '0':
                        literal.append('\0');
                        break;
                    case '\\':
                        literal.append('\\');
                        break;
                    case 'x':
                    {
                        if (index + 2 >= bytes.size())
                        {
                            throw std::invalid_argument("Hex byte escape requires two digits");
                        }
                        const int high = hexDigit(bytes[index + 1]);
                        const int low  = hexDigit(bytes[index + 2]);
                        if (high < 0 || low < 0)
                        {
                            throw std::invalid_argument("Hex byte escape requires two digits");
                        }
                        literal.append(char(high * 16 + low));
                        index += 2;
                        break;
                    }
                    default:
                        throw std::invalid_argument("Unsupported byte escape");
                }
                continue;
            }
            if ((ch == '{' || ch == '}') && index + 1 < bytes.size() && bytes[index + 1] == ch)
            {
                literal.append(ch);
                ++index;
                continue;
            }
            if (ch == '}')
            {
                throw std::invalid_argument("Unmatched closing format brace");
            }
            if (ch != '{')
            {
                literal.append(ch);
                continue;
            }
            if (++fieldCount > 1024)
            {
                throw std::invalid_argument("Format exceeds 1024 fields");
            }

            flushLiteral();
            const auto end = bytes.indexOf('}', index + 1);
            if (end < 0)
            {
                throw std::invalid_argument("Unclosed format field");
            }
            const auto field = bytes.mid(index + 1, end - index - 1);
            if (field.contains('{'))
            {
                throw std::invalid_argument("Nested and dynamic format fields are unsupported");
            }
            const auto colon     = field.indexOf(':');
            const auto nameBytes = colon < 0 ? field : field.left(colon);
            const auto name      = QString::fromUtf8(nameBytes);
            const auto spec      = colon < 0 ? QByteArray() : field.mid(colon + 1);
            validateSpec(spec);

            qsizetype slot = -1;
            for (size_t entry = 0; entry < data->schema->entries.size(); ++entry)
            {
                if (data->schema->entries[entry].name == name)
                {
                    slot = qsizetype(entry);
                    break;
                }
            }
            if (slot < 0)
            {
                throw std::invalid_argument("Unknown format field: " + name.toStdString());
            }

            Data::Segment segment;
            segment.slot = slot;
            switch (data->schema->entries[size_t(slot)].kind)
            {
                case FormatSchema::Kind::Signed:
                    segment.formatter = prepare<qint64>(spec);
                    break;
                case FormatSchema::Kind::Unsigned:
                    segment.formatter = prepare<quint64>(spec);
                    break;
                case FormatSchema::Kind::Floating:
                    segment.formatter = prepare<double>(spec);
                    break;
                case FormatSchema::Kind::Boolean:
                    segment.formatter = prepare<bool>(spec);
                    break;
                case FormatSchema::Kind::Text:
                    segment.rawText = spec.isEmpty();
                    if (!segment.rawText)
                    {
                        segment.formatter = prepare<fmt::string_view>(spec);
                    }
                    break;
                case FormatSchema::Kind::Bytes:
                    segment.rawBytes = spec.isEmpty();
                    if (!segment.rawBytes)
                    {
                        segment.formatter = prepare<fmt::string_view>(spec);
                    }
                    break;
            }
            data->used[size_t(slot)] = true;
            data->segments.push_back(std::move(segment));
            index = end;
        }
        flushLiteral();

        data->variableCounts.resize(data->schema->entries.size());
        for (const auto& segment : data->segments)
        {
            if (segment.slot < 0)
            {
                data->fixedReserve += segment.literal.size();
            }
            else if (data->schema->entries[size_t(segment.slot)].kind == FormatSchema::Kind::Text
                     || data->schema->entries[size_t(segment.slot)].kind == FormatSchema::Kind::Bytes)
            {
                ++data->variableCounts[size_t(segment.slot)];
            }
            else
            {
                data->fixedReserve += 32;
            }
        }

        return std::shared_ptr<const CompiledFormat>(new CompiledFormat(std::move(data)));
    }
    catch (const std::exception& exception)
    {
        if (error)
        {
            *error = QString::fromUtf8(exception.what());
        }
        return {};
    }
}

bool CompiledFormat::render(const FormatSchema::Values& values,
                            QByteArray&                 output,
                            qsizetype                   maximumBytes,
                            QString*                    error) const
{
    if (error)
    {
        error->clear();
    }

    try
    {
        if (maximumBytes < 0)
        {
            throw std::invalid_argument("Format byte limit must be nonnegative");
        }
        if (values._identity != _data->schema)
        {
            throw std::invalid_argument("Format values belong to another schema");
        }

        for (size_t index = 0; index < _data->used.size(); ++index)
        {
            if (!_data->used[index])
            {
                continue;
            }
            const auto actual   = values._values[index].index();
            const auto expected = size_t(_data->schema->entries[index].kind) + 1;
            if (actual == 0)
            {
                throw std::invalid_argument("Required format value is unset");
            }
            if (actual != expected)
            {
                throw std::invalid_argument("Format value has the wrong type");
            }
        }

        BoundedOutput destination(maximumBytes);
        qsizetype     reserve = std::min(maximumBytes, _data->fixedReserve);
        for (size_t index = 0; index < _data->variableCounts.size(); ++index)
        {
            const auto count = _data->variableCounts[index];
            if (!count)
            {
                continue;
            }
            const auto& value      = values._values[index];
            const auto  size       = std::holds_alternative<QString>(value) ? values._utf8[index].size() :
                                                                              std::get<QByteArrayView>(value).size();
            const auto  multiplier = count;
            reserve += size > (maximumBytes - reserve) / multiplier ? maximumBytes - reserve : size * multiplier;
        }
        destination.bytes.reserve(reserve);
        Scratch scratch {BoundedAllocator(maximumBytes)};
        Context context(fmt::appender(scratch), {});
        for (const auto& segment : _data->segments)
        {
            if (segment.slot < 0)
            {
                destination.append(segment.literal);
                continue;
            }

            const auto& value = values._values[size_t(segment.slot)];
            if (segment.rawText)
            {
                destination.append(values._utf8[size_t(segment.slot)]);
                continue;
            }
            if (segment.rawBytes)
            {
                destination.append(std::get<QByteArrayView>(value));
                continue;
            }
            scratch.clear();
            std::visit(
                [&](const auto& formatter) {
                    using F = std::decay_t<decltype(formatter)>;
                    if constexpr (std::is_same_v<F, fmt::formatter<qint64>>)
                    {
                        formatter.format(std::get<qint64>(value), context);
                    }
                    else if constexpr (std::is_same_v<F, fmt::formatter<quint64>>)
                    {
                        formatter.format(std::get<quint64>(value), context);
                    }
                    else if constexpr (std::is_same_v<F, fmt::formatter<double>>)
                    {
                        formatter.format(std::get<double>(value), context);
                    }
                    else if constexpr (std::is_same_v<F, fmt::formatter<bool>>)
                    {
                        formatter.format(std::get<bool>(value), context);
                    }
                    else if constexpr (std::is_same_v<F, fmt::formatter<fmt::string_view>>)
                    {
                        const auto view = std::holds_alternative<QString>(value) ?
                                              QByteArrayView(values._utf8[size_t(segment.slot)]) :
                                              std::get<QByteArrayView>(value);
                        formatter.format(fmt::string_view(view.data(), size_t(view.size())), context);
                    }
                },
                segment.formatter);
            destination.append(QByteArrayView(scratch.data(), qsizetype(scratch.size())));
        }

        output = std::move(destination.bytes);
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error)
        {
            *error = QString::fromUtf8(exception.what());
        }
        return false;
    }
}

bool CompiledFormat::_uses(const std::shared_ptr<const void>& identity, qsizetype index) const
{
    return identity == _data->schema && index >= 0 && size_t(index) < _data->used.size() && _data->used[size_t(index)];
}
