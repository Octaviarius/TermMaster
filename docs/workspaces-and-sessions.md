# Workspaces and sessions

## Ownership and current scope

App owns one active Core `Workspace` and optionally one active Core `Session`.
A workspace is a single YAML file, conventionally named `*.termaster`; it owns
workspace settings, a saved-session catalog and session history. Each session owns
an independent directory and `session.yaml`, containing configured objects,
links, resources and presentation metadata. Opening a session loads configuration
only. Terminal creation, backend activation, connection execution and graph
execution remain planned.

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
  sessions:
    40b8c33d-1955-49e7-a9b7-311a67482da4:
      directory: sessions/40b8c33d-1955-49e7-a9b7-311a67482da4
  directories:
    plugins: plugins
    scripts: scripts
  state:
    recentSessions: [40b8c33d-1955-49e7-a9b7-311a67482da4]
```

The mandatory envelope uses shared [Settings](settings.md) validation and atomic
persistence. `name` is a nonblank string of at most 256 characters without NUL;
`settings` and `sessions` are mappings. Every catalog entry is a mapping with a
required string `directory`. IDs match `[A-Za-z0-9][A-Za-z0-9_.-]{0,127}`.
References must be relative children of the fixed workspace-parent `sessions/`
directory. Absolute paths, traversal and symlink escapes are rejected, including
when only a nearest existing ancestor can be resolved. A catalog folder need not
match its ID. Missing referenced folders are allowed on workspace load; opening
that session reports the concrete failure. Unknown fields are preserved.

`Workspace::sessionsDirectory()` returns the fixed workspace-local root;
ApplicationPaths has no global sessions location. `sessionIds()` lists registered
IDs; `sessionDirectory()` validates and resolves a reference. `registerSession()`
requires an existing confined directory and preserves other entry fields.
`unregisterSession()` removes catalog and history references without deleting
files. `sessions()` remains a scoped metadata view. `state()` provides a scoped
view of lifecycle state.

Optional plugin/script directory metadata resolves relative to the workspace
parent, or retains an absolute location. Opening a workspace does not create
these metadata directories, discover workspace plugins or execute scripts.
Application plugin discovery remains owned by PluginManager and ApplicationPaths.
No operation changes the process working directory.

Workspace loading validates a candidate before replacing owned state. Creation
requires an existing parent and refuses to overwrite a file. `sync()` validates
cached values before writing. New workspaces contain empty settings, catalog and
directories mappings.

## Session document

```yaml
version: 1
type: termaster.session
description: Termaster session
content:
  id: 40b8c33d-1955-49e7-a9b7-311a67482da4
  name: Amber Forest
  settings: {}
  objects:
    terminal1:
      type: terminal
      module: example
      settings: {}
  links: {}
  resources:
    example: resources/example.txt
  presentation: {}
```

All five payload sections are mappings. Objects have safe IDs, nonempty string
`type` and `module`, and a `settings` mapping. Each link has a safe ID and `source`
and `target` endpoint mappings, each containing an existing `object` ID and a
nonblank string `port`. Resources map safe IDs to relative session-contained paths;
absolute paths, invalid components and symlink escapes are rejected. Resource
contents are not executed. Presentation metadata remains separate from object
configuration. Unknown fields survive writes.

App assigns a UUID to a new session and uses it as the folder name and stable
catalog/document ID. The human-readable name is independent and mutable. Empty
creation names use `RandomNames::generate()` to select an adjective and noun.
Renaming changes the name without moving the folder or changing the ID.
Session creation refuses to replace existing data. App validates that a loaded
document ID matches its selected catalog key before activating it.

Only the current format is supported. Former embedded session definitions are
obsolete; there is no legacy reader, migration or compatibility alias. Local
development data must be prepared separately when changing formats.

## Configuration overlay

Application defaults are `application.yaml` `content.settings`. Workspace
`content.settings` overlays application defaults; an active session's
`content.settings` overlays that live workspace/application overlay.
Mappings merge recursively; higher-level scalars, lists and null replace lower
values. `App::configuration()` writes only to the highest active scope: session,
workspace or application. Removing an override reveals the inherited value.
Lower owners remain unchanged. Overlays and nodes borrow their owners, use the
same thread and become invalid when an owner is replaced or released.

## Application lifecycle

Startup restores only application `content.state.lastWorkspace`; it does not
automatically reopen a last session. Missing or invalid workspaces leave the
Open/New choice page visible with a concrete cause. Successful workspace
Open/New operations persist canonical absolute paths and update global history.

File provides workspace New/Open/Save/Close and session New/Open/Save/Close/Rename.
The loaded workspace page provides session choices and displays the active
session name, file and configuration summary. Session New/Open require a
workspace; session Save/Close/Rename require an active session. Handlers validate
the target again. No displayed object count establishes running terminals.

Opening another session validates its candidate before replacing the active
owner, saves the old session and records successful session history. Failed
loading, ID matching or persistence preserves the old active owner. Closing a
session saves before releasing its borrowed views. Workspace Save includes the
active session; workspace replacement, close and normal exit save the active
session before releasing workspace state. Write failures prevent successful
closure or replacement. Persistence is explicit, not a destructor side effect.
The last successful workspace remains available for the next startup.

## Recent workspaces and sessions

File's recent group contains `/file/recentWorkspaces` and
`/session/recentSessions`. Histories contain at most ten unique newest-first entries
and refresh when opened. Workspace entries invoke the real open operation using
global application `content.state.recentWorkspaces`.

Workspace `content.state.recentSessions` stores local catalog IDs.
`recentSessions()` returns only valid registered entries without modifying the
document. `rememberSession()` requires a registered valid entry, moves it to the
front and limits the list to ten. Malformed history types, duplicate IDs and
invalid IDs are rejected on load. Removed references are filtered when reading.
Recent session actions are enabled and open the selected saved configuration;
missing or invalid documents report an error and preserve the active session.
History is not stored in application-wide configuration.

## Verification status

Verified on 2026-11-08 with Windows x64, MSVC 19.44, Qt 6.12.0 and Ninja Debug.
The application and all affected targets built; all nine CTest suites passed,
including the independent external plugin consumer. AppTests passed ten results,
covering generated names, stable IDs and paths after rename, three-level settings,
graph/resource/presentation persistence, enabled Recent actions, separate-process
session reopening and normal-close autosave. Failure cases preserve owners and
pending edits; failed catalog persistence rolls back the new folder/reference.
Workspace Save refreshes the session heading after direct model edits.

WorkspaceTests passed 15 results and skipped its directory-symlink case.
SessionTests passed six results and skipped two symlink cases. This Windows user
cannot create those symbolic links; symlink acceptance remains unverified on
this host. Regular CTest also skips the optional action-container capture case.
No elevation or system configuration changes were performed.

Native AppTests passed all ten results at scale factors 1 and 2. Session pages
and recent-session captures were visually inspected: mutable names, configuration
counts and file paths rendered correctly. Fixtures use isolated temporary data
and synthetic object graphs. No real device or external-network traffic was
used. Release was not rebuilt for this feature.

`TERMASTER_BUILD_SESSION_TESTS`, `TERMASTER_BUILD_WORKSPACE_TESTS` and
`TERMASTER_BUILD_APP_TESTS` enable the affected test targets.

## Reusable processor definitions

Workspace `content/processorDefinitions` stores reusable named processing definitions
with a kind, source, defaults, schema and editor metadata. App publishes them into
the common processor registry when opening or changing a workspace.
Each node's unique `name` is its identity within the block. A session terminal's
`settings/processing` map stores its private Rx/Tx catalog selection and occurrence
configuration overrides; saving definitions does not require their native plugins
to be loaded. The [processing core](processing-core.md) specifies defaults,
occurrence paths, validation and independent runtime lifetimes.

The [Lineflow editor](lineflow-editor.md) applies workspace definition drafts and
refactors named occurrence overrides across registered active/closed sessions.
Owner snapshots preserve borrowed settings views and unrelated pending settings.
Staged multi-document writes use a bounded recovery journal beside the workspace;
App checks it before opening/restoring the workspace. External changes stop
preflight or recovery rather than being overwritten automatically.
