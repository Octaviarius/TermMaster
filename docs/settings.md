# Settings and application paths

## Core ownership

`<Termaster/Core/Settings.h>` is the shared YAML settings owner, implemented in
`src/core/src/Settings.cpp`. It replaces the old QSettings-derived class and the
plugin-specific SettingsManager; no compatibility aliases or migrations remain.
`<Termaster/Core/ApplicationPaths.h>` provides application-owned locations.

Settings owns a cached YAML document. Construction does not read or write files;
`load()` explicitly reads and replaces the cache after validation. `sync()`
atomically persists dirty data through QSaveFile. The destructor never writes.
A successful reload deliberately replaces pending edits; a failed load leaves
the current document and dirty flag unchanged. Failed writes retain pending
changes and do not replace the existing file. Callers check bool results and
optional concrete error strings. Successful operations clear stale errors.

```cpp
ApplicationPaths paths;
Settings settings(paths.configFile("example"));
QString error;
if (!settings.load(&error))
    reportError(error);

auto serial = settings.node("terminal/serial");
if (!serial.setValue("baudRate", 115200, &error))
    reportError(error);
auto baudRate = serial.value("baudRate", 9600).toInt();

if (!settings.sync(&error))
    reportError(error);
```

## Document envelope

Every persisted document requires integer `version: 1`, a matching string `type`,
a string `description` and mapping `content`. Generic Settings defaults to type
`termaster.settings` and description `Termaster settings`; specialized owners
supply `Settings::Info`. The current application uses `termaster.application`,
workspace files use `termaster.workspace`, session files use `termaster.session`,
and plugin preferences use
`termaster.plugins`. Incorrect types, unsupported versions and bare legacy
mappings fail loading. There are no migrations or compatibility aliases.

```yaml
version: 1
type: termaster.settings
description: Termaster settings
content:
  terminal:
    serial:
      baudRate: 115200
```

All value and Node paths are relative to `content`; metadata is outside those
paths. `info()` and `version()` expose the loaded metadata. Unknown envelope and
content fields survive writes.

## Values and scoped views

Keys are slash-delimited relative mapping paths. Empty components, leading or
trailing slashes, `.` and `..` are rejected. A complete key is limited to 64
components before recursive updates. Reads never create values or mark data dirty;
`value()` returns the supplied fallback only when the key is absent or invalid.
Present YAML null is an invalid QVariant, distinguishable through `contains()`.

Supported QVariant values are strings, booleans, signed/unsigned integral values,
finite floating-point values, QVariantMap, QVariantList, QStringList and null.
YAML sequences read as QVariantList; Qt integer storage widths are not encoded.
Quoted strings and explicit string tags remain strings even when they resemble
booleans or numbers. Floating-point values preserve floating type on round trips,
including integral-valued doubles. Integers outside the supported 64-bit ranges
read as strings, avoiding rounding. Unsupported QVariant types and non-finite
QVariant numbers are rejected. This is a documented settings schema, not an
implementation of every YAML type or arbitrary QVariant serialization.

Settings::Node is a copyable, non-owning view into a live document path. Its keys
are relative to that path; `node()` obtains further subviews. An empty node path
means the current/root view. Independent views never change a shared current
prefix. Missing mappings are created lazily by writes. A scalar/list cannot be
silently replaced while traversing a mapping path. Views re-evaluate the path
after reload or deletion, rather than retaining stale YAML nodes. They become
invalid after owner destruction: reads return fallbacks/empty data and writes
fail with a cause. The owner is neither copyable nor movable; all access to the
owner and its views occurs on the same thread.

Every update is prepared and validated before replacing cached state. Changes
through YAML aliases detach the changed branch; editing one scope does not alter
an unrelated alias-linked scope. Unknown fields and untouched YAML tags survive
saving. Quoting/formatting/comments are not preserved byte-for-byte; ambiguous
quoted strings may be emitted with explicit string tags to preserve their type.
The root must be one mapping document. Limits are 4 MiB, depth 64 and 100000 nodes;
duplicate/non-scalar mapping keys and malformed input are rejected.

## Application and workspace paths

ApplicationPaths has explicit data, config, logs and plugin directory getters,
setters and `configFile(name)`. Supplied locations are captured as absolute native
Path values. Defaults are Qt application-data, the same directory for config,
`data/logs` and `applicationDir/plugin`. Config defaults follow data-directory
changes, while explicit overrides remain fixed. A filename is a single component;
`.yaml` is appended unless `.yaml` or `.yml` is present. Paths containing directory
separators, a drive colon, NUL, or an empty/dot traversal name are rejected. Path
resolution performs no filesystem I/O and creates no directories.

Session paths belong to [Workspace](workspaces-and-sessions.md), not application
locations. Workspace owns one document and resolves its optional plugin/script
directory metadata relative to that file's parent. Session catalog references
point into its fixed local `sessions/` root. Each session owns `session.yaml`
inside its own directory; catalog removal does not delete that directory.

## Plugin preferences

PluginManager borrows a Settings owner, validates the `enabled` mapping as strict
boolean preferences and owns the meaning of plugin IDs. It rereads preferences
before changing one enabled state, preserving unrelated external fields. It
rejects reload/save when the borrowed owner has pending edits, avoiding silent
loss of those edits. The default bootstrap uses `paths.configFile("plugins")`,
using the application-data `plugins.yaml` location with the mandatory
`termaster.plugins` envelope. Preferences are under `content.enabled`.

## Layered configuration

`SettingsOverlay` combines application `content.settings` with workspace
`content.settings`, then allows a session `content.settings` mapping to overlay
that live result. Nested mappings merge recursively; overrides replace other
values, including null. Writes affect only the highest active override scope;
removing an override reveals the inherited value. Parent changes remain visible
without copying snapshots. Nodes and overlays borrow all owners, use their thread
and become invalid when a required owner is released or replaced.

Application `content.state.lastWorkspace` is lifecycle state outside configuration.
Application `content.state.recentWorkspaces` stores the global ten-entry path
history; workspace `content.state.recentSessions` stores local catalog IDs.
Both use newest-first unique string lists. No last-session automatic restoration
is implemented. See [workspace lifecycle](workspaces-and-sessions.md).
Session settings and three-level overlays passed Debug tests; see
[session verification](workspaces-and-sessions.md#verification-status).

## Earlier verification

`TERMASTER_BUILD_SETTINGS_TESTS` builds SettingsTests. Tests use temporary files
and include typed values, errors, transactional load/write behavior, preservation
of unknown fields/tags, aliases, dirty state, depth/size/node limits, independent
scopes, reload and expired-owner views. PathTests covers ApplicationPaths defaults,
overrides and filename validation. Plugin integration and window tests verify
that the generic settings owner preserves the current lifecycle/persistence
contract. No legacy module or serial-device access is required.

Verified on 2026-11-08 with Windows x64, MSVC 19.44, Qt 6.12.0 and Ninja Release.
A fresh Core-only build with Qt SerialBus and SerialPort package discovery
explicitly disabled compiled the application and all test targets. All five
CTest suites passed without skips: plugin integration 13, plugin window 9,
action containers 12, paths 21 and settings 9 QtTest results. The independent
plugin SDK consumer was included. A native plugin-window run passed all 9 results
and its rendered capture was reviewed. Core CMake source existence, active module
includes, preservation of the archived entry point and documentation links were
validated automatically. Formatting and git diff --check passed. Unix filesystem
acceptance was not performed. These results predate session configuration loading, the
mandatory envelope and workspace lifecycle changes; they do not verify them.
