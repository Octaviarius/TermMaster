#include "Compression.h"

#include "Compress.h"
#include "Uncompress.h"

namespace
{
QByteArray envelope(const QByteArray& properties)
{
    return "version: 1\ntype: termaster.settings-schema\ndescription: Compression processor settings\ncontent:\n  type: object\n  x-groups:\n    - {id: format, title: Format, order: -1}\n  properties:\n"
         + properties;
}
} // namespace

Codec::Definition Compression::descriptor()
{
    return {
        envelope(
            R"(    format: {type: string, title: Format, description: 'Shared DEFLATE wire wrapper; raw requires explicit selection.', enum: [zlib, gzip, raw], default: zlib, x-group: format}
    windowBits: {type: integer, title: Window bits, description: Base-two logarithm of the DEFLATE window size., minimum: 9, maximum: 15, default: 15, x-group: format}
)"),
        R"(level: {type: integer, title: Compression level, description: '-1 selects the zlib default; 0 stores bytes without compression.', minimum: -1, maximum: 9, default: -1}
memLevel: {type: integer, title: Memory level, description: 'Memory/speed trade-off: 1 uses the least compression memory, 9 the most.', minimum: 1, maximum: 9, default: 8}
strategy: {type: string, title: Strategy, enum: [default, filtered, huffman, rle, fixed], default: default}
)",
        R"(autoDetect: {type: boolean, title: Auto-detect wrapper, description: 'Accept either zlib or gzip when Format is zlib or gzip; raw always uses explicit raw DEFLATE.', default: true}
maxOutputBytes: {type: integer, title: Maximum output bytes, description: 'Per-packet output limit, also bounded by the host byte limit.', minimum: 1, maximum: 67108864, default: 1048576}
concatenated: {type: boolean, title: Accept concatenated streams, description: 'Decode every complete member into one packet. Trailing junk is always rejected.', default: false}
)",
        Compress::create,
        Uncompress::create,
        "Compressed",
        "Bytes",
        "compress",
        "uncompress"};
}
