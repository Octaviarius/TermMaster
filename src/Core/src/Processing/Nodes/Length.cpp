#include "Length.h"

#include "LengthUnwrap.h"
#include "LengthWrap.h"

Codec::Definition Length::descriptor()
{
    return {R"(version: 1
type: termaster.settings-schema
description: Whole-packet binary length field settings
content:
  type: object
  properties:
    encoding:
      type: string
      title: Encoding
      default: fixed
      x-group: length
      x-order: 0
    includeField:
      type: boolean
      title: Include field length
      description: Count the length field itself in addition to payload bytes.
      default: false
      x-group: length
      x-order: 4
    adjustment:
      type: integer
      title: Length adjustment
      description: Signed value added after counting payload and optional field bytes.
      minimum: -2147483647
      maximum: 2147483647
      default: 0
      x-group: length
      x-order: 5
  oneOf:
    - title: Fixed
      required: [encoding]
      properties:
        encoding: {type: string, const: fixed}
        position:
          type: string
          title: Position
          description: Place the length field before or after the payload.
          enum: [prefix, suffix]
          default: prefix
          x-group: length
          x-order: 1
        width:
          type: integer
          title: Field width (bytes)
          description: Unsigned binary length field size.
          minimum: 1
          maximum: 8
          default: 2
          x-group: length
          x-order: 2
        endianness:
          type: string
          title: Endianness
          description: Byte order of the unsigned length field.
          enum: [little, big]
          default: little
          x-group: length
          x-order: 3
    - title: Varint
      required: [encoding]
      properties:
        encoding: {type: string, const: varint}
        maxBytes:
          type: integer
          title: Maximum bytes
          description: Maximum canonical unsigned ULEB128 prefix width (uint64, Protobuf encoding).
          minimum: 1
          maximum: 10
          default: 10
          x-group: length
          x-order: 1
  x-groups:
    - id: length
      title: Length field
)",
            {},
            {},
            LengthWrap::create,
            LengthUnwrap::create,
            "Packet",
            "Payload",
            "wrap",
            "unwrap"};
}
