#include "Stuffing.h"

#include "StuffingUnwrap.h"
#include "StuffingWrap.h"

Codec::Definition Stuffing::descriptor()
{
    return {R"SCHEMA(version: 1
type: termaster.settings-schema
description: Whole-packet fixed-pair byte stuffing settings
content:
  type: object
  x-groups:
    - {id: stuffing, title: Byte stuffing, order: 0}
  properties:
    boundaryMode:
      type: string
      title: Boundary mode
      description: Suffix adds/requires a trailing delimiter; both also adds/requires a leading delimiter. Use none when a Framer has already removed the delimiter.
      enum: [suffix, both, none]
      default: suffix
      x-group: stuffing
      x-order: 0
    delimiter:
      type: integer
      title: Delimiter
      description: Reserved boundary byte, in hexadecimal; must differ from escape and both replacement codes.
      minimum: 0
      maximum: 255
      default: 192
      x-widget: hexInteger
      x-group: stuffing
      x-order: 1
    escape:
      type: integer
      title: Escape
      description: Introduces exactly one replacement code, in hexadecimal.
      minimum: 0
      maximum: 255
      default: 219
      x-widget: hexInteger
      x-group: stuffing
      x-order: 2
    delimiterCode:
      type: integer
      title: Delimiter replacement
      description: Code following escape for a payload delimiter, in hexadecimal; must differ from the escape replacement code.
      minimum: 0
      maximum: 255
      default: 220
      x-widget: hexInteger
      x-group: stuffing
      x-order: 3
    escapeCode:
      type: integer
      title: Escape replacement
      description: Code following escape for a payload escape byte, in hexadecimal; may equal escape.
      minimum: 0
      maximum: 255
      default: 221
      x-widget: hexInteger
      x-group: stuffing
      x-order: 4
)SCHEMA",
            {},
            R"(framing:
  type: boolean
  title: Framing
  description: Buffer stream chunks until each delimiter and decode complete frames; cancellation discards an incomplete frame. None and suffix consume the delimiter; both requires paired opening and closing delimiters.
  default: true
  x-group: stuffing
  x-order: 5
)",
            StuffingWrap::create,
            StuffingUnwrap::create,
            "Packet",
            "Payload",
            "wrap",
            "unwrap"};
}
