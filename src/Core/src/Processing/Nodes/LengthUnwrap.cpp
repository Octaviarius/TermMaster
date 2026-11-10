#include "LengthUnwrap.h"

LengthUnwrap::LengthUnwrap(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _field(configuration), _maxSteps(limits.maxSteps)
{
}

QByteArray LengthUnwrap::schema()
{
    return LengthField::schema();
}

std::unique_ptr<Processor> LengthUnwrap::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error) || limits.maxSteps <= 0)
    {
        _fail(error, QStringLiteral("Processing node limits must be positive"));
        return {};
    }

    auto configuration = input;
    if (!_stringOption(configuration, "position", "prefix", {"prefix", "suffix"}, error)
        || !_stringOption(configuration, "endianness", "little", {"little", "big"}, error)
        || !_boolOption(configuration, "includeField", false, error) || !LengthField::validate(configuration, error))
    {
        return {};
    }

    return std::unique_ptr<Processor>(new LengthUnwrap(std::move(configuration), limits));
}

void LengthUnwrap::_process(const Chunk& input, QByteArray& output, QString& error)
{
    if (_maxSteps < _field.width())
    {
        _fail(&error, QStringLiteral("Length field processing exceeds step limit"));
        return;
    }
    if (input.bytes.size() < _field.width())
    {
        _fail(&error, QStringLiteral("Packet is shorter than its length field"));
        return;
    }

    const auto payloadBytes = input.bytes.size() - _field.width();
    quint64    expected     = 0;
    if (!_field.value(payloadBytes, expected, error))
    {
        return;
    }
    const auto offset = _field.prefix() ? 0 : payloadBytes;
    const auto actual = _field.decode(input.bytes.constData() + offset);
    if (actual != expected)
    {
        _fail(&error, QStringLiteral("Packet length mismatch: field is %1, expected %2").arg(actual).arg(expected));
        return;
    }

    output = QByteArray(input.bytes.constData() + (_field.prefix() ? _field.width() : 0), payloadBytes);
}

void LengthUnwrap::_reset()
{
}
