# SHARED autoexport on MSVC: experiment

Date: 2026-11-08. Status: verified experiment; build/loading mechanism accepted.
The accepted mechanism and remaining design
questions are recorded in [Plugin module mechanism](plugins.md).

## Result

Two CMake SHARED libraries can call ordinary C++ APIs across a DLL boundary on
MSVC without any explicit export/import annotation in their public API header.
The successful fixture uses `WINDOWS_EXPORT_ALL_SYMBOLS ON` on the targets and
normal `target_link_libraries(Consumer PRIVATE Provider)`.

Configuration: Windows x64, MSVC 19.44.35225.0, Qt 6.12.0 MSVC 2022 x64,
CMake 3.30.5/Ninja, Release, C++20. Both targets are verified as SHARED_LIBRARY
in generated target records. The main application is not modified or rebuilt.

## What was tested

The authored public header contains ordinary declarations, with no `Q_DECL_*`,
`__declspec`, `PROVIDER_API`, `dllexport` or `dllimport` markup:

```cpp
int providerAdd(int left, int right);

class ProviderMath
{
public:
    static int multiply(int left, int right);
};
```

It additionally declares `ProviderAccumulator`, a concrete non-virtual class
with an out-of-line constructor, destructor and non-static `add()` method.
Consumer constructs it with 40, calls `add(2)`, checks 42, destroys it, and returns
`providerAdd(20, 22) + ProviderMath::multiply(6, 7)`, which must equal 84.

`dumpbin` confirms that Provider exports the API symbols and Consumer imports
them, including the constructor, destructor and `add()` method. The host does
not link either plugin. CMake generates the `.def` and import library from the
SHARED target; there is no handwritten export list or import-library workaround.
The installed SDK header matches the unannotated source header, and the installed
provider DLL matches the provider used by the runtime tests byte-for-byte.

Qt metadata and plugin instantiation still work: the SHARED libraries contain
`Q_PLUGIN_METADATA` and the host uses `QPluginLoader`. Qt's own generated plugin
entry points remain framework-managed; the absence of manual markup applies to
the public C++ API. The experiment uses `add_library(... SHARED ...)`, not
`qt_add_plugin()`, which would create MODULE targets.

## Verified loading cases

| Case | Result |
| --- | --- |
| Provider instantiated before Consumer | Provider returns 42; Consumer returns 84. |
| Consumer loaded first with native dependency search path | Consumer returns 84; provider DLL is mapped without creating its Qt plugin root. |
| Consumer loaded first without suitable search path | Expected load failure despite both DLL files being present. |
| Provider DLL absent | Expected load failure; embedded consumer metadata remains readable. |
| Consumer independently built against installed SDK | Returns 84 using the same unannotated API header and generated import library. |

All five cases passed. Metadata reads were verified not to map the provider DLL.
The verifier also ran with Python optimization enabled, so its checks do not
depend on Python assertions or C++ Release assertions.

## Control with autoexport disabled

The same source was built in a separate directory with
`-DPROBE_AUTO_EXPORT=OFF`. Provider still builds because Qt explicitly exports
its own plugin entry points. Its export table then contains only:

```text
qt_plugin_instance
qt_plugin_query_metadata_v2
```

Consumer fails with five `LNK2019` unresolved-symbol diagnostics covering the
free function, static method, constructor, destructor and non-static method,
followed by `LNK1120`. Thus the successful API exports originate from CMake
autoexport, not leftover explicit annotations in the public header.

## SDK and interpretation

The SDK is installed through normal CMake target/export installation. It contains
`Provider.dll`, `Provider.lib`, public headers and a CMake package. A separate
consumer project uses `find_package(SharedAutoExportProbe 1 CONFIG REQUIRED)`
and `SharedAutoExportProbe::Provider`. It needs no provider implementation sources
or explicit export/import annotations. Both producer and consumer use matching
MSVC/Qt/architecture/Release settings.

This satisfies the tested function and non-virtual-class use case. It does not
establish that every possible C++/Qt API can omit import annotations. CMake
documents exceptions for imported global data and some virtual-table references.
Autoexport also exports implementation symbols; it does not enforce the boundary
of the public headers. Cross-DLL inheritance, imported data/static data, arbitrary
templates, cross-version ABI stability, hot unloading, Debug, MinGW and other
operating systems were not tested in this experiment.
[CMake autoexport documentation](https://cmake.org/cmake/help/latest/prop_tgt/WINDOWS_EXPORT_ALL_SYMBOLS.html).

This build/loading mechanism is accepted based on the verification results.
Dependency metadata, DLL discovery and explicit module initialization remain
distinct responsibilities; the production manager contract is still to be designed.

## Evidence

- [Source and reproduction](../examples/SharedAutoExportProbe/README.md)
- [Unannotated public header](../examples/SharedAutoExportProbe/ProviderApi.h)
- [Durable runtime, target-type and negative-control results](SharedAutoExportProbeMsvcResults.json)
- [Comparison: explicit exports in Qt MODULE plugins](plugin-dependency-probe.md)

Generated artifacts use the `build/SharedAutoExportProbeMsvc` prefix, including
SDK, external-consumer and NoAuto directories. Acceptance records remain in docs.
C++ formatting, Python syntax, local documentation links and `git diff --check`
were verified. No downloads or production GUI changes were involved.
