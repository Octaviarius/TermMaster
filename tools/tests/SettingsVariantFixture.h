#pragma once
#include <QByteArray>

/** Synthetic descriptor shared by schema, generated form and actual Lineflow editor checks. */
inline QByteArray settingsVariantFixture()
{
    return R"(version: 1
type: termaster.settings-schema
description: Synthetic variant form acceptance
content:
  type: object
  required: [label]
  x-groups: [{id: format, title: Packet format, order: 0}]
  definitions:
    width: {type: integer, title: Width, minimum: 1, maximum: 8, default: 2, x-group: format, x-order: 1}
  properties:
    label: {type: string, title: Label, default: Frame, x-order: 1}
  oneOf:
    - title: Fixed width
      type: object
      additionalProperties: false
      required: [encoding, width]
      properties:
        label: {type: string, title: Label, default: Frame, x-order: 1}
        encoding: {const: fixed, title: Encoding, x-group: format, x-order: 0}
        width: {$ref: '#/definitions/width'}
        options: {type: object, title: Options, default: {}, x-widget: yaml, x-group: format, x-order: 2}
    - title: Variable width
      type: object
      additionalProperties: false
      required: [encoding, maxBytes]
      properties:
        label: {type: string, title: Label, default: Frame, x-order: 1}
        encoding: {const: varint}
        maxBytes: {type: integer, title: Maximum bytes, minimum: 1, maximum: 10, default: 5, x-group: format, x-order: 1}
)";
}
