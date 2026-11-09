# Quake-style dropdown console mode

Status: future requirement; implementation is not started.

Provide a Quake-style application window that drops down from the top edge of
the screen and can be shown or hidden with a configurable global shortcut.
Use the existing terminal views and backends. Hiding the window must preserve
terminal sessions and their connections; visibility and connection lifetime are
separate concerns. Keep this as a desktop Widgets presentation mode owned by App.

Before implementation, define monitor selection, window size and placement,
focus restoration, hide-on-focus-loss policy, interaction with fullscreen apps,
and coexistence with the normal main window. Platform support and global-shortcut
limitations must be explicit. Persistence belongs to the existing settings owner.

Track implementation in the [roadmap](roadmap.md#p6-quake-style-dropdown-mode).
