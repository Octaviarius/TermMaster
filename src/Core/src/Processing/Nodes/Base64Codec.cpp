#include "Base64Codec.h"

#include "Base64Decoder.h"
#include "Base64Encoder.h"

Base64Codec::Base64Codec(QVariantMap                configuration,
                         std::unique_ptr<Processor> decoder,
                         std::unique_ptr<Processor> encoder) :
    DuplexCodecNode(std::move(configuration), QStringLiteral("Base64"), std::move(decoder), std::move(encoder))
{
}

QByteArray Base64Codec::schema()
{
    return QByteArray(R"(version: 1
type: termaster.settings-schema
description: Bidirectional Base64 codec settings
content:
  type: object
  x-groups:
    - {id: decode, title: Decode}
    - {id: encode, title: Encode}
  properties:
    decodeAlphabet:
      type: string
      title: Alphabet
      enum: [standard, url]
      default: standard
      x-group: decode
    decodeIgnoreWhitespace:
      type: boolean
      title: Ignore ASCII whitespace
      default: true
      x-group: decode
    decodeRequirePadding:
      type: boolean
      title: Require padding
      default: true
      x-group: decode
    encodeAlphabet:
      type: string
      title: Alphabet
      enum: [standard, url]
      default: standard
      x-group: encode
    encodePadding:
      type: boolean
      title: Emit padding
      default: true
      x-group: encode
    encodeLineLength:
      type: integer
      title: Line length
      minimum: 0
      maximum: 4096
      multipleOf: 4
      default: 0
      x-group: encode
    encodeLineEnding:
      type: string
      title: Line ending
      enum: [lf, crlf]
      default: lf
      x-group: encode
)");
}

std::unique_ptr<Processor> Base64Codec::create(const QVariantMap& configuration, const Limits& limits, QString* error)
{
    auto decoder = Base64Decoder::create(_directionConfiguration(configuration, "decode"), limits, error);
    if (!decoder)
    {
        return {};
    }

    auto encoder = Base64Encoder::create(_directionConfiguration(configuration, "encode"), limits, error);
    if (!encoder)
    {
        return {};
    }

    return std::unique_ptr<Processor>(new Base64Codec(configuration, std::move(decoder), std::move(encoder)));
}
