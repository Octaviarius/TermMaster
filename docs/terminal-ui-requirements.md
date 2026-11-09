# Pending terminal UI requirements

## Consistent terminal toolbar

Status: implemented in stage P1; see [roadmap](roadmap.md).

Use the same control layout and ordering for all terminal types. Remove the Close
action from the toolbar to prevent accidental terminal closure. This requirement
does not request removal of terminal closing from other UI locations. Preserve
capability and state checks when applying the shared layout.

## Terminal tab context menu

Status: implemented in stage P1; see [roadmap](roadmap.md).

Right-clicking a tab selects it and shows its applicable connection, clear,
settings and additional backend actions, with Close separated at the end.
Creation actions are excluded. Commands revalidate the target terminal identity.

## Remove the terminal information strip

Status: implemented in stage P1; see [roadmap](roadmap.md).

Remove the extra information strip above the terminal display, such as
`COM1 / 9600 baud`, to reduce window clutter.

## Settings window presentation

Status: requested; implementation deferred until an explicit start instruction.

Use the supplied Qt Creator Preferences screenshot as the visual reference for
the settings window. Add meaningful icons to the navigation tree while retaining
the existing hierarchical category/page structure rather than a flat category
list. Preserve settings search/filtering and the page area on the right.

## Visible color samples in settings

Status: requested; implementation deferred until an explicit start instruction.

Color editors must show both the editable color code and a visible sample of the
actual color at the same time. Keep the sample synchronized with valid manual
code edits and color-dialog selection. Apply this to foreground/background and
other settings using the shared color editor selected through x-widget.

## Send after completing input

Status: implemented in stage P2; see [command input](command-input.md).

Add an input option to buffer typed command text locally and send it after input
is completed, for example on Enter, instead of transmitting each typed character
immediately. This is useful for AT commands. Keep this behavior distinct from
local echo and configured outgoing line-ending conversion. A dedicated multiline editor uses Enter to submit and Shift+Enter for a newline.

## Outgoing command terminator

Status: implemented in stage P2; see [command input](command-input.md).

Provide a configurable outgoing command terminator alongside buffered command
input. Support LF (`\n`), CRLF (`\r\n`), NUL (`\0`) and other terminators.
Represent terminators as bytes so embedded NUL is preserved. The chosen
terminator completes the submitted command; coordinate this with outgoing
newline conversion to avoid appending or converting it twice. Buffered input and
terminator selection are separate options.

## Reset button height

Status: requested; implementation deferred until an explicit start instruction.

The value reset button must have the same height as its adjacent settings
control, with aligned top and bottom edges. Apply this consistently to the
shared settings editor layout.

## Additional command input features

Status: history, editor, terminators and paste policy implemented in
stage P2; favorites, file/delayed/periodic sending and retained disconnected queues
remain planned in P4. See [command input](command-input.md).

- Command history with Up/Down navigation and search.
- Editing before submission through a dedicated single-line or multiline editor.
- Extend terminator choices with CR (`\r`), no terminator and a custom sequence.
- Selectable stream codecs, including HEX conversion, are planned; see
  [Python integration and codecs](python-and-codecs.md).
- Named favorite commands, each with its own terminator.
- File sending as raw bytes or line by line, with cancellation.
- Configurable delays between characters and between lines for slow receivers.
- Periodic command sending with a visible active indicator and a stop button.
- Paste policy: send immediately or retain for editing; confirm multiline sending.
- Explicit disconnected-send policy: reject sending or retain a bounded queue
  until reconnection. Never silently discard or replay pending commands.

These features extend the buffered input and terminator requirements above.
Keep queued data and file transfer bounded, respect transport backpressure and
cancel timers/pending sends when their owner shuts down. Keep command history
and favorites out of routine logs; persistence details remain to be designed.

## Global keyboard shortcut editor

Status: requested for the settings stage; not part of stage P1.

Add a keyboard shortcut editor to application-wide settings. Use the supplied
Qt Creator reference for a searchable command list showing command identity,
human-readable label and assigned shortcut. Allow editing command shortcuts and
persist the assignments in the global application configuration. Further reset,
conflict handling and import/export behavior remains to be designed.

## Bulk terminal connection commands

Status: implemented as a stage P1 follow-up.

The Terminal menu exposes Start all and Stop all for open terminals of the
current session. Keep these commands out of the toolbar and per-tab menus.
Stopping clears requested connection state and cancels automatic retries while
retaining the tabs. The tab menu always offers Close, including for a backend
that registers no presentation actions.
