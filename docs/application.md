# Application module

`src/app` owns the application shell and orchestration. The executable target
remains Termaster; App is the static application module and Core is reusable
infrastructure. The obsolete Widgets/Managers modules and their bootstrap have been removed. Core does not depend on App.

## Entry and structure

```cpp
#include <Termaster/App/App.h>

int main(int argc, char* argv[])
{
    App app(argc, argv);
    return app.run();
}
```

App derives from QApplication for Qt framework integration, sets application
metadata before resolving standard paths, and owns its components. The module
uses the established mirrored include/source layout:

```text
src/app/
    CMakeLists.txt
    inc/Termaster/App/App.h
    src/App.cpp
    src/gui/main/MainWindow.h
    src/gui/main/MainWindow.cpp
```

Base application windows and dialogs belong to App's `src/gui/<feature>`, with
internal headers and forms beside implementation. Shared GUI contracts, if
needed, use `inc/Termaster/App/gui/<feature>`. The concrete MainWindow is internal;
App exposes a QMainWindow reference. Core retains its plugin-management window
and action-container implementations, which are feature GUI of Core.

App::Options optionally supplies data/plugin directories for isolated launch or
embedding. Defaults retain the current ApplicationPaths locations. The owner,
commands, windows and all accessors are used on the GUI thread. Construct one App
and invoke run once. The central area presents workspace Open/New choices or
the active workspace name/path, session choices and loaded session configuration.
Recognized terminal objects open as disconnected tabs through registered plugin factories;
link/resource graph execution remains planned. See [terminal support](terminals-and-serial.md).

## Access to commands and containers

App provides `commands()`, `mainMenu()`, `toolbar()`, `mainWindow()`, `settings()`
and read-only `paths()`. Returned references are borrowed for App's lifetime;
commands themselves remain valid until removed from CommandRegistry. No new
singleton or process-wide mutable command registry is introduced.

```cpp
auto* command = app.commands().create("Example.Action", "Example");
auto* action = command->newAction(&app.mainWindow());
auto* tools = app.mainMenu().findMenu("/tools");
tools->addGroup("example");
tools->addAction(action, "example");
```

Public consumers include the corresponding Core command/container headers to
call their APIs. CommandRegistry owns registrations, validates bounded immutable
case-sensitive IDs and rejects duplicates. `create()` returns a valid command
or throws: invalid IDs raise `std::invalid_argument`; duplicate IDs or an
unavailable registry raise `std::logic_error`. Container add/insert registration
also throws rather than returning a success flag. Movement/removal retain bool
results. Registration failures are programming errors, not silently skipped actions. Removing a command destroys its
remaining actions; ActionContainer automatically drops those registrations.
Contributors must remove their owned menu groups and unregister their commands
before unloading their code. Hosted plugins receive the optional PluginHost SDK contract before startup;
Core remains independent of App.

## Workspace and standard menus

App exposes `workspace()`, `configuration()`, `restoreLastWorkspace()`,
`openWorkspace()`, `createWorkspace()`, `saveWorkspace()` and `closeWorkspace()`.
The active workspace is owned by App; `workspaceChanged` reports successful
changes. [Workspaces and sessions](workspaces-and-sessions.md) describes the
single-file format, settings overlay and save/restore contract.

File provides New/Open/Save/Close workspace and Exit. Its Recent group contains
separate Workspaces and Sessions submenus, each limited to ten entries.
`recentWorkspaces()` returns the global history; workspace entries perform real
Open operations. Enabled session entries open saved configuration from the current
workspace catalog.
See the [history contract](workspaces-and-sessions.md#recent-workspaces-and-sessions). Save/Close are enabled only
with an active workspace, and handlers validate their target again. Startup
restores the last workspace or shows the Open/New choice page. Replacement,
closure and normal exit persist pending state and report concrete failures.

App also exposes `session()`, `createSession()`, `openSession()`, `saveSession()`,
`renameSession()` and `closeSession()`. `sessionChanged` reports active-session
changes. Session New/Open require an active workspace; Save/Close/Rename require
an active session. UUID identity and folder location remain stable across a
human-readable name change. Empty creation names use a random adjective/noun.
The workspace page displays configuration metadata; recognized terminal objects
open as disconnected tabs, without executing arbitrary link/resource graphs. Session state is saved before workspace replacement,
close and normal exit. Startup restores the workspace without reopening a session.

`configuration()` exposes a live borrowed application/workspace/session overlay,
writing only to the highest active scope. Session and workspace views expire when
their owners are released. Session integration passed Debug tests and native GUI
checks at scales 1 and 2; see
[session verification](workspaces-and-sessions.md#verification-status). Earlier
verification sections below describe their original scope.

Edit provides Undo, Redo, Cut, Copy, Paste and Select All with standard shortcuts.
Its internal EditActions controller targets the focused QLineEdit, QTextEdit or
QPlainTextEdit in the active main window. Enabled state follows read-only state,
selection, undo/redo availability, content and clipboard capabilities. Execution
resolves the focused editor again. These are real Qt editor operations; terminal
input and terminal selection route through TerminalDisplay's own encoder and
clipboard operations. Terminal-focused Copy/Paste/Select All use the same menu,
while terminal ShortcutOverride preserves program input such as Ctrl+C and Tab.

View provides Toolbar, Status bar and Full screen. Tools/Settings opens the
generated settings dialog for the active application, workspace or session scope.
Its schema and form behavior are described in [settings forms](settings-forms.md).
Toolbar/status preferences use
`ui/toolbarVisible` and `ui/statusBarVisible` through the active configuration
scope. Tools/Plugins and the toolbar share one QAction and open the same modeless
plugin window. Closing the dialog deletes it; another invocation creates a new
one. Help provides About Termaster and About Qt. MainWindow constructs views and
native containers; App sequences application operations.

Application settings use `application.yaml` under Qt AppDataLocation with the
mandatory `termaster.application` envelope. `content.settings` holds defaults;
`content.state.lastWorkspace` holds the last successfully opened absolute path,
and `content.state.recentWorkspaces` holds unique canonical absolute paths in
newest-first order. Successful Open/New persist both together.
Plugin preferences use `plugins.yaml` with type `termaster.plugins`. Loading does
not write merely because a file was read. These current formats have no legacy
reader or migration.

## Lifetime and command fixes

App owns settings, PluginManager, CommandRegistry and the main window. Plugin
shutdown and PluginManager destruction run while command/container/settings
owners still exist, including a retry after incomplete shutdown. The main
window and its child dialogs are destroyed before the command registry; borrowed
Qt actions are weakly tracked, so widget-first destruction is safe. QApplication
outlives all of these resources.

Command now tracks actions through QPointer snapshots when updating/deleting,
rejects action creation during destruction, preserves QString Unicode, accepts
an optional QObject owner and keeps its registration name immutable. An empty
initial shortcut uses the supplied default; explicit disabling uses setShortcut
with an empty sequence after construction. Shared-action mode remains an explicit
API with command-owned action lifetime. The shell uses one action in both native
presentations, avoiding duplicate shortcut registrations for those presentations.
Full context routing, command-level checked/enabled state and shortcut preference
persistence are not implemented by this stage.

## Earlier verification

Enable TERMASTER_BUILD_COMMAND_TESTS and TERMASTER_BUILD_APP_TESTS for the new
suites. CommandTests exercises Unicode, shortcuts, shared actions, action/parent
lifetimes, registry validation and reentrant destruction. AppTests uses temporary
settings/plugin directories and an actual App event loop: it checks the shell,
shared menu/toolbar action, dialog reuse/recreation and resource destruction after
App exits. Set TERMASTER_APP_GUI_CAPTURE for native main/plugin-window captures.
All checks use synthetic/local data; no real terminal backend is activated.

Verified on 2026-11-08 with Windows x64, MSVC 19.44, Qt 6.12.0 and Ninja Release:
Termaster, App and all affected test targets built. All seven CTest suites passed
without skips, including 8 CommandTests and 4 AppTests results plus the independent
plugin SDK consumer. Native AppTests passed all four results at scale factors 1
and 2; main-window and plugin-dialog captures were visually inspected. Source
layout, public header/source pairing, active dependency boundaries, documentation
links, clang-format and git diff --check were verified. SerialBus/SerialPort remain
disabled in this build. Session configuration loading was not part of that stage. These historical results
predate the workspace lifecycle and standard-menu changes and do not verify them.

[Contextual actions](contextual-actions.md) defines icon-only toolbars and scoped extension groups.

## Application icon

App uses the approved orange asset from assets/IconOrange.png as its global Qt
window icon through `:/Termaster/App/IconOrange.png`. The App static module owns
the resource; main and child windows inherit the application icon. On Windows,
the executable additionally embeds assets/IconOrange.ico through the configured
AppIcon.rc resource template under src/app/res. Both original assets are used
unchanged; the ICO supplies seven native sizes from 16 through 256 pixels.

Verified on 2026-11-08 in the Desktop Qt MSVC Debug build: all seven CTest suites
passed, native AppTests passed at scale factors 1 and 2, and the Qt-rendered icon
was visually inspected. The compiled executable's icon group contains all seven
images matching the approved ICO bytes exactly. The extracted native icon was
also visually inspected. The blue asset remains unassigned.

## Workspace verification

The workspace implementation was verified on 2026-11-08 in the Desktop Qt MSVC
Debug kit. All eight CTest suites passed. Native AppTests passed all eight
results at scale factors 1 and 2; workspace pages and both recent menus were
visually inspected. Separate-process checks covered last-workspace restoration,
normal-close autosave and recovery from a missing last file. Persistence tests
used isolated temporary application data. See
[workspace verification](workspaces-and-sessions.md#verification-status) for scope
and remaining runtime limitations.

## Generated preferences verification

The Tools/Settings action uses the reusable schema form against the active
application/workspace/session settings scope. Verified on 2026-11-08 with MSVC
19.44 and Qt 6.12.0 Debug: all eleven CTest suites passed, and native AppTests
passed eleven results at scales 1 and 2. The generated preferences and custom
model-backed editor captures were visually inspected. See
[settings forms](settings-forms.md#verification-status) for limits and checks.

## Preferences page catalog

The internal SettingsDialog owns category/page navigation, search and page drafts.
App registers real Appearance pages for application, workspace and session scopes,
then adds explicitly isolated preview pages from YAML resources under
`src/app/res/settings/`. SerialPortPreviewEditor and its example model belong to
App's `gui/settings` feature. The application-owned widget registry outlives dialogs;
its preview factory is registered once per App instance. No serial API is used.
See [preferences navigation](settings-forms.md#preferences-navigation-and-preview-pages).

## Terminal runtime

[Terminal support](terminals-and-serial.md) describes SerialTerminal, the selected-tab
controller, the Terminal menu, five-layer configuration and unload lifetimes.
Application, workspace and session settings include real Terminal defaults pages;
a selected tab adds its own connection and display pages. Production serial
creation uses the plugin port selector rather than the isolated preview model.

The terminal runtime uses the project-owned [VT widget](terminal-emulation.md).
Outgoing user events and protocol replies carry distinct kinds; local echo uses
separate display data. Future script processing inserts before/after this endpoint
rather than entering the renderer or serial backend.
