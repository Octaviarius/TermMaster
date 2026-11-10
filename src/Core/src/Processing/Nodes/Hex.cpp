#include "Hex.h"

#include "HexDecoder.h"
#include "HexEncoder.h"

namespace
{
QByteArray envelope(const QByteArray& properties)
{
    return "version: 1\ntype: termaster.settings-schema\ndescription: Hex processor settings\ncontent:\n  type: object\n  properties:\n"
         + properties;
}
} // namespace

Codec::Definition Hex::descriptor()
{
    return {envelope({}),
            R"(letterCase: {type: string, title: Letter case, enum: [upper, lower], default: upper}
separator: {type: string, title: Byte separator, maxLength: 16, default: ' ', description: 'Literal UTF-8 separator, at most 16 characters.'}
)",
            R"(ignoreWhitespace: {type: boolean, title: Ignore ASCII whitespace, default: true}
accumulate: {type: boolean, title: Accumulate incomplete HEX byte, default: false}
)",
            HexEncoder::create,
            HexDecoder::create,
            "HEX",
            "Bytes"};
}
