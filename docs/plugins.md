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

## Dependencies and initialization

The intended startup flow reads module descriptions, validates dependencies,
constructs their order and then loads/initializes the selected modules.
Native DLL imports and application initialization are separate: Windows can map
a dependency DLL without creating its Qt plugin root or preparing its services.
Native dependency discovery must also be configured; metadata alone does not
make dependency DLLs discoverable.

The exact metadata schema, dependency/version policy, root interface and
initialization/shutdown contract remain to be designed. This
decision does not silently establish hot reload or other lifecycle features.

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
