# Direct calls between Qt plugins: experiment

Date: 2026-11-08. Status: verified experiment; plugin architecture remains open.

Verification currently covers MinGW only. The user requested a separate MSVC
run while installing the required tools. The build fixture and PE verifier have
an MSVC path prepared; no successful MSVC run is claimed. A matching Qt/MSVC SDK
is required before execution.

## Question and result

Can one Qt plugin call ordinary exported C++ functions from another Qt plugin,
or are Qt plugins limited to metadata/Qt entry points?

**It works in the tested Windows/MinGW configuration.** A Qt plugin DLL can
export ordinary functions and class methods alongside its Qt plugin entry points.
The consumer directly imports and calls those symbols. Ordinary SHARED libraries
are therefore not strictly required for cross-plugin C++ calls in this setup.

The distinction is between symbol exports, CMake target-linking rules, native DLL
discovery and application initialization. They are separate concerns.

## Fixture

- Windows x86-64, Qt 6.12.0, MinGW GCC 13.1.0, CMake/Ninja Release, C++20.
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

The verifier checks constructor diagnostics as well as calculations and module
mapping. It does not infer plugin initialization from mere DLL presence.

## Build-system restriction

Direct `target_link_libraries(Consumer PRIVATE Provider)` was separately tested
and rejected during CMake configuration:

```text
Target "Provider" of type MODULE_LIBRARY may not be linked into another target.
```

The working fixture keeps both Qt plugins as MODULE targets and asks MinGW's
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
The present probe supplies the header and generated MinGW import library in its
source/build trees; it does not yet implement an installable SDK or test an
independent third-party build. SDK packaging is a separate requested design step.

Qt plugins remain a technically viable option; no architecture is selected by
this experiment. Embedded metadata and an application dependency graph can still
be useful for validation and ordering explicit initialization. The native loader
handles binary imports, not application-level readiness of plugin services.

Explicit exports are required by this fixture; it does not claim that every
internal symbol is automatically public. The result is specific to the tested
toolchain. MSVC, other operating systems, incompatible compiler/Qt builds,
cross-version ABI stability, hot unload/reload and cyclic dependencies were not
tested. The host's metadata/instance calls are synchronous on its main thread.
Qt loaders and their default unload hints remain in effect until process exit.

No production application sources were changed and the main application was not
built. C++ formatting, Python syntax, local documentation links and repository
whitespace were checked. All four positive/negative runtime cases passed, and
the direct-target configuration failed with the expected MODULE diagnostic.

## Evidence and reproduction

- [Source and build commands](../examples/PluginDependencyProbe/README.md)
- [Automated binary/runtime verifier](../tools/tests/VerifyPluginDependencies.py)
- [Durable runtime results](PluginDependencyProbeResults.json)

Raw generated artifacts are under `build/PluginDependencyProbe` and
`build/PluginDependencyProbeCases`; the report and result JSON above are durable.
