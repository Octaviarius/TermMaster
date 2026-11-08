# Settings forms

Settings forms are generated from bounded YAML descriptors. Core owns the immutable
schema, draft model, editor registry and form widget. App owns the application dialog
and persistence callback. This keeps schema validation and presentation reusable
without making a generic widget perform file I/O.

## Descriptor contract

A descriptor is a YAML document with the standard versioned envelope. The only
supported schema descriptor type is `termaster.settings-schema`; its description
is required and `content` contains a JSON Schema Draft 7 object schema.

```yaml
version: 1
type: termaster.settings-schema
description: Serial port preferences
content:
  $schema: http://json-schema.org/draft-07/schema#
  title: Serial port
  type: object
  additionalProperties: false
  required: [baudRate]
  x-groups:
    - id: connection
      title: Connection
      order: 0
  properties:
    baudRate:
      type: integer
      minimum: 300
      maximum: 4000000
      enum: [9600, 19200, 115200]
      default: 115200
      title: Baud rate
      description: Serial line rate to use when a serial backend is configured.
      x-settings-path: transport/serial/baudRate
      x-group: connection
      x-order: 0
      x-widget: enumCombo
      x-widget-options:
        compact: true
```

Each root `properties` member is one editable field. Its property key is a literal
field identifier and may contain `/`; it is not expanded as a nested form path.
`x-settings-path` binds the field to a key relative to the `Settings::Node` or
`SettingsOverlay` passed to the model. If omitted, the property key is the settings
key and must itself meet the settings path rules. Field paths must be unique and
must not overlap as parent and child paths. Draft 7 `required`, `default`, `readOnly`,
`title`, `description`, and validation keywords apply to the field. Defaults seed
the draft and are written only when Apply is used.

The root and field validation uses Draft 7 with strong QVariant types. Schema
references may be local JSON Pointers only; unresolved or cyclic references are
rejected. External references are unsupported. YAML aliases, duplicate mapping
keys, non-scalar mapping keys, multiple documents and unsupported explicit scalar
tags are rejected. YAML values are converted through a Qt QVariant adapter because
the upstream YAML adapter does not preserve the required strong scalar types.
Valijson is pinned under `components/valijson` at
`af2383509277f8e8051acbb3e4b3060f4779aa10` as a Git submodule. Initialize dependencies with
`git submodule update --init`; configuration never downloads dependencies.

Parsing and serialization are bounded to 4 MiB, depth 64 and 100000 nodes. YAML
aliases and non-finite numbers are rejected. JSON Schema expansion is also bounded
to depth 64 and 100000 nodes. JSON Schema numeric validation uses Valijson's
`BasicAdapter` and may round integers through `double`; therefore schema numeric
constants and form values that are integers, including integral-valued floating
point values, must be within the inclusive exact-double range
`-9007199254740991` through `9007199254740991`. YAML parsing itself preserves
supported 64-bit integers, but this does not mean all such integers can be
validated against a schema. These limits apply to descriptor and form YAML values.

## Presentation annotations and editors

Root `x-groups` is an ordered sequence of `{id, title, order}` mappings. A field's
`x-group` selects a group, `x-order` orders the field within that group, and
`x-widget` selects an editor hint. `x-widget-options` is a mapping forwarded in
the field's presentation data for a custom editor. Missing groups are created using
their IDs as titles. Built-in hints include `boolCheckBox`, `stringLineEdit`,
`enumCombo`, `integer`, `number`, `password`, `multiline`, and `yaml`; field types
and `enum` can also select standard controls automatically. These flat annotations
are presentation hints; JSON Schema remains authoritative for validation. The old
nested `x-ui` annotations are rejected and are not aliases for the flat names.

Object and array fields use a YAML value editor. This is also available as the
`yaml` hint. The form does not recursively lay out nested `properties`; complex
structures use the YAML editor or a custom field editor. Register custom editor
factories explicitly in `SettingsWidgetRegistry` and use their `x-widget` name.
A factory must remain loaded while any editor it created is alive. Unregistering a
factory with live editors fails, so a plugin owner must close forms before unloading
its editor implementation.

## Use from a component

```cpp
auto schema = std::make_shared<SettingsSchema>(descriptorYaml);
auto* model = new SettingsFormModel(schema, settings.node("terminals/primary"), dialog);
auto* form = new SettingsFormWidget(*model, registry, dialog);
```

For the descriptor above, the baud-rate field resolves to
`terminals/primary/transport/serial/baudRate`. The caller places the form in its
layout, invokes `model->apply(&error)` on Apply and asks the settings owner to
persist when appropriate. The same API accepts a live `SettingsOverlay`.

```cpp
registry.registerWidget("serialPort", [](const SettingsSchema::Field& field, QWidget* parent) {
    return new SerialPortEditor(field.presentation, parent);
});
```

`SerialPortEditor` implements `SettingsFieldEditor`: `value()`, `setValue()` and
`valueChanged()`, with optional local-input diagnostics. It owns its view/model
using normal Qt parent ownership. A descriptor selects it with
`x-widget: serialPort`; `x-widget-options` reaches `field.presentation`. The
serial selector in automated GUI checks uses synthetic entries and icons only.

## Draft, apply and persistence

`SettingsFormModel` reads from its borrowed live scope and keeps original and draft
values separately. Editing and validation do not write settings. Apply validates
the complete draft, checks that edited keys have not changed since the draft was
loaded, then applies one patch to the highest active scope. Removal reveals an
inherited value in an overlay. The settings owner prepares and atomically applies
the patch while retaining unknown data and concurrent unrelated changes. Reload is
explicit and discards a draft.

App supplies separate pages for application settings and available workspace/
session configuration overlays, together with a persistence callback. The dialog provides Apply, OK
(apply and close), and Cancel. A scope change closes the dialog to avoid applying
a draft against a different workspace or session. Generic Core widgets do not open
files or persist documents; App chooses the owning settings file and performs the
save after a successful model patch.

The sample application descriptor controls toolbar and status-bar visibility in
the active scope. The serial example above documents a schema capability only; it
does not indicate that a serial terminal backend is implemented.

## Preferences navigation and preview pages

This is the primary settings layout for Termaster. The two-level navigation,
right-hand page area and shared search form the established layout for future
settings pages. The captures under `docs/images/preferences/` are the visual
reference. Preview pages are connected to real owners as their features are
implemented; their preview designation remains until that integration is complete.

Tools/Settings presents a two-level `QTreeView`: category and page. Page registration
uses a unique ID, category, title, description, schema and borrowed settings scope.
The right-hand stack contains the generated forms. Category/page navigation never
applies or discards drafts. Application, Workspace and Session Appearance pages
bind to real owners, with workspace/session pages available for active scopes.

The search box filters categories, pages, groups and fields, using case-insensitive
substring matching against titles, descriptions, field identifiers and settings
paths. Matching a category/page reveals its whole form; matching a group reveals
all its parameters. Other queries hide unmatched rows and empty groups. Clearing
the query restores the form without losing edits or changing validation. Applying
an invalid hidden draft clears the query and selects the affected page.

Additional MVP pages demonstrate application behavior, workspace directories,
session layout, terminal appearance and COM-port parameters. These pages are
explicitly marked Preview. Their defaults and edits belong to a dialog-owned cache;
preview-only Apply does not call application persistence. The serial selector uses
a custom field editor, a `QStandardItemModel`, example device labels/icons and an
example-list refresh button. No devices are enumerated, opened or configured.
Preview changes disappear when the dialog closes. The sample pages do not establish
implemented startup, auto-save, layout, font, encoding or serial-backend behavior.

Apply validates every dirty page, including filtered-out pages, before updating
owners. Dependent scopes are registered base to derived and applied in reverse
order so a base change from this dialog does not create a false inherited-value
conflict. Clean page models reload after success to reflect new inherited values.
Each model patch is atomic; application of several owners and their file writes
is not a single transaction. Persistence failures keep the dialog open for retry.

The preview screenshots use one native window size: 1020 by 720 pixels at scale 1.

## Verification status

`TERMASTER_BUILD_SCHEMA_TESTS` enables `SettingsSchemaTests` and
`SettingsFormTests`. The schema suite covers typed YAML, schema validation,
references and bounded/rejected input. The form suite covers defaults, deferred
writes, atomic apply, scope overlays, external-change guards and custom editor
lifecycle.

Verified on 2026-11-08 with Windows x64, MSVC 19.44, Qt 6.12.0 and Ninja Debug.
The application and affected test targets built; all eleven CTest suites passed,
including the independent external plugin SDK consumer. SettingsSchemaTests
passed 17 results and SettingsFormTests passed ten. AppTests passed eleven results,
including generated dialog cancellation, scoped apply/persistence and automatic
closure when the active settings scope changes. Atomic patch checks include
rollback, unknown metadata, aliases, expired owners and overlapping paths.

Native AppTests and SettingsFormTests passed without skips at scale factors 1
and 2. Application preferences and the custom synthetic port selector were
visually inspected: labels, controls, Unicode and model-provided icons rendered
correctly. The optional action-container capture and three existing workspace/
session symbolic-link cases remain skipped by regular CTest; this Windows user
cannot create the required symbolic links. No devices were scanned or opened,
no external connections were made, and no dependencies were downloaded.
Release was not rebuilt for this feature.

### Preferences MVP acceptance

Verified on 2026-11-08 with Windows x64, MSVC 19.44, Qt 6.12.0 and Ninja Debug.
The application and affected tests built. All eleven CTest suites passed,
including the independent plugin SDK consumer. AppTests passed twelve results;
SettingsFormTests passed eleven. Checks cover exactly two navigation levels,
page/parameter/group search, empty results, clearing a filter without losing
invalid drafts, isolated preview edits, hidden-page validation and dependent
scope apply/reload behavior. Existing optional action capture and three
symbolic-link cases remain skipped by regular CTest.

Native AppTests passed all twelve results without skips. Four inspected captures
were generated at one size, 1020 by 720 pixels at scale 1:
[Workspace](images/preferences/Workspace.png),
[Session](images/preferences/Session.png),
[COM-port terminal](images/preferences/Serial.png), and
[Search](images/preferences/Search.png).
No serial hardware was enumerated or opened. Release was not rebuilt.

## Inherited terminal settings

Overlay-backed fields are immediately editable, including inherited required values.
Editing establishes a local override. A small reset button to the right removes
only that override and displays the inherited value or descriptor default; its
tooltip shows the fallback. The reset button uses an original curved return-arrow
SVG on an orange vertical rectangle with rounded corners,
visible only for a local override; its space remains reserved to keep fields
aligned when editing or resetting. Inherited and local values use the same text
color. Required values without any fallback cannot be reset.
An explicit equal value remains an override. Validation checks the effective
projection, and concurrent changes to ownership conflict even when effective
values are equal. Defaults remain virtual until explicitly overridden.
Application, workspace, session and selected-terminal pages use this behavior;
see [terminal settings](terminals-and-serial.md).

The terminal scope category is titled `Terminal`. The reset presentation update
was verified with the MSVC Debug build, SettingsFormTests and AppTests. Native
application checks passed at scale factors 1 and 2. Tests cover reset visibility
through edit/apply/reset and equal inherited/local text colors; the updated
1280 by 800 reference is [Serial terminal settings](images/preferences/SerialTerminal.png).

The curved reset symbol is a project-owned SVG bundled by Core. Its rendering is
checked through the static resource link in SettingsFormTests. The updated symbol
passed the affected Debug tests and native application checks at scales 1 and 2;
the reference capture above reflects the rounded-button variant.

## Pending terminal appearance requirements

Status: recorded for later implementation; no new editors or preview behavior
have been implemented for this request.

Terminal appearance forms need a font selection button and foreground/background
color selection buttons, using appropriate Qt pickers instead of requiring
manual entry of font names and color codes. Preserve explicit font-size editing
and the existing settings inheritance, validation and reset behavior.

A terminal preview must show how the selected font, size and colors render text.
It must update from the form's effective draft values, including inherited
values and resets, before Apply. Previewing must not change a live terminal's
configuration, persist settings or open a connection. Reuse the same appearance
editors and preview behavior across settings scopes.

## Pending reset button placement requirement

Status: recorded for later implementation.

Place the reset-to-inherited/default button directly beside its parameter's
editor, with a small consistent gap. Compact controls such as checkboxes must
not push the button to the distant right edge of an expanded form row. Keep
the control and reset affordance visually grouped across all settings scopes.
Preserve the approved orange button, its override-only visibility and stable
alignment when the button is hidden.
