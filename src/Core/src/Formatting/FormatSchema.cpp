#include "FormatSchemaData.h"

#include <Termaster/Core/Formatting/FormatSchema.h>
#include <stdexcept>

FormatSchema::Values::Values(std::shared_ptr<const void> identity, qsizetype size) :
    _identity(std::move(identity)), _values(static_cast<size_t>(size)), _utf8(static_cast<size_t>(size))
{
}

void FormatSchema::Values::clear()
{
    for (auto& value : _values)
    {
        value = std::monostate();
    }
    for (auto& text : _utf8)
    {
        text.clear();
    }
}

bool FormatSchema::Values::_set(const std::shared_ptr<const void>& identity,
                                qsizetype                          index,
                                Value                              value,
                                QString*                           error)
{
    if (error)
    {
        error->clear();
    }

    if (!identity || identity != _identity || index < 0 || size_t(index) >= _values.size())
    {
        if (error)
        {
            *error = QStringLiteral("Format slot belongs to another schema");
        }
        return false;
    }

    if (std::holds_alternative<QString>(value))
    {
        _utf8[size_t(index)] = std::get<QString>(value).toUtf8();
    }
    _values[size_t(index)] = std::move(value);
    return true;
}

FormatSchema::FormatSchema() : _data(std::make_shared<Data>())
{
}
FormatSchema::~FormatSchema() = default;

FormatSchema::Values FormatSchema::createValues() const
{
    _data->sealed = true;
    return Values(_data, qsizetype(_data->entries.size()));
}

std::shared_ptr<const void> FormatSchema::_identity() const
{
    return _data;
}

qsizetype FormatSchema::_add(const QString& name, Kind kind)
{
    if (_data->sealed)
    {
        throw std::invalid_argument("Format schema is sealed");
    }
    if (_data->entries.size() >= 256)
    {
        throw std::invalid_argument("Format schema exceeds 256 slots");
    }

    const auto isStart = [](QChar ch) {
        return (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) || (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
            || ch == QLatin1Char('_');
    };
    if (name.isEmpty() || !isStart(name.front()))
    {
        throw std::invalid_argument("Format slot name must be an ASCII identifier");
    }

    for (QChar ch : name)
    {
        if (!isStart(ch) && !(ch >= QLatin1Char('0') && ch <= QLatin1Char('9')))
        {
            throw std::invalid_argument("Format slot name must be an ASCII identifier");
        }
    }
    for (const auto& entry : _data->entries)
    {
        if (entry.name == name)
        {
            throw std::invalid_argument("Duplicate format slot name");
        }
    }

    const auto index = qsizetype(_data->entries.size());
    _data->entries.push_back({name, kind});
    return index;
}
