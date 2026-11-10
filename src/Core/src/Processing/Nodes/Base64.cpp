#include "Base64.h"

#include "Base64Decoder.h"
#include "Base64Encoder.h"

namespace
{
QByteArray envelope(const QByteArray& properties)
{
    return "version: 1\ntype: termaster.settings-schema\ndescription: Base64 processor settings\ncontent:\n  type: object\n  x-groups:\n    - {id: format, title: Format, order: -1}\n  properties:\n"
         + properties;
}
} // namespace

Codec::Definition Base64::descriptor()
{
    return {
        envelope(
            "    alphabet: {type: string, title: Alphabet, enum: [standard, url], default: standard, x-group: format}\n"),
        R"(padding: {type: boolean, title: Emit padding, default: true}
lineLength: {type: integer, title: Line length, minimum: 0, maximum: 4096, multipleOf: 4, default: 0}
lineEnding: {type: string, title: Line ending, enum: [lf, crlf], default: lf}
)",
        R"(ignoreWhitespace: {type: boolean, title: Ignore ASCII whitespace, default: true}
requirePadding: {type: boolean, title: Require padding, default: true}
)",
        Base64Encoder::create,
        Base64Decoder::create,
        "Base64",
        "Bytes"};
}
