#include "HexEncoder.h"

#include "Hex.h"

HexEncoder::HexEncoder(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _separator(configuration.value("separator").toString().toUtf8()),
    _lower(configuration.value("letterCase").toString() == "lower")
{
}
QByteArray HexEncoder::schema()
{
    return Hex::descriptor().schema(Codec::Mode::Direct);
}

std::unique_ptr<Processor> HexEncoder::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error))
    {
        return {};
    }
    auto configuration = input;
    if (!_stringOption(configuration, "letterCase", "upper", {"upper", "lower"}, error))
    {
        return {};
    }
    if (!configuration.contains("separator"))
    {
        configuration.insert("separator", QStringLiteral(" "));
    }
    const auto value = configuration.value("separator");
    if (value.typeId() != QMetaType::QString || value.toString().size() > 32 || value.toString().toUcs4().size() > 16
        || value.toString().toUtf8().size() > 64)
    {
        _fail(error, QStringLiteral("HEX separator must contain at most 16 Unicode characters"));
        return {};
    }
    return std::unique_ptr<Processor>(new HexEncoder(std::move(configuration), limits));
}
void HexEncoder::_process(const Chunk& input, QByteArray& output, QString& error)
{
    const char* digits = _lower ? "0123456789abcdef" : "0123456789ABCDEF";
    for (unsigned char byte : input.bytes)
    {
        if ((_haveByte && !_append(output, _separator, error)) || !_append(output, digits[byte >> 4], error)
            || !_append(output, digits[byte & 15], error))
        {
            return;
        }
        _haveByte = true;
    }
}
void HexEncoder::_reset()
{
    _haveByte = false;
}
