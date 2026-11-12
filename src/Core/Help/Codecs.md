# Codecs and packet envelopes

HEX and Base64 operate on complete packets. HEX Decoder can explicitly retain a partial pair when its buffering option is enabled; otherwise incomplete/malformed input is an error.

Each family provides a direct action, inverse action and duplex Codec. Directions retain independent state. Shared wire settings appear once; only relevant direction controls are created.

Length Wrap/Unwrap adds/checks a fixed-width or varint length. Checksum Wrap/Unwrap adds/verifies a checksum; CRC polynomial, initial register and final XOR display in HEX. Stuffing Wrap/Unwrap escapes configurable delimiter/escape bytes and replacement codes. Unwrap framing defaults on.

Compress/Uncompress acts on packets. Choose algorithms and parameters matching the peer protocol. Processing faults are reported without disconnecting the transport.
