#include "HexCodec.h"

#include "HexDecoder.h"
#include "HexEncoder.h"

HexCodec::HexCodec(QVariantMap configuration, std::unique_ptr<Processor> decoder, std::unique_ptr<Processor> encoder) :
    DuplexCodecNode(std::move(configuration), QStringLiteral("HEX"), std::move(decoder), std::move(encoder))
{
}

QByteArray HexCodec::schema()
{
    return QByteArray(R"(version: 1
type: termaster.settings-schema
description: Bidirectional HEX codec settings
content:
  type: object
  x-groups:
    - {id: decode, title: Decode}
    - {id: encode, title: Encode}
  properties:
    decodeIgnoreWhitespace:
      type: boolean
      title: Ignore ASCII whitespace
      default: true
      x-group: decode
    decodeAccumulate:
      type: boolean
      title: Accumulate incomplete HEX byte
      default: false
      x-group: decode
    encodeLetterCase:
      type: string
      title: Letter case
      enum: [upper, lower]
      default: upper
      x-group: encode
    encodeSeparator:
      type: string
      title: Byte separator
      maxLength: 16
      default: ' '
      x-group: encode
)");
}

std::unique_ptr<Processor> HexCodec::create(const QVariantMap& configuration, const Limits& limits, QString* error)
{
    auto decoder = HexDecoder::create(_directionConfiguration(configuration, "decode"), limits, error);
    if (!decoder)
    {
        return {};
    }

    auto encoder = HexEncoder::create(_directionConfiguration(configuration, "encode"), limits, error);
    if (!encoder)
    {
        return {};
    }

    return std::unique_ptr<Processor>(new HexCodec(configuration, std::move(decoder), std::move(encoder)));
}
