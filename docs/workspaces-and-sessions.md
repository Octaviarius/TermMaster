# Workspaces and sessions

## Ownership and current scope

Termaster opens one workspace at a time. Core `Workspace` owns a single YAML
file, conventionally named `*.termaster`. App coordinates its lifecycle and
presents the current name and path. A workspace owns settings and a saved-session
mapping. A session is a group of configured terminals; its schema, menu/catalog,
loading, switching and connection activation remain planned. Stored session
metadata does not establish an implemented terminal backend.

## Workspace document

```yaml
version: 1
type: termaster.workspace
description: Termaster workspace
content:
  name: Example
  settings:
    ui:
      toolbarVisible: true
  sessions: {}
  directories:
    plugins: plugins
    scripts: scripts
```

The envelope is mandatory and uses the shared [Settings](settings.md) validation
and atomic persistence. There is no legacy reader or migration. `name` must be a
nonblank string of at most 256 characters without NUL. `settings` and `sessions`
must be mappings. Optional `directories` must be a mapping; present `plugins` and
`scripts` entries must be nonempty path strings without NUL. Unknown fields are
preserved. New workspaces contain empty settings, sessions and directories maps.

Directory paths resolve relative to the workspace file's parent, or retain their
absolute location. They are metadata only: opening a workspace does not create
these directories, discover or activate workspace plugins, or execute scripts.
Application plugin discovery remains owned by PluginManager and ApplicationPaths.
Session paths belong to the workspace; ApplicationPaths has no global sessions
directory. No operation changes the process working directory.

`Workspace::load()` validates a candidate before replacing owned state. Creation
requires an existing parent directory and refuses to overwrite an existing file.
`sync()` validates cached domain values before writing. `sessions()` exposes a
borrowed Settings node; `settings()` exposes a borrowed SettingsOverlay. Owners
and views use the same thread, and views must not outlive their owners.

## Configuration overlay

Application defaults are stored in `application.yaml` at `content.settings`.
The active workspace's `content.settings` recursively overlays those defaults.
Mappings merge; workspace scalars, lists and null replace base values. Writes
through the overlay affect only workspace overrides. Removing an override reveals
its base value. Application defaults remain unchanged. Without an open workspace,
`App::configuration()` reads and writes the application settings scope.

## Application lifecycle

Startup attempts to restore the absolute path in application
`content.state.lastWorkspace`. With no saved path, or when restoration fails, the
main window presents Open and New workspace choices; a failed restore includes
its cause. File provides New, Open, Save, Close and Exit. The file chooser uses
`*.termaster` by default. Save and Close require an open workspace.

Opening or creating a replacement saves the old workspace before switching and
records the new path in application data. A failed load, save or pointer write
does not replace the active workspace. Close saves before releasing the workspace
and returns to the choice page. The last successful path remains available for
next startup. Window close and normal application exit save pending workspace and
application settings; write failures are reported and prevent a successful close.
Persistence is explicit rather than a destructor side effect.

## Recent workspaces and sessions

File has a `recent` group with separate Workspaces and Sessions submenus at
`/file/recentWorkspaces` and `/file/recentSessions`. Each history is limited to ten
unique entries, newest first. Menus refresh when opened.

`App::recentWorkspaces()` returns the global workspace history. Successful
Open/New operations persist canonical absolute paths in application
`content.state.recentWorkspaces` together with `content.state.lastWorkspace`.
Opening an existing history entry uses the real workspace-open operation, with
the same validation, save and error handling. Malformed history lists are rejected.

`Workspace::recentSessions()` returns that workspace's session history from
`content.state.recentSessions`, filtering out removed entries and entries whose
saved-session value is no longer a mapping. `rememberSession(id, error)` requires
an existing direct session-map key that is nonempty, at most 256 characters and
contains no NUL. It prepends the ID, removes duplicates and limits the stored list
to ten. Optional workspace `state` must be a mapping; malformed recent-session
lists are rejected. Session history is local to its workspace rather than global
application data.

Recent session entries are visible but disabled, with a tooltip explaining that
session loading is not implemented. History persistence does not load a session
or activate terminals.

## Planned terminal behavior

The application coordinator will load selected session definitions into configured
terminal groups. Session selection belongs to the workspace's menu/catalog.
Persistence, transport I/O, emulation/screen state and rendering/input retain
separate owners. Shutdown must stop operations before destroying borrowed state.
Closing a running group must not delete its saved definition. Session validation,
terminal configuration editing and terminal-group loading are not delivered by
the workspace lifecycle implementation.

## Verification status

Verified on 2026-11-08 with Windows x64, MSVC 19.44, Qt 6.12.0 and Ninja Debug.
The application and affected tests built; all eight CTest suites passed. The
independent external plugin consumer ran successfully. The optional container
capture case was skipped by regular CTest and passed separately with the native
Windows platform (all 12 container results passed).

WorkspaceTests passed 13 results, covering recursive overrides, borrowed-view
lifetimes, validation, no-clobber creation, write failures, metadata directories
and recent-session bounds/preservation. AppTests passed eight results, including
separate-process restoration, normal-close autosave, missing-file recovery,
failed-switch preservation, editing capability routing, scoped View settings
and recent workspace ordering/deduplication/limits. SettingsTests passed 13
results including required metadata, unknown-field preservation and aliases.

Native AppTests passed all eight results at scale factors 1 and 2. Welcome,
loaded-workspace and recent-menu captures were visually inspected; ten workspace
entries and disabled saved-session entries rendered correctly. Verification used
temporary application data and synthetic sessions, without real devices or
external-network traffic. Release was not rebuilt for this feature.
