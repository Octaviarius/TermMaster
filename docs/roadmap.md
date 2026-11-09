# Development roadmap

This is the single milestone checklist for pending work. Priority applies to the
requirements collected in [terminal UI requirements](terminal-ui-requirements.md)
and [process terminal planning](process-terminal.md), not an audit of the entire
application. Stages P1 and P2 are complete; later stages await a separate start instruction.
Items within each stage are ordered; later stages depend on relevant earlier work.

## P1: Consistent terminal controls

- [x] Unify terminal toolbar control layout/order across backends and remove Close.
- [x] Add terminal tab context menus; connection/output/settings commands and separated Close.
- [x] Remove the redundant information strip above the terminal display.
- [x] Add Start all / Stop all to the Terminal menu and guarantee tab-menu Close.

These small changes address accidental closure and visual clutter first.
Preserve backend capability/state checks and verify the affected GUI.

## P2: Essential command input

- [x] Provide an editor and buffered send-on-submit mode, distinct from local echo.
- [x] Add byte-safe terminators: LF, CRLF, CR, NUL, none and custom sequences.
- [x] Add paste policy and confirmation for multiline sending.
- [x] Add command history with Up/Down navigation and search.
- [x] Add HEX input with validation before sending.

Establish one submission path that handles partial writes, backpressure and
outgoing conversion without duplicating terminators. Verify exact bytes with
synthetic transports, including embedded NUL. Keep existing interactive input
available for shells.

Implemented contracts, limits and verification are recorded in [command input](command-input.md).

## P3: Settings presentation

- [ ] Match reset button height to its adjacent control.
- [ ] Show synchronized color codes and color samples in shared x-widget editors.
- [ ] Style the settings window after the supplied Qt Creator reference, add
      navigation icons and retain the hierarchical tree and search.
- [ ] Add an application-wide keyboard shortcut editor with command search and
      persistent shortcut assignments.

These changes can be implemented independently of command submission. Verify
alignment and rendering at supported display scales before marking them complete.

## P4: Advanced sending

- [ ] Add named favorite commands with per-command terminators.
- [ ] Add bounded, cancellable sending with character/line delays.
- [ ] Add file sending as raw bytes or line by line, using the same send pipeline.
- [ ] Add explicit disconnected-send policy: reject or retain a bounded queue.
- [ ] Add periodic sending with an active indicator and a stop command.

Share ordering, cancellation and queue ownership with the P2 submission path.
Design reconnect/replay behavior explicitly before combining queues and timers.

## P5: Process terminal

- [ ] Implement a separate plugin for launching processes through stdio pipes.
- [ ] Separate process lifetime from its view; support reattachment to retained
      Termaster-owned endpoints and distinguish detach from terminate.
- [ ] Add an asynchronous process picker with filtering and process identity checks.
- [ ] Investigate attachment options for existing external processes and document
      the supported scope before exposing each method.
- [ ] Research native-agent/API-hook attachment on controlled fixtures, including
      output observation, input, asynchronous I/O and safe detach.

Ordinary process pipes come before invasive attachment research. Hooking remains
experimental and must not delay the usable launch mode. Detailed lifecycle,
platform limitations and verification boundaries are in the process planning doc.

## P6: Quake-style dropdown mode

- [ ] Add a Quake-style dropdown console window with a configurable global
      show/hide shortcut, preserving existing terminal sessions while hidden.

See [dropdown mode requirements](quake-console.md). Monitor, focus and normal-window
behavior must be defined before implementation. This is a future presentation
mode, not a new terminal backend.
