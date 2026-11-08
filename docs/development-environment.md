# Windows development environment and Qt desktop builds

`.vscode/ExportEnvironment.ps1` exports Qt and MSVC x64 variables into the current
PowerShell process. The script is local and remains ignored with `.vscode/`; it
is not included in a fresh checkout. It does not configure/build/run Termaster, deploy files or
write persistent user/machine environment variables. Visual Studio is discovered
through vswhere; Developer PowerShell is initialized without changing directories.
Qt bin, CMake and Ninja are added to PATH without duplicate entries. Existing
CMAKE_PREFIX_PATH entries are preserved after the selected Qt root.

From the repository root:

```powershell
. .\.vscode\ExportEnvironment.ps1
cmake --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Debug
cmake --build --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Debug
.\build\Desktop_Qt_6_12_0_MSVC2022_64bit-Debug\Termaster.exe
```

CMakePresets.json defines Debug and Release configure/build/test presets. Each
configuration has its own directory, matching Qt desktop kit naming:

```text
build/Desktop_Qt_6_12_0_MSVC2022_64bit-Debug/
build/Desktop_Qt_6_12_0_MSVC2022_64bit-Release/
```

Use the matching Release preset to configure/build Release. CMake/Ninja output
paths are controlled by the preset, not by scripts moving generated files. The
older probe/acceptance build directories remain disposable and are not used by
these presets. Their existing executables may remain open during development.

The export script defaults to `C:\Qt\6.12.0\msvc2022_64`. It exports
TERMASTER_QT_ROOT and TERMASTER_QT_TOOLS, consumed by the presets. For another
installation of the same kit, override paths explicitly:

```powershell
. .\.vscode\ExportEnvironment.ps1 -QtRoot 'D:\Qt\6.12.0\msvc2022_64' -QtTools 'D:\Qt\Tools'
```

`-VisualStudioPath` optionally selects an installation instead of discovery. The
presets target Windows, Qt 6.12.0 and MSVC2022 x64; another Qt/compiler kit should
have a correspondingly named preset. Only the current PowerShell process and its
child processes receive the environment. Open a new console to start with its
original environment. Run the export script before using these presets from a
plain console; an IDE kit may initialize its own compiler environment.

## Optional tests

Test targets are opt-in and can be enabled while configuring:

```powershell
cmake --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Debug `
    -DTERMASTER_BUILD_PLUGIN_TESTS=ON `
    -DTERMASTER_BUILD_ACTION_TESTS=ON `
    -DTERMASTER_BUILD_PATH_TESTS=ON `
    -DTERMASTER_BUILD_SETTINGS_TESTS=ON `
    -DTERMASTER_BUILD_COMMAND_TESTS=ON `
    -DTERMASTER_BUILD_APP_TESTS=ON
cmake --build --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Debug
ctest --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Debug
```

The independent plugin SDK acceptance additionally needs a separately built
consumer with matching Debug/Release configuration, Qt and compiler. Supply its
DLL through TERMASTER_EXTERNAL_PLUGIN; otherwise that optional case is skipped.
Test presets report an error if no tests were enabled. Regular builds require no
serial hardware or external-network access.

For a fresh checkout without the local script, start an MSVC x64 Developer
PowerShell and set the kit variables directly before using the presets:

```powershell
$env:TERMASTER_QT_ROOT = 'C:\Qt\6.12.0\msvc2022_64'
$env:TERMASTER_QT_TOOLS = 'C:\Qt\Tools'
$env:PATH = "$env:TERMASTER_QT_ROOT\bin;$env:TERMASTER_QT_TOOLS\CMake_64\bin;$env:TERMASTER_QT_TOOLS\Ninja;$env:PATH"
```

Verified on 2026-11-08: environment script syntax, repeated export without PATH or
prefix duplication, configure/build/test preset parsing and a full MSVC Debug
build passed. All seven Debug CTest suites passed, including a separately built
Debug SDK consumer. The Release preset configured successfully; a full Release
build using that new directory was not repeated. The export script remains local
under the ignored .vscode directory by design.
