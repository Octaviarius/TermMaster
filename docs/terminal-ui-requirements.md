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

Status: requested; implementation deferred until an explicit start instruction.

Add an input option to buffer typed command text locally and send it after input
is completed, for example on Enter, instead of transmitting each typed character
immediately. This is useful for AT commands. Keep this behavior distinct from
local echo and configured outgoing line-ending conversion. The exact editor
presentation remains to be specified.
