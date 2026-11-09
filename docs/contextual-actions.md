# Contextual application actions

## Presentation slots

Application toolbars use icons, not text-only buttons. Every toolbar action must
provide a meaningful icon and a tooltip. Icon-only presentation uses native Qt
metrics for high-DPI scaling; the same QAction may also appear in a menu with its
text. Workspace, session and plugin management remain accessible through menus.
The shared [command iconography](iconography.md) defines bundled palette-aware
icons and a 24-logical-pixel terminal toolbar.

The toolbar exposes exactly two ordered extension groups:

| Group | Scope | Context |
| --- | --- | --- |
| terminalCreation | New console, serial and socket connections | Requires a workspace/session capable of creating a terminal |
| terminal | Operations on the selected terminal | Requires a matching target, capability and state |

Plugin action roles explicitly define toolbar eligibility and ordering:
Create actions use terminalCreation, ordered by stable action ID; Connect,
Disconnect, Restart, Clear and Settings use terminal in that order. Close and
Additional actions remain menu-only. Matching operations share play, square,
rotate-cw, eraser and sliders icons across terminal backends. Backend-specific
verbs remain Start/Stop/Restart for consoles and Connect/Disconnect/Reconnect
for serial/socket connections. Groups are placement slots, not owners of domain
state or command eligibility. Empty groups add no buttons.

Right-clicking a terminal tab selects it and opens its applicable connection,
output, settings and additional commands, followed by a separated Close action.
Creation commands and actions for other terminal types are excluded. The
registered terminalType filters tab commands independently of global menu visibility. Empty tab-bar space has no menu. Commands follow
the terminal UUID rather than a tab index and revalidate the original registered
QAction before execution. Changing selection dismisses the popup; removed plugin
actions disable their popup counterparts. Close remains available in the Terminal
menu, the tab context menu and existing tab close buttons.

File retains workspace commands and Session retains session commands. Tools / Plugins
opens plugin management. These actions are absent from the terminal toolbar;
workspace/session command identities, shortcuts and enablement remain unchanged.

## Context and execution requirements

App derives workspace and session command state from its active owners,
and EditActions derives editor command state from the focused standard Qt editor.
TerminalController derives terminal presentation state from the selected terminal. Terminal connection
state and operation capabilities remain authoritative in the terminal/backend
owners. The controller must observe target selection changes, target destruction,
state transitions and capability changes, then update the affected actions.
Views and persistence do not maintain competing copies of that state.

An action unsupported by the selected terminal type/capabilities is hidden.
An applicable operation that is temporarily unavailable is disabled. For example,
connection and disconnection operations depend on both backend capability and
current connection state; terminal-specific operations must never be shown as
implemented merely because an action name or icon exists. No selected terminal
means no terminal-specific action target. Visible group separators must follow
the remaining visible actions without leading/trailing/doubled gaps.

Actions retain stable IDs, registrations and shortcuts as selection changes;
selection changes should not reconstruct the whole command registry. A command
handler resolves the active target again at execution time and validates its
current capability/state. It must not retain an obsolete raw terminal pointer
captured while the toolbar was last updated. Owned target handles, QObject weak
references and context-bound signal connections are used where appropriate.
An operation may become unavailable between presentation refresh and invocation;
execution checks are required even when the button was enabled.

Different window contexts require independent command presentation state and
shortcut scope; sharing one application's global QAction between windows must
not accidentally bind all windows to one terminal. The existing shell has one
main window; plugin management is menu-only. A plugin-context injection API and a multi-window command dispatch model
remain separate design stages.

## Implementation status

Icon-only toolbar presentation, the plugin action icon/tooltip and all four
ordered extension groups are implemented and checked by AppTests. Empty-group
separator behavior is covered by action-container tests. Session configuration
selection, persistence, rename and enabled recent actions are implemented. These
actions load configuration without starting connections. Terminal capability/state models and
terminal command dispatch remain planned. Standard Edit actions already route to
QLineEdit, QTextEdit and QPlainTextEdit using their actual capabilities; they do
not simulate terminal behavior. See [workspaces and sessions](workspaces-and-sessions.md).

Verified on 2026-11-08 with MSVC 19.44 / Qt 6.12.0 Debug in the Desktop Qt kit
build directory. All seven CTest suites passed, including the separate Debug SDK
consumer. AppTests checks toolbar icon presence, tooltips, icon-only style,
ordered groups and absence of separators for empty groups. Native AppTests passed
at scale factors 1 and 2; both main-window captures were visually inspected.
Formatting and git diff --check passed. These results predate workspace and
standard-editor command changes; they do not verify those additions.

Session actions passed Debug integration tests and native GUI checks at scales
1 and 2; see [session verification](workspaces-and-sessions.md#verification-status).

## Stage P1 verification

Verified on 2026-11-09 with Qt 6.12.0 / MSVC 19.44 Debug. The application and
all three production plugins built; all 26 configured CTest suites passed.
Synthetic plugin tests cover reconnect eligibility across states and requested
connection intent, ordered disconnect/connect dispatch and stale selection.
Application tests load plugins out of order, compare the common toolbar for all
three terminal types, verify direct display tabs without the serial header, and
exercise inactive-tab menus, tab moves, closure and plugin unload with a stale
popup action. No serial device was opened for these checks.

Native Windows checks passed at scale factors 1 and 2. Reference captures use
disconnected real plugin instances and synthetic display text, without launching
shells or opening sockets/devices. The 100% captures are 1280 by 800; at 200% the
available display constrains the window height. Toolbar and popup alignment were
visually checked at both scales:

- [Serial](images/terminal-controls/1x/serial.png)
- [Socket](images/terminal-controls/1x/socket.png)
- [Console](images/terminal-controls/1x/console.png)
- [Tab menu at 100%](images/terminal-controls/1x/TabContextMenu.png)
- [Tab menu at 200%](images/terminal-controls/2x/TabContextMenu.png)

The hosted-plugin SDK ABI is version 5. Rebuild third-party hosted plugins before
loading them; the normal application build already rebuilds the bundled plugins.
