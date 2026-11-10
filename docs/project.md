# Termaster

Termaster is a desktop terminal application built with C++ and Qt 6 Widgets.
The root CMake project is `TermasterProject`; the application target and Windows
executable are `Termaster` and `Termaster.exe`. The Qt application name is
`Termaster`. Public module includes use `<Termaster/ModuleName/Header.h>`.

The application homepage and help links target
[the repository](https://github.com/Octaviarius/Termaster) and
[its wiki](https://github.com/Octaviarius/Termaster/wiki). Remote repository
renaming is a separate hosting operation. Existing checkout paths and Git remote
configuration are not changed by the source rename.

## Local application data

Settings use the existing settings owner and Qt's `AppDataLocation`. The new
application name selects a new application-data directory. Existing data is
left in place; no migration or compatibility alias is provided.

## Rename verification

Verified on Windows x64 with MSVC 19.44, Qt 6.12.0 and Ninja Release:

- Fresh configuration and full application build produced `Termaster.exe`.
- The application launched and exposed `Termaster` as its native window title.
  Further UI inspection was interrupted; no additional visual acceptance is claimed.
- Two existing build blockers were corrected separately: the missing return in
  `Deque::operator=` and numeric formatting of the experimental result value
  with `QString::arg`.
- Both plugin examples and their independent SDK consumers were rebuilt.
  Each plugin verifier passed all five loading cases with the updated IID.
- The rendering example was rebuilt and all 15 high-DPI captures passed the
  existing raster comparison threshold. The documentation image was refreshed.
- No CTest tests are registered by the main application configuration.

[Rendering evidence](terminal-rendering-profile.md#naming-update-verification)
and [plugin mechanism](plugins.md) describe the affected examples and their scope.


## Source organization

Dependencies are pinned submodules under root-level `components/`. PCRE2 10.49
is pinned at `6f9d7c1373262c541324a16a358785b33ef116cf`; its SLJIT submodule is pinned
at `de0259c7aaf36aa40cba8014f3fad3edde9307f9`. Initialize it with
`git submodule update --init --recursive components/pcre2`. CMake configures the
8-bit static library with JIT and never downloads source dependencies. Upstream
source files remain unmodified.

The application entry point, resource directory and application CMake assembly are directly in
`src/`. Each application module uses this layout:

```text
src/<ModuleName>/
    CMakeLists.txt
    inc/Termaster/<ModuleName>/<feature>/<Class>.h
    src/<feature>/<Class>.cpp
    inc/Termaster/<ModuleName>/Gui/<feature>/<PublicGuiClass>.h
    src/Gui/<feature>/<PublicGuiClass>.cpp
    src/Gui/<feature>/<InternalClass>.h/.cpp
    src/Gui/<feature>/<Form>.ui
```

Core, App and SerialTerminal are the active implementation modules. Core public includes use
`<Termaster/Core/Header.h>` and its implementation hierarchy mirrors that prefix
without repeating `Termaster/Core`. GUI contracts use `Gui/<feature>` under the
public include root and implementations/internal headers use `src/Gui/<feature>`.
Core includes no Managers, Logic, Terminal or Widgets module dependencies.

The obsolete Managers, Logic, Terminal and Widgets modules and the legacy entry
point were removed after checking the active CMake graph, includes and tests.
Their implementations remain recoverable from Git history. The current terminal
emulator belongs to Core; the serial backend belongs to SerialTerminal.
The current main.cpp only constructs App and calls run(). Module src/App owns
the Qt Widgets application shell, its main window, commands and orchestration.
Workspace/session persistence and disconnected terminal reconstruction are implemented.
See [terminal support](terminals-and-serial.md) for runtime boundaries.
No module-root `Gui/` directories are used.

The module layout does not change the existing static-library application graph
into runtime plugins. `termaster_add_plugin` remains the explicit SHARED module
build helper. Source headers use `inc/`; assembled SDKs expose `include/`.
fmt 12.2.1 is pinned at `208219daff58f2a7438f6931cc2ddc358acf33ef` under
`components/fmt`. Initialize it with `git submodule update --init components/fmt`.
Core links its static target; upstream code is unmodified and CMake never downloads it.

The YAML dependency remains at its original pinned commit after its directory
and submodule registration are moved to `components/yaml-cpp`.


The layout was verified on 2026-11-08 with a fresh MSVC 19.44/Qt 6.12.0 Ninja
Release build of the application and plugin suite. The independently rebuilt SDK
consumer and all 13 integration checks passed. Relocation checks accounted for
all tracked application files and preserved resource contents. No UI behavior
changes were introduced by the relocation.

[Action containers](action-containers.md) describes ordered command groups, menu
ownership and native Qt adapters.

## Mirrored GUI layout verification

On 2026-11-08, 44 files were relocated to the mirrored hierarchy. File contents
were checked byte-for-byte during relocation; include paths were then updated
and changed C++ files formatted. The plugin-window factory was extracted into
`Core/src/Gui/Plugin/PluginManagerGui.cpp` to match its public header. CMake
source existence, public header/source pairing and application-module includes
were checked automatically. Forms retained their original contents.

The MSVC 19.44 / Qt 6.12.0 Ninja Release application and all three test executables
built successfully. All three CTest suites passed, including the external plugin
SDK consumer. Native action-container and plugin-window suites passed 12 and 9
QtTest results respectively without failures or skips. Native action-window and
menu captures were pixel-identical to their pre-relocation references; the
plugin-window capture was visually inspected. No GUI behavior change is intended.

[Paths](paths.md) describes lexical POSIX/Windows values and native Qt filesystem operations.

## Active Core cleanup

Obsolete Registree, EnumReflection, Singleton, Deque and utils infrastructure was
removed from Core. Settings was replaced by a general YAML document owner with
scoped views. RandomNames is an ordinary explicitly constructed object;
BuildTime is a dedicated helper used by Version. ApplicationPaths resolves only
application locations. PluginManager borrows Settings directly and validates its
own preferences. Core links Qt Core/Widgets and pinned yaml-cpp; the active target
graph excludes the reference modules and their serial dependencies.

[Settings and paths](settings.md) describes the implemented API.
[Workspaces and sessions](workspaces-and-sessions.md) describes the implemented
single-file workspace lifecycle and planned session/terminal-group behavior.

[Application module](application.md) describes App, base windows and command/container access.

[Windows environment and Qt desktop builds](development-environment.md) documents environment export and presets.

## Archived source removal

Removed the five unbuilt trees `src/terminal`, `src/managers`, `src/logic`,
`src/widgets` and `src/legacy`. No active target, test or example depended on them.
Retained research examples, captures and protocol documentation remain useful
verification material. The unchanged active build graph was checked by a full
Debug build and the application/terminal test suites.

## Source directory naming

Project-owned directories inside `src/` use CamelCase: `App`, `Core`,
`SerialTerminal`, `Gui`, `Terminal`, `Plugin`, `Settings`, `Res` and their feature
subdirectories. The directory names `src` and `inc` stay lowercase at every level.
Public and implementation hierarchies mirror one another. Directories outside
`src/`, dependency checkouts and SDK output conventions keep their existing names.

The naming pass accounted for all 134 source files, with 131 case-only path
relocations. Exact source/public-include casing and unchanged non-C++ assets were
checked automatically. The complete MSVC/Qt Debug build, independently rebuilt
external plugin consumer and all 19 CTest suites passed. The native 1280 × 800
terminal capture matched the pre-relocation PNG byte-for-byte.
