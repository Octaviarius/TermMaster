#include "Checksum.h"

#include "ChecksumUnwrap.h"
#include "ChecksumWrap.h"

Codec::Definition Checksum::descriptor()
{
    return {R"SCHEMA(version: 1
type: termaster.settings-schema
description: Whole-packet payload checksum settings
content:
  type: object
  properties:
    algorithm:
      type: string
      title: Algorithm
      default: crc16-modbus
      x-group: checksum
      x-order: 0
    position:
      type: string
      title: Position
      description: Place the checksum before or after the payload; only payload bytes enter the checksum.
      enum: [suffix, prefix]
      default: suffix
      x-group: checksum
      x-order: 1
  oneOf:
    - title: Sum 8
      required: [algorithm]
      properties:
        algorithm: {type: string, const: sum8}
    - title: XOR 8
      required: [algorithm]
      properties:
        algorithm: {type: string, const: xor8}
    - title: CRC-8/SMBUS
      required: [algorithm]
      properties:
        algorithm: {type: string, const: crc8-smbus}
    - title: CRC-16/MODBUS
      required: [algorithm]
      properties:
        algorithm: {type: string, const: crc16-modbus}
        endianness:
          type: string
          title: Endianness
          description: Byte order of the checksum field; irrelevant for an 8-bit custom CRC.
          enum: [little, big]
          default: little
          x-group: checksum
          x-order: 2
    - title: CRC-16/IBM-3740
      required: [algorithm]
      properties:
        algorithm: {type: string, const: crc16-ibm3740}
        endianness:
          type: string
          title: Endianness
          description: Byte order of the checksum field; irrelevant for an 8-bit custom CRC.
          enum: [little, big]
          default: little
          x-group: checksum
          x-order: 2
    - title: CRC-32/ISO-HDLC
      required: [algorithm]
      properties:
        algorithm: {type: string, const: crc32-iso-hdlc}
        endianness:
          type: string
          title: Endianness
          description: Byte order of the checksum field; irrelevant for an 8-bit custom CRC.
          enum: [little, big]
          default: little
          x-group: checksum
          x-order: 2
    - title: CRC-32/ISCSI
      required: [algorithm]
      properties:
        algorithm: {type: string, const: crc32-iscsi}
        endianness:
          type: string
          title: Endianness
          description: Byte order of the checksum field; irrelevant for an 8-bit custom CRC.
          enum: [little, big]
          default: little
          x-group: checksum
          x-order: 2
    - title: Custom CRC
      required: [algorithm]
      properties:
        algorithm: {type: string, const: crc-custom}
        endianness:
          type: string
          title: Endianness
          description: Byte order of the checksum field; irrelevant for an 8-bit custom CRC.
          enum: [little, big]
          default: little
          x-group: checksum
          x-order: 2
        width:
          type: integer
          title: CRC width (bits)
          enum: [8, 16, 32]
          default: 16
          x-group: custom
          x-order: 0
        polynomial:
          type: integer
          title: Polynomial
          description: Normal unreflected polynomial without its top bit, in hexadecimal. Nonzero; must fit CRC width.
          minimum: 1
          maximum: 4294967295
          default: 32773
          x-widget: hexInteger
          x-group: custom
          x-order: 1
        init:
          type: integer
          title: Initial register
          description: Initial normal register value, in hexadecimal; must fit CRC width.
          minimum: 0
          maximum: 4294967295
          default: 65535
          x-widget: hexInteger
          x-group: custom
          x-order: 2
        refin:
          type: boolean
          title: Reflect input
          default: true
          x-group: custom
          x-order: 3
        refout:
          type: boolean
          title: Reflect output
          default: true
          x-group: custom
          x-order: 4
        xorout:
          type: integer
          title: Final XOR
          description: XOR applied after output reflection, in hexadecimal; must fit CRC width.
          minimum: 0
          maximum: 4294967295
          default: 0
          x-widget: hexInteger
          x-group: custom
          x-order: 5
      allOf:
        - if:
            required: [width]
            properties:
              width: {const: 8}
          then:
            properties:
              polynomial: {maximum: 255}
              init: {maximum: 255}
              xorout: {maximum: 255}
        - if:
            required: [width]
            properties:
              width: {const: 16}
          then:
            properties:
              polynomial: {maximum: 65535}
              init: {maximum: 65535}
              xorout: {maximum: 65535}
  x-groups:
    - id: checksum
      title: Checksum
      order: 0
    - id: custom
      title: Custom CRC
      order: 1
)SCHEMA",
            {},
            {},
            ChecksumWrap::create,
            ChecksumUnwrap::create,
            "Packet",
            "Payload",
            "wrap",
            "unwrap"};
}
