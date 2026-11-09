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
`confirmMultiline`, `localEcho`, `favorites`, `byteDelayMs`, `lineDelayMs`,
`disconnectedPolicy` and `periodMs`. Controls update only the active
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

Disconnected user submissions are rejected by default. The explicit Retain policy
keeps only untouched user submissions within the same 4 MiB / 4,096 limits.
Unexpected disconnect drops a submission's entire remainder once any prefix was
accepted; accepted prefixes are never replayed. A single queued status notification reports
the discarded submission and byte counts without including contents. Replies, focus and mouse reports
are never retained or paced. Explicit Stop, connection Stop, terminal close and
shutdown clear all pending output. Changing effective transport settings cancels
pending output with a concrete message, preventing replay to a different endpoint.
Partial transport acceptance cannot be rolled back; failure can leave a prefix
delivered. Untouched retained submissions resume only on the same live terminal's
reconnection and are never persisted.
Backend failures are reported through the existing status channel. Local echo
occurs when the application queue accepts manual user input; replies are never echoed or
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

## Advanced sending

The icon beside command input opens a compact optional panel. It is collapsed by
default, including for interactive shells. Named favorites store command text,
terminator and custom escaped terminator separately. Loading a favorite edits the
draft; it does not send. Save replaces a matching name while preserving unknown
entry fields. Favorites are limited to 100 entries, 16,384 UTF-16 units per command
and 1 MiB of serialized UTF-8. They are scoped settings rather than command history;
application, workspace, session and terminal overlays use the existing settings
owner. The `commandFavorites` x-widget provides a table and multiline editor in
generated forms. No commands or file contents enter routine logs.

Byte delay and line delay range from 0 to 60,000 ms. Byte pacing deliberately
operates on raw bytes and may split UTF-8 characters. Stream writes honor partial
acceptance and backpressure. Line pacing recognizes CR, LF and a combined CRLF
boundary; line-file submissions explicitly mark their logical boundary even when
the selected terminator is NUL, custom or empty. Protocol output keeps FIFO order
without acquiring pacing or retention. Datagram submissions stay atomic and reject
byte pacing. Raw file sending uses explicit datagrams of up to 65,507 bytes on UDP;
line files use one datagram per logical line and reject oversized datagrams.

File sending requires an explicit file chooser followed by confirmation of raw or
line mode. Raw mode sends unchanged bytes without a terminator. Line mode does not
decode text: CR, LF and CRLF delimit logical byte lines, including empty lines and
embedded NUL. Each logical line gets the selected terminator exactly once. A final
nonempty unterminated line also gets the terminator; an empty file produces no
output and a trailing delimiter produces no extra line. CRLF spanning read chunks
remains one boundary. Files are read in at most 64 KiB per event-loop turn. A line
is bounded to 4 MiB including its terminator, and the application output queue
retains its normal bound. Large files are not loaded as a whole. Stop and disconnect
close the file immediately; files never resume automatically. Already accepted
transport bytes cannot be recalled.

Periodic sending snapshots the current command bytes and pacing, shows an active
indicator, and exposes Stop even with the advanced panel collapsed. Intervals range
from 100 ms to 24 hours. A new interval begins after the previous application-queue
submission drains, so backpressure does not accumulate repeated commands. Only one
file or periodic producer can run per terminal. Periodic work stops on disconnect
and does not restart after reconnect or application startup. Pacing changes affect
new jobs; current producer options are snapshots. Stop clears manual retained
submissions as well as file and periodic work.

The main application remains unelevated and owns files, producer timers, scoped
settings and lifecycle orchestration. Core's borrowed-device queue owns submission
ordering and bounded pacing only. All producer work is cooperative on the GUI
event loop; there are no background services, new IPC paths or device commands in
verification. File reads are bounded synchronous local reads; a slow filesystem
can delay the individual read call. Acceptance into transport memory is not proof
of receiver delivery.

P4 acceptance on 2026-11-09 used the same Debug toolchain and environment export.
The complete application and bundled hosted plugins rebuilt successfully against
ABI 6. All 30 configured CTest suites passed (58.34 seconds). After final API-bound,
atomic-capability revalidation and disconnect-notification changes, AppTests, AdvancedSenderTests and
CommandInputTests passed again (23.97 seconds). Synthetic checks cover files larger
than the queue limit, CRLF read boundaries, empty/NUL lines, unterminated EOF,
oversized lines and terminators, pacing, cancellation, error/reopen behavior,
manual-only retention, target-configuration changes, periodic non-overlap and
callback-triggered cancellation/deletion. Changed C++ files were clang-formatted;
local documentation links and `git diff --check` passed.

Native Windows fixture runs passed at 100% and 200%. The advanced panel shows a
selected named CRLF command and active periodic status in
[the 100% capture](images/command-input/Advanced.png) and
[the 200% capture](images/command-input/AdvancedScale2.png). The custom favorites
settings editor is shown at [100%](images/command-input/Favorites.png) and
[200%](images/command-input/FavoritesScale2.png). Captures were visually inspected
and their physical dimensions verified as 1100 × 760 and 2200 × 1520. Existing
buffered, interactive and input-settings captures were refreshed. No hardware,
external network target, download, elevation or installed service change was used.
