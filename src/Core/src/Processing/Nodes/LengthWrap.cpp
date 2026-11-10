#include "LengthWrap.h"

LengthWrap::LengthWrap(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _field(configuration), _maxSteps(limits.maxSteps)
{
}

QByteArray LengthWrap::schema()
{
    return LengthField::schema();
}

std::unique_ptr<Processor> LengthWrap::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error) || limits.maxSteps <= 0)
    {
        _fail(error, QStringLiteral("Processing node limits must be positive"));
        return {};
    }

    auto configuration = input;
    if (!_boolOption(configuration, "includeField", false, error) || !LengthField::validate(configuration, error))
    {
        return {};
    }
    if (configuration.value("encoding") == "fixed"
        && (!_stringOption(configuration, "position", "prefix", {"prefix", "suffix"}, error)
            || !_stringOption(configuration, "endianness", "little", {"little", "big"}, error)))
    {
        return {};
    }

    return std::unique_ptr<Processor>(new LengthWrap(std::move(configuration), limits));
}

void LengthWrap::_process(const Chunk& input, QByteArray& output, QString& error)
{
    quint64 value = 0;
    int     width = 0;
    if (!_field.value(input.bytes.size(), value, width, error))
    {
        return;
    }
    if (_maxSteps < width)
    {
        _fail(&error, QStringLiteral("Length field processing exceeds step limit"));
        return;
    }
    if (input.bytes.size() > _maximumBytes() - width)
    {
        _fail(&error, QStringLiteral("Length-wrapped packet exceeds byte limit"));
        return;
    }

    const auto field = _field.encode(value);
    output.reserve(input.bytes.size() + width);
    output.append(_field.prefix() ? field : input.bytes);
    output.append(_field.prefix() ? input.bytes : field);
}

void LengthWrap::_reset()
{
}
