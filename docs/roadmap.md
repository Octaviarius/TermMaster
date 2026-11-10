# Development roadmap

This is the single milestone checklist for pending work. Priority applies to the
requirements collected in [terminal UI requirements](terminal-ui-requirements.md)
and [process terminal planning](process-terminal.md), not an audit of the entire
application. Stages P1 through P4 and the FLOW-1 core are complete; remaining integration stages
await a separate start instruction.
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
- [x] Add paste policy and direct multiline sending without an extra confirmation.
- [x] Add command history with Up/Down navigation and search.

Establish one submission path that handles partial writes, backpressure and
outgoing conversion without duplicating terminators. Verify exact bytes with
synthetic transports, including embedded NUL. Keep existing interactive input
available for shells.

Implemented contracts, limits and verification are recorded in [command input](command-input.md).

## P3: Settings presentation

- [x] Match reset button height to its adjacent control.
- [x] Show synchronized color codes and color samples in shared x-widget editors.
- [x] Style the settings window after the supplied Qt Creator reference, add
      navigation icons and retain the hierarchical tree and search.
- [x] Add an application-wide keyboard shortcut editor with command search and
      persistent shortcut assignments.

These changes can be implemented independently of command submission. Verify
alignment and rendering at supported display scales before marking them complete.

Delivered behavior and verification are recorded in [settings forms](settings-forms.md#settings-presentation-and-global-shortcuts).

## P4: Advanced sending

- [x] Add named favorite commands with per-command terminators.
- [x] Add bounded, cancellable sending with byte/line delays.
- [x] Add file sending as raw bytes or line by line, using the same send pipeline.
- [x] Add explicit disconnected-send policy: reject or retain a bounded queue.
- [x] Add periodic sending with an active indicator and a stop command.

Share ordering, cancellation and queue ownership with the P2 submission path.
The explicit reconnect/replay contract, byte-pacing semantics and verification are
recorded in [command input](command-input.md#advanced-sending).

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

## P7: Python integration

- [ ] Integrate Python for application scripting and Python-authored stream codecs.
- [ ] Define interpreter ownership, script/module lifetime, error reporting and
      execution limits without blocking the GUI.

See [Python integration and stream codecs](python-and-codecs.md). Implementation
is deferred until an explicit start instruction.

## P8: Stream codecs and packet input

- [ ] Add configurable encoding/decoding of incoming and outgoing byte streams.
- [ ] Support codec implementations in C++ and Python through a shared contract;
      native codecs must not require the Python runtime.
- [ ] Apply codecs to buffered packet submissions through the shared send pipeline.
- [ ] Provide SLIP packet encoding/decoding and HEX conversion as initial examples.
- [ ] Add codec selection for command input in place of the removed HEX toggle.
- [ ] Verify split frames, malformed input, bounded buffering, backpressure and
      cancellation with synthetic streams.

Python-authored codecs depend on P7; native codec work can proceed independently.
See [codec requirements](python-and-codecs.md#stream-codecs-and-packet-input).

## FLOW-1: Processing core

- [x] Deliver the shared asynchronous Processor contract and native factory registry.
- [x] Persist reusable ProcessorBlocks in the workspace without shared runtime state.
- [x] Prepare and execute bounded Lineflow compositions with cancellation and plugin leases.
- [x] Verify synthetic processing and a real native plugin lifecycle.

The delivered scope and execution contract are recorded in [processing core](processing-core.md).

## FLOW-2: Terminal processing lifecycle and routing

- [x] Define routing of byte chunks versus packets, outgoing input kinds and local
      echo. Command terminators must not be appended to protocol replies.
- [x] Define bounded receive ingress and transport backpressure behavior before
      connecting asynchronous processors to live input; report overflow explicitly.
- [x] Add an App-owned coordinator with independent Rx and Tx instances, supporting
      either a workspace Lineflow block or one registered Processor per direction.
- [x] Route device input through Rx to the display and outgoing data through Tx to
      the existing bounded transport queue, preserving ordering and datagram boundaries.
- [x] Add pass-through selection and safe replacement: cancel/quiesce the old runtime,
      handle buffered state explicitly and prevent old output reaching the replacement.
- [x] Verify chunk splits, delayed output, multiple results, reconnect, replacement,
      queue saturation and plugin unload using synthetic transports.

This stage makes processing usable without requiring the full editor. It must not
silently change protocol reply, paste, keyboard, packet or local-echo semantics.

## FLOW-3: Unified processor registration

- [x] Extend the common catalog to include native processors, saved workspace
      Lineflow blocks and saved code processors through the same registration contract.
- [x] Register a validated definition when an editor saves it and repopulate workspace
      entries when opening a workspace; expose catalog changes to selectors and editors.
- [x] Provide a stable identifier, display name, defaults, configuration schema,
      editor metadata and an instance factory for each entry, independently of origin.
- [x] Create independent Processor instances on demand; registration must not execute
      user code or create a running processing chain.
- [x] Resolve nested entries through the catalog and reject cyclic compositions.
- [x] Define update/removal and provider shutdown: preserve unavailable references,
      keep active instances safe and apply definition changes through explicit replacement.

No dedicated processor plugin is required. Any plugin may register processors;
workspace and code editors publish their definitions into the same catalog.
P8 remains the codec capability checklist.

## FLOW-4: Workspace Lineflow editor

- [x] Replace prototype-only tree contents with workspace block definitions and the
      registered processor catalog; provide block creation, naming, duplication and removal.
- [x] Preserve drag/drop ordering and nested block insertion with cycle checks.
- [x] Generate selected-node forms from schemas and x-widget registrations; support
      plugin-owned editors without coupling them to runtime instances.
- [x] Keep edits as drafts with explicit Apply/Cancel and validate before persistence.
- [x] Add node rename refactoring that handles affected occurrence overrides and
      references, including closed sessions; preflight failures must not leave partial changes.
- [x] Preserve unavailable processor definitions visibly and explain why execution
      is unavailable without silently replacing or removing those nodes.
- [x] Verify editor state, nested reuse, renaming, persistence failures and plugin
      removal, with native visual acceptance at supported display scales.

The delivered editor and persistence acceptance are recorded in
[Lineflow editor](lineflow-editor.md). No freely connected graph or ControlFlow
nodes are part of Lineflow.

## FLOW-4.1: Configurable nodes and catalog groups

- [x] Replace placeholder native operations with independent Processing/Nodes implementations.
- [x] Add Gate enabled by default, configurable Framer and HEX/Base64 encoder/decoder pairs.
- [x] Preserve stream state across reads and enforce packet finalization, strict decoding and bounds.
- [x] Add descriptor group metadata and Native/Workspace subgroup presentation with search.
- [x] Keep operation names wider than processor classes across model resets and user resizing.
- [x] Verify binary splits, schemas, lifecycle, grouped forms and native 1x/2x captures.

Configuration and byte-boundary contracts are specified in
[processing nodes](processing-nodes.md).

## FLOW-5: Terminal selection and private properties

- [x] Add compact independent Rx/Tx selectors for a block, elementary processor or bypass.
- [x] Persist selections and private configuration in each terminal's session settings.
- [x] Show properties in a side table, grouped by node name in expanded Lineflow order;
      distinguish repeated nested occurrences and expose the default/inherited value.
- [x] Reset individual overrides without modifying the shared workspace definition.
- [x] Apply changes through a prepared replacement plan; do not mutate an active runtime.
- [x] Show node-path errors, busy/cancelling state and bounded diagnostic counters
      without recording payloads by default.

- [x] Store reusable definitions in individual workspace-owned YAML files with
      bounded create/edit/delete transactions and recovery.

See [terminal processing](terminal-processing.md) for UI and acceptance details.
Private values resolve over workspace node configuration and processor defaults.
Rx/Tx and different terminals must remain independent even when sharing a block.

## FLOW-6: Codeflow

- [ ] Build on the Python ownership and execution work tracked in P7; define an
      explicit processing/control context without granting implicit transport ownership.
- [ ] Implement Codeflow as another Processor composition, accepting bytes and
      producing asynchronous results under the same limits and cancellation contract.
- [ ] Expose registered native processors and Lineflow blocks to scripts through
      owned instance handles, with defined async waits, ordering and error propagation.
- [ ] Add code editing, validation and diagnostics, preserving saved source/configuration
      separately from live script state and worker/interpreter resources.
- [ ] Verify cancellation, plugin/interpreter shutdown and bounded expansion without
      representing arbitrary script execution as sandboxed.

Codeflow can be selected on Rx/Tx or used inside Lineflow through the common
Processor contract. It does not add ControlFlow nodes to the Lineflow editor.

FLOW-2 routing, FLOW-3 registration, FLOW-4 workspace editing and FLOW-5 terminal properties are delivered.
The remaining processing stage is FLOW-6. Editors depend on the unified catalog definition registration
and update contracts. Python execution remains part of P7/FLOW-6.
The earlier unrelated P5/P6 milestones do not block this processing sequence.

## FLOW-7: Processing timing and compact terminal controls

- [x] Move byte/line delays and rate limiting into the asynchronous native Pacer node.
- [x] Stream Lineflow output between busy stages under bounded resident queues.
- [x] Preserve packet atomicity, cancellation, partial writes and bounded file ingress.
- [x] Keep mode, terminator, draft and Rx/Tx selection compact; move history and sending tools into popups.
- [x] Verify synthetic timing/lifecycle tests and native 1x/2x rendering.

See [processing nodes](processing-nodes.md) and [terminal processing](terminal-processing.md).

## FLOW-8: Variable processor ports and paired Lineflow

- [x] Add validated processor port metadata and owner-thread asynchronous lifecycle output.
- [x] Execute 1x1/2x2 Lineflow, nested shared nodes and independent Rx/Tx traversal.
- [x] Replace the operation tree with a native Qt palette/canvas/properties editor.
- [x] Support shape-aware drag/drop and movable, resizable, persistent dock panels.
- [x] Select one shared 2x2 flow per terminal with atomic persistence and cancellation.
- [x] Verify binary routing, cross-port responses, idle output, bounds and native 1x/2x rendering.

- [x] Name processor ports and display input/output direction at node connectors.
- [x] Flip a shared node's port pairs with persisted occurrence metadata and runtime parity.

## FLOW-9: Compact Lineflow table

- [x] Use compact tinted rows, centered names/classes and numbered stages.
- [x] Align endpoints, wires and border-centered port chevrons on shared lane axes.
- [x] Add inline 180-degree rotation while preserving port identity and draft settings.
- [x] Generate meaningful unique occurrence names, with no suffix on the first instance.
- [x] Verify affected behavior, native 1x/2x captures and the Debug application build.

## FLOW-10: Duplex codecs

- [x] Register HEX Codec and Base64 Codec as native 2x2 nodes with named ports.
- [x] Expose independent encoder/decoder settings with usable defaults.
- [x] Reuse unary codecs while preserving separate incremental state and packet boundaries.
- [x] Verify bounds, reentry, cancellation, rotation and native settings forms.

## FLOW-11: Terminal-local processing and workspace restoration

- [x] Filter Lineflow palettes and terminal selectors by the current topology.
- [x] Restore each workspace's last session after terminal factories are available.
- [x] Move buffered-input/file/processing controls to status-bar actions and the Terminal menu.
- [x] Retain independent Bypass/Simplex/Duplex profiles and terminal-local Inplace diagrams.
- [x] Edit local diagrams with the existing Lineflow editor and prepare them in isolated registry scopes.
- [x] Preserve atomic saves, private occurrence refactoring, plugin leases and callback lifetimes.
- [x] Verify synthetic routing/persistence, failure cases, native 1x/2x layouts and affected builds.

## APP-12: Main-window shutdown

- [x] Exit after an accepted main-window close independently of auxiliary windows.
- [x] Preserve persistence-error close veto and normal resource destruction.
- [x] Verify bounded subprocess exit, open-console shutdown and affected builds.

## APP-14: Processing faults and terminal input controls

- [x] Preserve transport connections and requested state on processing errors.
- [x] Keep failed lanes blocked and stop Tx producers without resetting processors.
- [x] Show sending controls only for file or periodic production.
- [x] Add a checkable per-terminal Local echo toolbar/menu action.
- [x] Verify failure isolation, recovery, selection state and native Windows controls.

## FLOW-12: Packet processing contract

- [x] Use whole byte packets for all processor invocations and remove boundary modes.
- [x] Preserve packets through ingress, routers and compositions without implicit splitting.
- [x] Finalize codecs per packet and make HEX pair accumulation explicitly optional.
- [x] Keep Framer as an explicitly buffering stage and Pacer as configured timing transformation.
- [x] Rebuild hosted modules and verify packet boundaries, lifecycle and affected application tests.

## FLOW-13: Delimiter and timed framers

- [x] Simplify delimiter framing to a terminator and optional byte count, default zero.
- [x] Add idle-gap framing with asynchronous output during silence.
- [x] Preserve host memory bounds, cancellation and callback lifetime contracts.
- [x] Verify direct nodes, compositions, terminal routing and native settings forms.

## FLOW-14: Streaming regex benchmark

- [x] Verify native MSVC build compatibility and record upstream blockers.
- [x] Build pinned PCRE2-8 and Vectorscan with the same MinGW Release toolchain.
- [x] Validate binary packet boundaries and group extraction, including DFA continuation.
- [x] Measure repeated isolated throughput, component memory, process memory and compilation.
- [x] Record reproducible scripts, raw data and decision limitations without selecting a production engine.
