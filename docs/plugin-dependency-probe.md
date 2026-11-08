# Direct calls between Qt plugins: experiment

Date: 2026-11-08. Status: verified comparison experiment.
The subsequently accepted mechanism uses SHARED libraries with autoexport;
see [Plugin module mechanism](plugins.md). This report preserves the earlier
explicit-export MODULE experiment and its results.

Verification covers both MinGW and MSVC, including installation of a provider
SDK and an independently configured consumer build using only that SDK API.

## Question and result

Can one Qt plugin call ordinary exported C++ functions from another Qt plugin,
or are Qt plugins limited to metadata/Qt entry points?

**It works with both tested Windows toolchains.** A Qt plugin DLL can
export ordinary functions and class methods alongside its Qt plugin entry points.
The consumer directly imports and calls those symbols. Ordinary SHARED libraries
are therefore not strictly required for cross-plugin C++ calls in this setup.

The distinction is between symbol exports, CMake target-linking rules, native DLL
discovery and application initialization. They are separate concerns.

## Fixture

- Windows x86-64, Qt 6.12.0, MinGW GCC 13.1.0, CMake/Ninja Release, C++20.
- Separate Qt 6.12.0 MSVC 2022 x64 package, MSVC 19.44.35225.0 / VS 17.14.29,
  Ninja Release, C++20. No mixing of MinGW and MSVC binaries.
- Provider and Consumer are both built with `qt_add_plugin()` as MODULE targets.
- Provider explicitly exports a C++ free function and a C++ class with a static method.
- Both plugins contain `Q_PLUGIN_METADATA` and implement the test Qt interface.
- Consumer directly calls the provider API through its header and import library.
- The host uses `QPluginLoader` and does not link either plugin at build time.

The provider PE export table contains:

```text
_Z11providerAddii
_ZN12ProviderMath8multiplyEii
qt_plugin_instance
qt_plugin_query_metadata_v2
```

The consumer PE import table names `Provider.dll` and imports the two C++ symbols.
The host imports neither `Provider.dll` nor `Consumer.dll`.

For MSVC, `dumpbin /exports` confirms the corresponding decorated names
`?providerAdd@@YAHHH@Z` and `?multiply@ProviderMath@@SAHHH@Z`, together with the Qt
entry points. `imports/Provider.lib` is generated and used to link Consumer.

## Verified runtime cases

Each case runs in a fresh process with staged DLL copies. The host reads embedded
metadata first; both before and after the metadata reads, `Provider.dll` is not
mapped and the consumer loader reports not loaded. The consumer's dependency list
is readable even when the provider DLL is absent.

| Case | Result | What it establishes |
| --- | --- | --- |
| Instantiate Provider first, then Consumer; plugin folder absent from PATH | Pass: Provider returns 42, Consumer returns 84 | Direct C++ calls work between two Qt plugin DLLs. |
| Load Consumer first with plugin folder on PATH | Pass: Consumer returns 84; provider DLL is mapped; only Consumer root is constructed | The native loader can pull in the dependency without creating its Qt plugin object. |
| Load Consumer first without a suitable DLL search path | Expected load failure despite both files being present | Scanning a plugin folder or knowing the consumer's absolute path does not by itself establish native dependency discovery in this setup. |
| Provider DLL absent, folder on PATH | Expected load failure; consumer metadata still readable | Metadata dependency declarations do not resolve or replace native imports. |
| Consumer built independently through the installed SDK | Pass: Consumer returns 84 | Public headers, the import library and CMake package suffice to build an additional Qt plugin without provider implementation sources. |

The verifier checks constructor diagnostics as well as calculations and module
mapping. It does not infer plugin initialization from mere DLL presence.
All five cases passed on both MinGW and MSVC. MSVC checks also passed with
Python optimization enabled; validation does not depend on Python assertions.

## MSVC control: API exports disabled

A separate Release build configured with `-DPROBE_EXPORT_API=OFF` leaves the
ordinary API unmarked for export while retaining both Qt plugin entry points.
The provider still builds. Its PE export table contains only:

```text
qt_plugin_instance
qt_plugin_query_metadata_v2
```

Consumer then fails to link with two `LNK2019` diagnostics for `providerAdd` and
`ProviderMath::multiply`, followed by `LNK1120`. The verifier checks both this
export table and the captured link diagnostics. This reproduces the reported
symbol-hiding symptom. Explicit `Q_DECL_EXPORT` on the producer API and
`Q_DECL_IMPORT` on the consumer declarations fix this fixture without giving up
Qt plugin metadata or `QPluginLoader`.

## Build-system restriction

Direct `target_link_libraries(Consumer PRIVATE Provider)` was separately tested
and rejected during CMake configuration:

```text
Target "Provider" of type MODULE_LIBRARY may not be linked into another target.
```

The working fixture keeps both Qt plugins as MODULE targets and asks the
linker to emit an import library for Provider's explicit exports. Consumer links
that file rather than the CMake MODULE target. A build dependency ensures Provider
is built before Consumer. No `resolve()` call or function-table indirection is
used for the cross-plugin calls.

This workaround demonstrates that the MODULE-target restriction is not a ban on
exporting symbols from Qt plugins. Production build packaging still needs a
deliberate design if other modules link these APIs.

## Architectural implications and limits

The user requires a module SDK so an additional plugin can be built separately.
Its deliverables should include authored public headers, the Windows import
library (`.dll.a` for MinGW or `.lib` for MSVC), the runtime DLL, and CMake package
metadata exposing the public include paths, link dependency and Qt requirements.
Headers are selected from the source API, not inferred/generated from DLL exports.
The probe now implements that SDK through `cmake --install`, separately for each
toolchain. `ExternalConsumer` is a separate CMake project using
`find_package(PluginDependencyProbe 1 CONFIG REQUIRED)` and the imported target
`PluginDependencyProbe::Provider`. That target describes the provider DLL's
exported API as a linkable imported SHARED library; the original Qt build target
remains MODULE. No provider implementation sources are part of the external build.

Both installed SDK DLLs were compared byte-for-byte with the providers used by
the runtime tests. External compile commands were checked for installed SDK
include paths. This is a minimal example SDK, not a production ABI/versioning
policy or a deployment package including Qt and compiler runtimes.

Qt plugins remain a technically viable option. The subsequent SHARED autoexport
experiment established the selected build mechanism. Embedded metadata and an application dependency graph can still
be useful for validation and ordering explicit initialization. The native loader
handles binary imports, not application-level readiness of plugin services.

Explicit exports are required by this fixture; it does not claim that every
internal symbol is automatically public. The result is specific to the tested
toolchains. Other operating systems, incompatible compiler/Qt builds,
cross-version ABI stability, hot unload/reload and cyclic dependencies were not
tested. The host's metadata/instance calls are synchronous on its main thread.
Qt loaders and their default unload hints remain in effect until process exit.

No production application sources were changed and the main application was not
built. C++ formatting, Python syntax, local documentation links and repository
whitespace were checked. Five runtime cases passed on each toolchain, and
the direct-target configuration failed with the expected MODULE diagnostic.
The missing-API-export MSVC control failed to link as expected.

## Evidence and reproduction

- [Source and build commands](../examples/PluginDependencyProbe/README.md)
- [Automated binary/runtime verifier](../tools/tests/VerifyPluginDependencies.py)
- [Durable MinGW runtime results](PluginDependencyProbeResults.json)
- [Durable MSVC runtime and missing-export control results](PluginDependencyProbeMsvcResults.json)

Generated MinGW artifacts use the `build/PluginDependencyProbe` prefix; MSVC
artifacts use `build/PluginDependencyProbeMsvc`. The `Sdk` and `External` directories
contain installed SDKs and independent consumer builds. The MSVC negative build
is under `build/PluginDependencyProbeMsvcNoExports`. The report and JSON above
preserve acceptance evidence independently of disposable build directories.
