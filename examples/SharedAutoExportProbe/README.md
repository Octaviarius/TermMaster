# MSVC SHARED libraries with automatic exports

This standalone experiment uses two ordinary CMake `SHARED` libraries. They retain
Qt plugin metadata and are loaded by the existing `QPluginLoader` test host.
Both targets use `WINDOWS_EXPORT_ALL_SYMBOLS ON`. The public `ProviderApi.h` has
no export/import macro or compiler annotation, on either side of the DLL boundary.

Consumer directly calls a free function, a static class method and a concrete
class's constructor, destructor and non-static method. It returns 84 only when
all calculations have the expected results. CMake directly links Consumer to
the Provider target and generates `Provider.lib` and the export `.def` itself.
No manual import-library workaround is used.

## Build, install SDK and build an independent consumer

Tested on Windows x64, MSVC 19.44, Qt 6.12.0 MSVC 2022 x64, Ninja Release, C++20.
From the repository root:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1' `
  -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$probeSdk = Join-Path (Get-Location).Path 'build/SharedAutoExportProbeMsvcSdk'
& C:\Qt\Tools\CMake_64\bin\cmake.exe -S examples/SharedAutoExportProbe `
  -B build/SharedAutoExportProbeMsvc -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64 `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -DCMAKE_CXX_COMPILER=cl
& C:\Qt\Tools\CMake_64\bin\cmake.exe --build build/SharedAutoExportProbeMsvc --parallel 4
& C:\Qt\Tools\CMake_64\bin\cmake.exe --install build/SharedAutoExportProbeMsvc --prefix $probeSdk
& C:\Qt\Tools\CMake_64\bin\cmake.exe -S examples/SharedAutoExportProbe/ExternalConsumer `
  -B build/SharedAutoExportProbeMsvcExternal -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64 `
  "-DSharedAutoExportProbe_DIR=$probeSdk/lib/cmake/SharedAutoExportProbe" `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -DCMAKE_CXX_COMPILER=cl
& C:\Qt\Tools\CMake_64\bin\cmake.exe --build build/SharedAutoExportProbeMsvcExternal --parallel 4
```

The SDK supplies `bin/Provider.dll`, `lib/Provider.lib`, public headers under
`include/SharedAutoExportProbe` and a standard CMake package. The external project
uses `find_package(SharedAutoExportProbe 1 CONFIG REQUIRED)` and links
`SharedAutoExportProbe::Provider`. It does not compile provider sources.

## Binary and runtime checks

```powershell
python tools/tests/VerifyPluginDependencies.py build/SharedAutoExportProbeMsvc `
  build/SharedAutoExportProbeMsvcCases --qt-bin C:/Qt/6.12.0/msvc2022_64/bin `
  --dumpbin 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe' `
  --external-consumer build/SharedAutoExportProbeMsvcExternal/plugins/Consumer.dll
```

The common verifier checks PE exports/imports, the three concrete-class imports,
metadata isolation and five loading cases, including the independently built
consumer. It records the generated `ProbeTargets.json` target types and autoexport
setting. Provider and Consumer are both SHARED, not MODULE targets.

For the negative control configure a separate directory
`build/SharedAutoExportProbeMsvcNoAuto` using the same options and
`-DPROBE_AUTO_EXPORT=OFF`. Its provider should build, but Consumer should fail
with five unresolved C++ symbols. Capture the build output:

```powershell
$probeLog = & C:\Qt\Tools\CMake_64\bin\cmake.exe --build build/SharedAutoExportProbeMsvcNoAuto --parallel 4 2>&1
$probeLog | Out-String | Set-Content build/SharedAutoExportProbeMsvcNoAuto/BuildLog.txt -Encoding utf8
```

Add `--without-api-build build/SharedAutoExportProbeMsvcNoAuto` to the verifier
to check the expected missing exports and link errors. The public header remains
unchanged between the positive and negative builds.

No production sources, settings or installed services are changed. No downloads
are performed. Results and practical limits are in the
[acceptance report](../../docs/shared-auto-export-probe.md).
