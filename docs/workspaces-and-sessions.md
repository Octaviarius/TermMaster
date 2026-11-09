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

Reusable processor definitions are separate version 1 YAML documents under
`<workspace filename>.d/processors/`, adjacent to the workspace file. The workspace
YAML contains no processor definition payload. Each filename is the lowercase
SHA-256 hexadecimal digest of the stable ID's UTF-8 bytes, followed by `.yaml`.
This permits arbitrary valid catalog IDs, including names that differ only in
case or resemble Windows device names, without filename collisions or path syntax.
Human-readable names and groups remain metadata inside the document.

```yaml
version: 1
type: termaster.processor-definition
description: Workspace processor definition
content:
  id: receive
  name: Receive processing
  kind: lineflow
  source:
    nodes:
      - name: frame
        type: native.framer
        configuration: {}
```

The workspace owner loads the complete collection before replacing its cached
map or publishing it through App into the common processor registry. A missing
directory represents an empty collection. Up to 1024 documents and 4 MiB of total
processor YAML are supported. Malformed documents, duplicate IDs, mismatched
filenames and directory links fail the complete load without replacing the active
owner. Missing processor providers and references remain valid saved definitions.
Unknown document-envelope and definition fields, including literal keys containing
slashes, survive edits. Unchanged files retain their exact bytes. Other non-YAML
files in the directory are neither loaded as definitions nor removed. Inline
`processorDefinitions` payloads are rejected; no legacy reader or migration is
provided during development.

Each node's unique `name` is its identity within the block. A session terminal's
`settings/processing` map stores its private Rx/Tx catalog selection and occurrence
configuration overrides; saving definitions does not require their native plugins
to be loaded. The [processing core](processing-core.md) specifies defaults,
occurrence paths, validation and independent runtime lifetimes.

The [Lineflow editor](lineflow-editor.md) applies workspace definition drafts and
refactors named occurrence overrides across registered active/closed sessions.
Owner snapshots preserve borrowed settings views and unrelated pending settings.
Normal workspace saves and editor refactorings use the same bounded file transaction.
Creating, editing and removing definitions commits their individual files together
with applicable workspace and session changes. Display-name changes keep the stable
filename; definition removal deletes its file but retains unavailable references.
A journal beside the workspace records explicit original/replacement presence and
bytes, so an absent file is distinct from an existing empty file. Core load and App
check recovery before opening/restoring the workspace. Exact-byte and collection
inventory checks stop external edits or newly introduced YAML files before writes;
recovery refuses unrecognized states and retains its journal for resolution.
Empty directories can remain after a failed creation attempt; unrelated files are
never removed. Journals are claimed with an atomic exclusive reservation before
file replacement. A crash leaving an incomplete reservation blocks automatic
recovery without changing documents.
