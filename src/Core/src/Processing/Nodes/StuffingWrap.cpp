#include "StuffingWrap.h"

#include <limits>

StuffingWrap::StuffingWrap(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _settings(configuration), _maxSteps(limits.maxSteps)
{
}

QByteArray StuffingWrap::schema()
{
    return StuffingSettings::schema();
}

std::unique_ptr<Processor> StuffingWrap::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error) || limits.maxSteps <= 0)
    {
        _fail(error, QStringLiteral("Processing node limits must be positive"));
        return {};
    }

    auto configuration = input;
    if (!StuffingSettings::validate(configuration, error))
    {
        return {};
    }

    return std::unique_ptr<Processor>(new StuffingWrap(std::move(configuration), limits));
}

void StuffingWrap::_process(const Chunk& input, QByteArray& output, QString& error)
{
    const auto boundaries = _settings.boundaries();
    // Bound the worst possible 2*n + boundaries before sizing any output allocation.
    if (input.bytes.size() > (std::numeric_limits<qsizetype>::max() - boundaries) / 2)
    {
        _fail(&error, QStringLiteral("Stuffing-wrapped packet size overflows"));
        return;
    }
    if (input.bytes.size() > _maxSteps || boundaries > _maximumBytes())
    {
        _fail(&error, QStringLiteral("Stuffing processing exceeds runtime limits"));
        return;
    }

    qsizetype outputBytes = boundaries;
    for (const unsigned char byte : input.bytes)
    {
        const auto count = byte == _settings.delimiter() || byte == _settings.escape() ? 2 : 1;
        if (count > _maximumBytes() - outputBytes)
        {
            _fail(&error, QStringLiteral("Stuffing-wrapped packet exceeds byte limit"));
            return;
        }
        outputBytes += count;
    }
    if (outputBytes > _maxSteps - input.bytes.size())
    {
        _fail(&error, QStringLiteral("Stuffing processing exceeds step limit"));
        return;
    }

    output.reserve(outputBytes);
    if (boundaries == 2)
    {
        output.append(char(_settings.delimiter()));
    }
    for (const unsigned char byte : input.bytes)
    {
        if (byte == _settings.delimiter() || byte == _settings.escape())
        {
            output.append(char(_settings.escape()));
            output.append(char(byte == _settings.delimiter() ? _settings.delimiterCode() : _settings.escapeCode()));
        }
        else
        {
            output.append(char(byte));
        }
    }
    if (boundaries > 0)
    {
        output.append(char(_settings.delimiter()));
    }
}

void StuffingWrap::_reset()
{
}
