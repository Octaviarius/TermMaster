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

The shared plugin interface and plugin manager belong under
`src/app/core/plugins`. Every plugin root inherits the shared interface.

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
The shutdown completion/error contract and the handling of native loader
references remain to be designed. Library unloading must not proceed while
shutdown is incomplete or outstanding uses remain.

Exact C++ type names, method signatures, information fields, state values,
error reporting and synchronous/asynchronous lifecycle behavior remain to be
designed. The relationship between runtime `info` and embedded metadata also
remains to be defined. This section records the accepted contract outline;
production interface and manager implementation are pending.

## Dependencies and initialization

The intended startup flow reads module descriptions, validates dependencies,
constructs their order and then loads/initializes the selected modules.
Native DLL imports and application initialization are separate: Windows can map
a dependency DLL without creating its Qt plugin root or preparing its services.
Native dependency discovery must also be configured; metadata alone does not
make dependency DLLs discoverable.

The exact metadata schema, dependency/version policy and lifecycle semantics
remain to be designed. Runtime unloading is required; automatic reloading and
state restoration are not specified.

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

Production integration is pending. The examples demonstrate the accepted build,
metadata, loading and SDK mechanism; they are not the production module manager.
