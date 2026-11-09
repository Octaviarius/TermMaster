# Plugin module mechanism

Decision date: 2026-11-08. Status: accepted.

## Accepted build and loading mechanism

Termaster modules are ordinary CMake `SHARED` libraries. Use
`add_library(... SHARED ...)` rather than `qt_add_plugin()` for these modules.
On Windows/MSVC, enable `WINDOWS_EXPORT_ALL_SYMBOLS` on each module target so
CMake generates the export definition and import library automatically.

The validated public API style uses ordinary C++ declarations without per-symbol
export/import annotations. Other modules may link directly to the module target
or to the imported target supplied by its SDK. There is no need for the manual
MODULE-target/import-library workaround used in the earlier experiment.

Keep Qt's metadata and object-loading mechanisms:

- Enable `moc` processing for module QObject classes.
- Embed the module description through `Q_PLUGIN_METADATA`.
- Use `Q_INTERFACES` / `Q_DECLARE_INTERFACE` for the plugin root contract.
- Read descriptions with `QPluginLoader::metaData()` before loading code.
- Load and obtain plugin root objects through `QPluginLoader`.

This is a custom application module manager using existing Qt facilities. It
does not require a custom binary metadata parser or a replacement for the native
dynamic-library loader.

## SDK deliverables

The build must assemble each module's deliverables under one module-specific
distribution directory, including its runtime library and public headers. Its
import library and CMake package belong to that same distribution root. This
requirement concerns module-owned artifacts; it does not establish bundling of
Qt or compiler runtimes.

The accepted public include convention is `<Termaster/ModuleName/Header.h>`.
Use the exact, case-sensitive prefix `Termaster` consistently in installed header
paths and includes. Module-specific subdirectories allow different modules to
publish identically named header files without include-path ambiguity:

```cpp
#include <Termaster/Core/AnyHeader.h>
#include <Termaster/SshTerminal/Serial.h>
#include <Termaster/SerialTerminal/Serial.h>
```

Place these under `include/Termaster/ModuleName` inside the module distribution.
The module's CMake imported target exposes the distribution's `include` directory.
File path separation does not itself separate C++ type names or linker symbols;
their naming/namespace policy remains a separate API design question. These are
include-path examples, not declarations of backend implementation status.

Each module intended for use by separately built extensions supplies:

- its runtime DLL;
- its import library (`.lib` in the verified MSVC configuration);
- authored public headers;
- a CMake package exposing an imported target and its usage requirements.

Public headers are selected and installed from source, not inferred from the
DLL. Additional plugins can use `find_package()` and link the imported module
target without building the provider's implementation sources.

## Core plugin contract and location

The public plugin contract and manager headers belong under
`src/Core/inc/Termaster/Core/Plugin`; the implementation belongs under
`src/Core/src/Plugin`. Every plugin root inherits the shared interface.

The interface exposes four operations:

| Operation | Responsibility |
|---|---|
| `startup` | Start the plugin. |
| `shutdown` | Stop the plugin and release its owned resources before runtime unloading. |
| `info` | Expose plugin information. |
| `state` | Expose the current plugin state. |

### Shutdown and runtime unloading

Runtime unloading is a requirement. Completion of `shutdown` must leave the
plugin ready for library unloading:

- Stop accepting new work and cancel or complete pending operations.
- Stop and join owned workers before destroying resources they use.
- Release owned allocations, handles, connections and other resources.
- Destroy plugin-created runtime objects, including externally owned objects,
  through an agreed ownership/release contract before unloading their code.
- Remove registered callbacks, factories, timers and other entry points; ensure
  queued work cannot subsequently enter plugin code.

The Qt-managed plugin root remains loader-owned and must be destroyed through
the loader lifecycle, rather than deleting itself inside `shutdown`.

The manager must stop dependent plugins and release external uses of the plugin
before unloading its library. A plugin cannot make unloading safe while another
module retains callable pointers, live objects or native DLL dependencies on it.
Shutdown success is required before library unloading. All native module imports
must be declared as dependencies and loaders must be exclusively manager-owned.
Untracked native references and arbitrary external pointers cannot be reclaimed
by the manager. Library unloading must not proceed while shutdown is incomplete
or outstanding uses remain.

`Plugin` is a pure interface with synchronous `bool startup(QString* error)`
and `bool shutdown(QString* error)` methods, plus `Info info() const` and
`State state() const`. Lifecycle completion is reported by the return value and
state: successful startup enters `Running`; successful shutdown enters `Stopped`.
A failed startup must also support shutdown to release partially acquired resources.
All calls run on the manager's QObject thread and must not reenter the manager.

`PluginManager` owns the Qt loaders and implements `QAbstractListModel`. Catalog
roles expose ID, name, version, path, enabled preference, lifecycle status,
dependencies, direct reverse dependencies and diagnostic text. A module-owned
Widgets window exposes this model through a table projection and controller.
The settings owner is borrowed and must outlive the manager.

## Dependencies and initialization

The intended startup flow reads module descriptions, validates dependencies,
constructs their order and then loads/initializes the selected modules.
Native DLL imports and application initialization are separate: Windows can map
a dependency DLL without creating its Qt plugin root or preparing its services.
Native dependency discovery must also be configured; metadata alone does not
make dependency DLLs discoverable.

Startup uses deterministic topological order; shutdown reverses actual load
order. Missing dependencies, duplicate IDs/library filenames and cycles block
initialization. Disabling a dependency stops its transitive consumers first.
Their enabled preferences remain unchanged, so they recover automatically when
all dependencies become available and running. Dependencies are not implicitly
enabled. A new plugin is disabled unless its ID is already enabled in settings.

Startup/runtime failures remain latched until an explicit targeted retry or a
replacement of that plugin. Unrelated directory changes do not retry failures.
A shutdown or unload failure pins the library and its dependencies; it never
permits unloading code still in use. Misbehaving plugins cannot be forcibly
cleaned up by the host. The manager retains pinned loaders at process shutdown
and reports this condition rather than unmapping executing code.

The application starts a manager for `plugin/` beside its executable. Discovery
is recursive. File and directory watches use a 150 ms debounce, with a two-second
polling fallback; content hashes detect changes. Removed/replaced watches are
reattached. Limits are 512 libraries, 8192 catalog entries, 256 MiB per library,
and 128 dependencies per plugin. A scan failure preserves the previous catalog.

Libraries are loaded directly from their module directories. No shadow copies
or temporary runtime directories are used. The supported update sequence is:

1. Disable the target with `setEnabled(id, false)` and check the result.
2. Replace its library after successful shutdown/unload.
3. Allow discovery to refresh the catalog, or call `rescan()` explicitly.
4. Enable the target; its enabled consumers recover in dependency order.

An unexpected change to a still-loaded library is reported and held until manual
unload. `reload(id)` restarts an unchanged plugin and its consumers without
changing enabled preferences. No session-state restoration is provided.
Qt load hints are set before assigning the library filename to avoid the default
`PreventUnloadHint`; tests verify real Windows unmapping and file replacement.
[Qt plugin loader](https://doc.qt.io/qt-6/qpluginloader.html).

## Metadata and configuration

Embedded metadata uses this schema, under interface IID
`org.octaviarius.Termaster.Plugin/1.0`:

```json
{
    "api": 1,
    "id": "example.consumer",
    "name": "Example consumer",
    "version": "1.0.0",
    "dependencies": ["example.provider"]
}
```

Runtime `info()` must match the embedded description. IDs contain letters,
digits, dots, hyphens or underscores, start with a letter/digit and have at most
128 characters. Versions contain three numeric components. Dependency version
ranges and cross-toolchain ABI compatibility are not implemented; plugins must
use a compatible compiler, Qt, architecture and configuration.

Enable preferences are stored in `plugins.yaml` under Qt's application-data
directory, through an independently owned `Settings`:

```yaml
version: 1
type: termaster.plugins
description: Plugin activation preferences
content:
  enabled:
    example.provider: true
    example.consumer: true
```

The envelope is mandatory; bare legacy mappings are rejected without migration.
Workspace plugin directory metadata does not change application plugin discovery.

The YAML layer has been rewritten to remove unsafe QVariant casts, preserve
unknown mappings/sequences and check parse/serialization/write failures. Plugin
preferences are validated before atomic `QSaveFile` replacement; malformed files
are not overwritten. File limits are 4 MiB, 64 nesting levels and 100000 nodes.
The former QSettings-based APIs are unbuilt reference material; current consumers use Core Settings.

## Module build helper

`cmake/TermasterPlugin.cmake` provides `termaster_add_plugin`. Each build produces
an isolated SDK under `build/plugin/<Target>/`: runtime library, import library,
public headers and a relocatable `Termaster<Target>` CMake package. Public headers
are authored below `HEADER_BASE_DIR/Termaster/<Target>` (default base: `inc/`) and preserve subdirectories.
`TermasterPluginApi` supplies the common `<Termaster/Core/Plugin/Plugin.h>` contract.

```cmake
termaster_add_plugin(ExampleConsumer
    SOURCES
        Consumer.cpp
    HEADER_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PUBLIC_HEADERS
        inc/Termaster/ExampleConsumer/Consumer.h
    DEPENDENCIES
        Termaster::ExampleProvider
)
```

For an external build, add the API SDK, dependency SDK roots and Qt to
`CMAKE_PREFIX_PATH`, then use `find_package(TermasterPluginApi CONFIG REQUIRED)`
and `find_package(TermasterExampleProvider CONFIG REQUIRED)`. The helper creates
SHARED targets with automatic exports; it does not bundle Qt/compiler runtimes.
One module SDK represents the last built configuration.

## Evidence and implementation status

The selected mechanism is verified in
[SharedAutoExportProbe](../examples/SharedAutoExportProbe/README.md) on Windows
x64 with MSVC 19.44, Qt 6.12.0, Ninja Release and C++20. It covers direct calls
to a free function, static method, constructor, destructor and non-static method
of a non-virtual class, plus an independently built SDK consumer. All five load
cases passed. Disabling autoexport reproduces unresolved-symbol link failures.
[Acceptance report](shared-auto-export-probe.md).

This does not establish annotation-free support for every C++ API shape. Imported
data, cross-DLL inheritance and virtual-table cases need verification when
introduced; the autoexport experiment does not cover those cases or MinGW.
Matching toolchain/Qt/architecture/configuration were used for the tests.

The runtime implementation is in `src/Core/src/Plugin`; the application owns
its manager for the duration of the event loop. Earlier examples remain isolated
experiments, not production module implementations.

The integration suite in `tools/tests/Plugin` uses real SHARED DLL fixtures and
an independently built SDK consumer. It checks dependency ordering, native calls,
cascade/recovery, missing dependencies, cycles/duplicates, failed-start cleanup,
shutdown pinning, runtime failure, actual Windows unmapping, directory changes,
replacement after manual unload, and YAML preservation/validation. Fixtures own
real worker threads and timers and require their cleanup before root destruction.
Build with `-DTERMASTER_BUILD_PLUGIN_TESTS=ON`; use the Qt bin directory on PATH
when running `ctest`. `TERMASTER_EXTERNAL_PLUGIN` selects the independently built
`ExternalConsumer` fixture for the optional SDK scenario.


### Verification record

Verified on Windows x64, MSVC 19.44, Qt 6.12.0 and Ninja Release on 2026-11-08.
The application and integration target built successfully. The final integration
run reported 13 passed, zero failed and zero skipped, including the external SDK
consumer. Tests used synthetic modules, real timers and worker threads; no device
or external-network acceptance was performed. The GUI verification record below
covers the subsequently added management window.

The SDK consumer was configured independently from
`tools/tests/Plugin/ExternalConsumer`, with the API and provider SDK roots in
`CMAKE_PREFIX_PATH`, and built with MSVC Release. The test run selected its DLL
through `TERMASTER_EXTERNAL_PLUGIN`. Generated logs remain disposable build output.


## Management window

The implementation belongs to `src/Core/src/Gui/Plugin/`. The concrete
window, table projection and controller headers remain internal. The public
`<Termaster/Core/Gui/Plugin/PluginManagerGui.h>` factory creates a modeless `QDialog`;
the caller owns it. Its implementation is `src/Core/src/Gui/Plugin/PluginManagerGui.cpp`. Destroying the borrowed manager disables the window safely.
Closing the window does not itself change plugin preferences or stop the manager.

The table shows enabled preferences, names, IDs, versions and runtime states,
with filtering and sorting. Selection is restored by library path when catalog
resets occur. Commands capture plugin IDs before queued execution; row positions
are never used as command identities. The controller delegates enable, disable,
restart and refresh to `PluginManager`; widgets do not read or write YAML.

Details show the library path, dependencies, direct reverse dependencies
(`Required by`) and concrete diagnostics. Reverse dependencies include disabled
plugins and are recalculated from the current catalog. Dependency lists are
bounded scrollable fields. Invalid metadata cannot be enabled through the window.
Lifecycle and persistence errors are reported without a modal confirmation flow.

The current App shell opens this window through Tools/Plugins or its toolbar
command. The obsolete terminal entry point was removed with the archived modules.

The window uses standard Qt Widgets and native palette/font metrics. Content can
scroll on small displays; the close button and operation diagnostics remain outside the scroll area.
Initial size is bounded by the current screen's available geometry.

### GUI verification

Windows x64, MSVC 19.44, Qt 6.12.0, Ninja Release. The GUI suite exercises seven
scenarios: enable/disable/restart with cascade and YAML persistence, live catalog
and reverse-dependency updates with selection retention, filtering, disabling
invalid plugins, concrete
failure diagnostics, manager destruction with a pending command, and recovery
from invalid YAML. The existing lifecycle suite remains enabled. Native captures
at scale factors 1, 1.5 and 2 check that the frame fits the available screen and
permit visual inspection of controls, dependencies and error text. Synthetic
fixtures are used; no real device or external-network commands are involved.


The completed GUI run passed all seven behavior scenarios (nine QtTest results
including setup/cleanup). The existing lifecycle suite passed all 13 results,
including the independently built SDK consumer. The Windows application uses
the GUI subsystem. Final native captures at 100%, 150% and 200% were reviewed;
small viewports expose scrollable content with persistent close/error controls.
Tests also verify that Enter in the filter does not change plugin preferences.

Plugin preferences now use the general [Core Settings owner](settings.md). The plugin manager
validates its own enabled states; no Managers module or SettingsManager is required.

## Hosted terminal extensions

PluginManager accepts a borrowed PluginHost before starting any module. An
optional HostedPlugin interface receives it before startup; the base lifecycle
interface remains unchanged. The SDK uses pure virtual headers and Qt/std value
types so plugins do not link a duplicate static Core. Host lifetime covers
shutdown. SerialTerminal demonstrates terminal/device factories, settings editor
registration and scoped icon actions. See [terminal lifecycle](terminals-and-serial.md).

## Test plugin isolation

Desktop presets disable plugin fixtures by default. Explicitly enabling
`TERMASTER_BUILD_PLUGIN_TESTS` builds the synthetic modules and provider SDKs
under `<build>/tools/tests/Plugin/plugin`, outside the application's scanned
`<build>/plugin` directory. The production libraries there are SerialTerminal and ConsoleTerminal, alongside
the header-only PluginApi SDK. No fixture library is a production plugin.

The independent SDK consumer uses `<build>/plugin/PluginApi` and
`<build>/tools/tests/Plugin/plugin/PluginFixtureProvider` as package prefixes.
Normal Desktop configuration restores the fixture option to OFF; its explicit
`-DTERMASTER_BUILD_PLUGIN_TESTS=ON` override remains available for acceptance.

Verified with MSVC / Qt Debug: both plugin integration/window suites passed
from the private output, including the independently rebuilt SDK consumer.
Normal Desktop Debug and Release caches have fixtures disabled. Ninja's
`-t cleandead` removed historical fixture outputs, and the normal application
build graph contains no PluginFixture targets. Both runtime directories contain
SerialTerminal.dll (and ConsoleTerminal.dll after local-console integration); generated SDK metadata from older builds is harmless
and is not a loadable plugin. The normal Debug application target builds.

Local process terminals and environment commands are provided by the independent
[ConsoleTerminal plugin](local-console.md), using HostedPlugin ABI version 4.
