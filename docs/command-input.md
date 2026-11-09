# Terminal command input

## Interaction and settings

Terminal tabs retain interactive keyboard input by default. The input panel can
switch to Buffered mode without reopening a connection. In Buffered mode a
multiline draft receives focus even when the display is clicked. Enter submits;
Shift+Enter inserts a newline. A failed validation or disconnected submission
retains the draft. Local echo is a separate setting and displays accepted input
locally; acceptance into an output queue does not prove device delivery.

Text commands are encoded as UTF-8. Selectable codecs are planned in
[Python integration and codecs](python-and-codecs.md). The selected terminator is appended
once after encoding: LF, CRLF, CR (default), NUL, none or custom. Custom sequences
allow literal UTF-8 and `\r`, `\n`, `\0`, `\\` and `\xHH` escapes. Unknown or
incomplete escapes reject the submission. NUL bytes remain bytes throughout.
Command submission bypasses interactive key/newline encoding and bracketed paste
wrapping; interactive display paste retains the terminal's own paste encoding.

Paste defaults to retention for editing. Pasting into an interactive display with
this policy selects Buffered mode and retains the text. Immediate display paste
uses the terminal's existing paste bytes, without appending a command terminator.
Immediate paste into the command editor submits the pasted text as one command
with the chosen terminator. Standard keyboard, Edit menu and editor context-menu
paste paths share the policy. Multiline submissions and immediate multiline paste
ask for confirmation by default, with No selected. The scoped confirmation setting
can disable the prompt explicitly. Cancelling preserves the draft and history.

Accepted command history is per live terminal, memory-only and excluded from
routine logs and settings. Up/Down recall entries while retaining the unfinished
draft; the editable history selector supports substring completion/search. Closing
the terminal discards its history. Drafts are limited to 16,384 UTF-16 units;
history is limited to 100 entries and 1 MiB of UTF-8. Oversized paste is rejected.

Input settings use the existing `input` YAML overlay and generated descriptor:
`mode`, `terminator`, `customTerminator`, `pastePolicy`,
`confirmMultiline` and the existing `localEcho`. Controls update only the active
terminal override; application/workspace/session generated forms provide inherited
values. Normal existing session saves persist those overrides. Settings and custom
terminators are not written by the widget.

## Output ownership and lifecycle

App's terminal runtime owns a GUI-thread Core `TerminalSendQueue` borrowing its
backend. All command, interactive keyboard/paste, mouse/focus and terminal-reply
bytes use this queue, preserving submission order and individual write boundaries.
The queue retains at most 4 MiB and 4,096 submissions; each prepared command is
also limited to 4 MiB. The command editor imposes its tighter draft limit first.
Stream backends may accept a prefix; zero accepted bytes means temporary
backpressure. The remainder retries through a bounded event-loop timer without
blocking the GUI. Serial and TCP transport queues remain limited to 1 MiB; UDP
accepts each datagram atomically, including empty datagrams, and rejects datagrams
larger than 65,507 bytes. A full UDP slot queue explicitly rejects an empty
datagram because zero accepted bytes otherwise means successful empty-message
acceptance. Separate submissions are never concatenated.

Disconnected submissions are rejected with a concrete status message. Any
non-open state, explicit stop, asynchronous failure, terminal close or owner
shutdown cancels pending output. Reconnection never replays it. Partial transport
acceptance cannot be rolled back; failure can therefore leave a prefix delivered.
This stage provides no offline retention, file sender, pacing or automatic replay.
Backend failures are reported through the existing status channel. Local echo
occurs when the application queue accepts user input; replies are never echoed or
fed back into received input. Callback reentrancy is guarded against reset,
replacement and deletion while a backend write is running.

## Verification

Synthetic tests cover exact UTF-8/terminator/custom bytes including embedded NUL,
invalid custom escapes, output bounds, partial and zero acceptance, asynchronous
failure, disconnect/reopen cancellation, separate submission boundaries and
callback reset/deletion. Widget tests cover accepted/rejected submissions,
history navigation/search, terminator selection, paste policy through the standard
paste slot, multiline cancellation and oversized paste. The App fixture verifies
buffered focus/first-key behavior, byte-exact partial sends, reconnect cancellation
and terminal replies through the shared queue without display echo.

No real serial device, external network target or elevation is used for command
input acceptance. Native visual captures use a synthetic backend at 1100 × 760
logical pixels; the terminal renderer itself is unchanged.

The 100% and 200% native captures show [buffered input](images/command-input/Buffered.png),
[buffered input at 200%](images/command-input/BufferedScale2.png),
[interactive input](images/command-input/Interactive.png) and
[interactive input at 200%](images/command-input/InteractiveScale2.png).
Updated [input settings](images/command-input/Settings.png) and
[input settings at 200%](images/command-input/SettingsScale2.png) show the
terminator and paste controls after removal of the HEX toggle.

Acceptance on 2026-11-09 used the existing Debug build directory
`build/Desktop_Qt_6_12_0_MSVC2022_64bit-Debug` after sourcing
`. ./.vscode/ExportEnvironment.ps1`. The complete application build passed and
all 28 configured CTest suites passed (47.68 seconds), including command input,
editor, App lifecycle and saturated empty-UDP queue checks. Changed C++ files were
formatted with the repository clang-format configuration. Local documentation
links and `git diff --check` passed. Native Windows fixture runs passed at both
scales; captures measure 1100 × 760 and 2200 × 1520 physical pixels respectively.
