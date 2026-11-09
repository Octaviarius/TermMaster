# Pending terminal UI requirements

## Consistent terminal toolbar

Status: requested; implementation deferred until an explicit start instruction.

Use the same control layout and ordering for all terminal types. Remove the Close
action from the toolbar to prevent accidental terminal closure. This requirement
does not request removal of terminal closing from other UI locations. Preserve
capability and state checks when applying the shared layout.

## Terminal tab context menu

Status: requested; implementation deferred until an explicit start instruction.

Add a context menu to terminal tabs. Its action list remains to be specified.

## Remove the terminal information strip

Status: requested; implementation deferred until an explicit start instruction.

Remove the extra information strip above the terminal display, such as
`COM1 / 9600 baud`, to reduce window clutter.
