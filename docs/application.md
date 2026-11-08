# Application module

`src/app` owns the application shell and orchestration. The executable target
remains Termaster; App is the static application module and Core is reusable
infrastructure. Neither the old Widgets/Managers modules nor their bootstrap are
restored to the active graph. Core does not depend on App.

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
and invoke run once. The central widget is intentionally empty; workspace/session
loading is not implemented by this shell.

## Access to commands and containers

App provides `commands()`, `mainMenu()`, `toolbar()`, `mainWindow()`, `settings()`
and read-only `paths()`. Returned references are borrowed for App's lifetime;
commands themselves remain valid until removed from CommandRegistry. No new
singleton or process-wide mutable command registry is introduced.

```cpp
auto* command = app.commands().create("Example.Action", "Example");
if (command)
{
    auto* action = command->newAction(&app.mainWindow());
    auto* tools = app.mainMenu().findMenu("/tools");
    if (tools && tools->addGroup("example"))
        tools->addAction(action, "example");
}
```

Public consumers include the corresponding Core command/container headers to
call their APIs. CommandRegistry owns registrations, validates bounded immutable
case-sensitive IDs and rejects duplicates. Removing a command destroys its
remaining actions; ActionContainer automatically drops those registrations.
Contributors must remove their owned menu groups and unregister their commands
before unloading their code. Passing App to plugin startup is not added by this
change; plugin context exposure remains a separate contract decision.

The shell registers Application.Plugins and Application.Exit. File/Exit exits the
application. Tools/Plugins and the toolbar share one QAction and open the same
modeless plugin window. Closing that dialog deletes it; another invocation
creates a new one. QAction handlers perform application operations through App;
MainWindow only constructs the window and native container wrappers.

Application settings use application.yaml and plugin preferences retain
plugins.yaml. App checks settings/plugin startup failures and displays causes in
the window status bar. Settings writes remain explicit; startup does not save
preferences merely by reading them.

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

## Verification

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
disabled in this build. Workspace/session behavior remains planned.
