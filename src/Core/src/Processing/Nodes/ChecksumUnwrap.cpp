#include "ChecksumUnwrap.h"

#include "Checksum.h"

ChecksumUnwrap::ChecksumUnwrap(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _field(configuration), _maxSteps(limits.maxSteps)
{
}

QByteArray ChecksumUnwrap::schema()
{
    return Checksum::descriptor().schema(Codec::Mode::Inverse);
}

std::unique_ptr<Processor> ChecksumUnwrap::create(const QVariantMap& input, const Limits& limits, QString* error)
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

    return std::unique_ptr<Processor>(new ChecksumUnwrap(std::move(configuration), limits));
}

void ChecksumUnwrap::_process(const Chunk& input, QByteArray& output, QString& error)
{
    const auto width = _field.width();
    if (input.bytes.size() < width)
    {
        _fail(&error, QStringLiteral("Packet is shorter than its checksum field"));
        return;
    }
    if (input.bytes.size() > _maxSteps)
    {
        _fail(&error, QStringLiteral("Checksum processing exceeds step limit"));
        return;
    }

    const auto payloadBytes  = input.bytes.size() - width;
    const auto payloadOffset = _field.prefix() ? width : 0;
    const auto fieldOffset   = _field.prefix() ? 0 : payloadBytes;
    const auto payload       = QByteArrayView(input.bytes.constData() + payloadOffset, payloadBytes);
    const auto actual        = QByteArrayView(input.bytes.constData() + fieldOffset, width);
    if (actual != _field.encode(payload))
    {
        _fail(&error, QStringLiteral("Packet checksum mismatch"));
        return;
    }

    output = QByteArray(payload.data(), payload.size());
}

void ChecksumUnwrap::_reset()
{
}
