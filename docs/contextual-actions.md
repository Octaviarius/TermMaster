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

Plugin actions registered in the Terminal menu's create group use terminalCreation.
Other terminal actions use terminal. This ordering keeps all creation buttons at
the beginning regardless of plugin load order. Groups are placement slots, not
owners of domain state or command eligibility. Empty groups add no buttons.

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
main window and shares its Plugins QAction only between that window's menu and
toolbar. A plugin-context injection API and a multi-window command dispatch model
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
