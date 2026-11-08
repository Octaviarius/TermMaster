# Cross-plugin C++ export probe

Two actual Qt plugins created by `qt_add_plugin()`, plus a console host using
`QPluginLoader`. The experiment is independent of the TermMaster executable.
It targets Windows, Qt 6.7+ and C++20. MinGW is verified; the MSVC path is prepared
but awaits a matching Qt/MSVC installation and execution. No downloads are performed.

`Provider.dll` embeds Qt plugin metadata and explicitly exports both:

- `int providerAdd(int, int)`;
- `ProviderMath::multiply(int, int)`, a static member of an exported C++ class.

`Consumer.dll` calls both through ordinary C++ declarations and import-library
linking, returning `providerAdd(20, 22) + ProviderMath::multiply(6, 7) == 84`.
It does not call `QLibrary::resolve()` or look up the provider's Qt interface.
The host links neither plugin and obtains the consumer through `QPluginLoader`.

Both Qt targets remain `MODULE_LIBRARY`. On MinGW the provider linker is asked
to emit an import library. The consumer links that file directly, bypassing
CMake's rejection of a `MODULE` target in `target_link_libraries()`.
This demonstrates feasibility, not a selected production build design.

## Reproduce

From the repository root, using the installed development tools:

```powershell
$env:PATH = 'C:\Qt\6.12.0\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
& C:\Qt\Tools\CMake_64\bin\cmake.exe -S examples/PluginDependencyProbe `
  -B build/PluginDependencyProbe -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64 `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
& C:\Qt\Tools\CMake_64\bin\cmake.exe --build build/PluginDependencyProbe --parallel 4

python tools/tests/VerifyPluginDependencies.py build/PluginDependencyProbe `
  build/PluginDependencyProbeCases --qt-bin C:/Qt/6.12.0/mingw_64/bin `
  --objdump C:/Qt/Tools/mingw1310_64/bin/objdump.exe
```

The verifier checks PE exports/imports and runs fresh processes with isolated
plugin directories and controlled DLL search paths. It also checks that reading
embedded metadata does not map the provider DLL, including when it is missing.
`GetModuleHandleW` observes native mapping without loading the provider.
Root-constructor diagnostics distinguish DLL mapping from Qt plugin instantiation.
Checks remain active independently of Release/NDEBUG or Python optimization.

For manual provider-first loading:

```powershell
$env:QT_FORCE_STDERR_LOGGING = '1'
& .\build\PluginDependencyProbe\PluginDependencyProbe.exe --provider-first
```

For the CMake restriction, configure a separate build with the same toolchain
arguments and `-DPROBE_DIRECT_TARGET_LINK=ON`. Configuration is expected to fail:
`Target "Provider" of type MODULE_LIBRARY may not be linked into another target.`

Results and limitations: [acceptance report](../../docs/plugin-dependency-probe.md).

## Pending MSVC verification

Use an x64 MSVC developer environment and a Qt build for MSVC, not the MinGW Qt
package. Configure a fresh build directory with Ninja, `-DCMAKE_CXX_COMPILER=cl`,
and `-DCMAKE_PREFIX_PATH` pointing to that matching Qt installation. Build Release
first. The provider's explicit exports should produce `imports/Provider.lib`.
Run the same verifier with the MSVC build directory and `--dumpbin PATH_TO_DUMPBIN`
instead of `--objdump`; set `--qt-bin` to the matching MSVC Qt bin directory.

Keep compiler, Qt variant, architecture and build configuration aligned. The
MinGW result does not establish the result for MSVC; that run remains pending.
