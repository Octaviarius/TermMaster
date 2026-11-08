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

Dependencies are pinned submodules under root-level `components/`. The application
entry point, resource directory and application CMake assembly are directly in
`src/`. Each application module uses this layout:

```text
src/<module>/
    CMakeLists.txt
    inc/Termaster/<ModuleName>/<Header>.h
    src/<implementation and internal directories>
```

Current modules are `core`, `managers`, `logic`, `terminal` and `widgets`;
public include names use `Core`, `Managers`, `Logic`, `Terminal` and `Widgets`.
For example, `<Termaster/Core/plugin/Plugin.h>` and
`<Termaster/Managers/SettingsManager.h>`. Include paths are target-scoped.
Existing target names and active responsibilities are preserved. Public headers
use module-qualified paths; plugin headers are grouped under `Core/plugin`.
Implementation directories such as `plugin`,
`config` and `TermWidget` remain internal. Two inactive configuration widget
placeholders remain internal and are not exposed as usable public API.

The module layout does not change the existing static-library application graph
into runtime plugins. `termaster_add_plugin` remains the explicit SHARED module
build helper. Source headers use `inc/`; assembled SDKs expose `include/`.
The YAML dependency remains at its original pinned commit after its directory
and submodule registration are moved to `components/yaml-cpp`.


The layout was verified on 2026-11-08 with a fresh MSVC 19.44/Qt 6.12.0 Ninja
Release build of the application and plugin suite. The independently rebuilt SDK
consumer and all 13 integration checks passed. Relocation checks accounted for
all tracked application files and preserved resource contents. No UI behavior
changes were introduced by the relocation.
