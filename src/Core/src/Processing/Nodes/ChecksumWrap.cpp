#include "ChecksumWrap.h"

#include "Checksum.h"

ChecksumWrap::ChecksumWrap(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _field(configuration), _maxSteps(limits.maxSteps)
{
}

QByteArray ChecksumWrap::schema()
{
    return Checksum::descriptor().schema(Codec::Mode::Direct);
}

std::unique_ptr<Processor> ChecksumWrap::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error) || limits.maxSteps <= 0)
    {
        _fail(error, QStringLiteral("Processing node limits must be positive"));
        return {};
    }

    auto configuration = input;
    if (!ChecksumField::validate(configuration, error))
    {
        return {};
    }

    return std::unique_ptr<Processor>(new ChecksumWrap(std::move(configuration), limits));
}

void ChecksumWrap::_process(const Chunk& input, QByteArray& output, QString& error)
{
    const auto width = _field.width();
    if (input.bytes.size() > _maximumBytes() - width)
    {
        _fail(&error, QStringLiteral("Checksum-wrapped packet exceeds byte limit"));
        return;
    }
    if (input.bytes.size() > _maxSteps - width)
    {
        _fail(&error, QStringLiteral("Checksum processing exceeds step limit"));
        return;
    }

    const auto field = _field.encode(input.bytes);
    output.reserve(input.bytes.size() + width);
    output.append(_field.prefix() ? field : input.bytes);
    output.append(_field.prefix() ? input.bytes : field);
}

void ChecksumWrap::_reset()
{
}
