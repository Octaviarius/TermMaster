#include "LengthUnwrap.h"

#include "Length.h"

LengthUnwrap::LengthUnwrap(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _field(configuration), _maxSteps(limits.maxSteps)
{
}

QByteArray LengthUnwrap::schema()
{
    return Length::descriptor().schema(Codec::Mode::Inverse);
}

std::unique_ptr<Processor> LengthUnwrap::create(const QVariantMap& input, const Limits& limits, QString* error)
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

    return std::unique_ptr<Processor>(new LengthUnwrap(std::move(configuration), limits));
}

void LengthUnwrap::_process(const Chunk& input, QByteArray& output, QString& error)
{
    quint64 actual = 0;
    int     width  = 0;
    if (!_field.decode(input.bytes, actual, width, _maxSteps, error))
    {
        return;
    }

    const auto payloadBytes  = input.bytes.size() - width;
    quint64    expected      = 0;
    int        expectedWidth = 0;
    if (!_field.value(payloadBytes, expected, expectedWidth, error))
    {
        return;
    }
    if (actual != expected || width != expectedWidth)
    {
        _fail(&error, QStringLiteral("Packet length mismatch: field is %1, expected %2").arg(actual).arg(expected));
        return;
    }

    output = QByteArray(input.bytes.constData() + (_field.prefix() ? width : 0), payloadBytes);
}

void LengthUnwrap::_reset()
{
}
