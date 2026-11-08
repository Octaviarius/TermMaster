# Cross-plugin C++ export probe

Two actual Qt plugins created by `qt_add_plugin()`, plus a console host using
`QPluginLoader`. The experiment is independent of the Termaster executable.
It targets Windows, Qt 6.7+ and C++20. MinGW GCC 13.1 and MSVC 19.44 were verified
with their matching Qt 6.12.0 packages. No downloads are performed.

`Provider.dll` embeds Qt plugin metadata and explicitly exports both:

- `int providerAdd(int, int)`;
- `ProviderMath::multiply(int, int)`, a static member of an exported C++ class.

`Consumer.dll` calls both through ordinary C++ declarations and import-library
linking, returning `providerAdd(20, 22) + ProviderMath::multiply(6, 7) == 84`.
It does not call `QLibrary::resolve()` or look up the provider's Qt interface.
The host links neither plugin and obtains the consumer through `QPluginLoader`.

Both Qt targets remain `MODULE_LIBRARY`. The provider linker is asked
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

## MSVC reproduction and installed SDK

Use an x64 MSVC developer environment and a Qt build for MSVC, not the MinGW Qt
package. The commands used for the MSVC build and independent consumer are:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1' `
  -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$probeSdk = Join-Path (Get-Location).Path 'build/PluginDependencyProbeMsvcSdk'
& C:\Qt\Tools\CMake_64\bin\cmake.exe -S examples/PluginDependencyProbe `
  -B build/PluginDependencyProbeMsvc -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64 `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -DCMAKE_CXX_COMPILER=cl
& C:\Qt\Tools\CMake_64\bin\cmake.exe --build build/PluginDependencyProbeMsvc --parallel 4
& C:\Qt\Tools\CMake_64\bin\cmake.exe --install build/PluginDependencyProbeMsvc --prefix $probeSdk
& C:\Qt\Tools\CMake_64\bin\cmake.exe -S examples/PluginDependencyProbe/ExternalConsumer `
  -B build/PluginDependencyProbeMsvcExternal -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64 `
  "-DPluginDependencyProbe_DIR=$probeSdk/lib/cmake/PluginDependencyProbe" `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -DCMAKE_CXX_COMPILER=cl
& C:\Qt\Tools\CMake_64\bin\cmake.exe --build build/PluginDependencyProbeMsvcExternal --parallel 4
```

The installed SDK contains:

```text
bin/Provider.dll
include/PluginDependencyProbe/ProviderApi.h
include/PluginDependencyProbe/ProbeInterface.h
lib/Provider.lib
lib/cmake/PluginDependencyProbe/PluginDependencyProbeConfig.cmake
lib/cmake/PluginDependencyProbe/PluginDependencyProbeConfigVersion.cmake
```

The separate consumer uses only the public SDK and Qt:

```cmake
find_package(PluginDependencyProbe 1 CONFIG REQUIRED)
target_link_libraries(Consumer PRIVATE PluginDependencyProbe::Provider)
```

Run the verifier with `--dumpbin` for the chosen MSVC tools and the external DLL:

```powershell
python tools/tests/VerifyPluginDependencies.py build/PluginDependencyProbeMsvc `
  build/PluginDependencyProbeMsvcCases --qt-bin C:/Qt/6.12.0/msvc2022_64/bin `
  --dumpbin 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe' `
  --external-consumer build/PluginDependencyProbeMsvcExternal/plugins/Consumer.dll
```

For the missing-export control, configure another MSVC build directory with the
same arguments and `-DPROBE_EXPORT_API=OFF`. Provider builds but Consumer is expected
to fail with `LNK2019`/`LNK1120`. Save that build's output as UTF-8 `BuildLog.txt` in
its build directory. Add `--without-api-build BUILD_DIRECTORY` to the verifier to
check that only the Qt entry points remain exported and the expected link errors
were produced. This is a deliberate negative test, not the normal build mode.

Keep compiler, Qt variant, architecture and build configuration aligned. The
same SDK installation/external-build flow is also verified for MinGW; its import
library is `lib/libProvider.dll.a`. Use separate build/SDK directories per toolchain.
