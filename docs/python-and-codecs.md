# Python integration and stream codecs

Status: planned; no Python runtime or configurable codec pipeline is delivered
by this requirement. Milestones are tracked only in the [roadmap](roadmap.md).

## Python integration

Integrate Python for application scripting and user-authored byte-stream codecs.
Define interpreter and module ownership, loading/unloading, callback lifetimes,
error reporting and cancellation before implementation. Execution must preserve
GUI responsiveness; thread affinity, execution budgets and shutdown behavior
require explicit design. Do not imply that arbitrary Python code is sandboxed.

## Stream codecs and packet input

Provide configurable encoding and decoding of incoming and outgoing byte streams.
Codecs may be implemented in C++ or Python through the same behavioral contract.
C++ codecs must remain usable without initializing or installing Python.

Apply codecs to buffered packet input as well as transport streams. A submitted
payload must be encoded into its intended packet before transmission. Initial
examples are wrapping outgoing payloads in SLIP frames, decoding incoming SLIP
frames, and converting between raw bytes and hexadecimal text. HEX encoding and
HEX decoding are distinct operations. Codec selection will replace the removed
HEX command input toggle; command text currently uses UTF-8 without a selectable
codec.

Define codec ordering, direction, parameters and explicit packet/stream modes.
Specify whether terminators belong to the payload before framing, so framing and
terminator conversion cannot be applied twice. Preserve individual submission
boundaries through preparation and the shared bounded send queue; transport read
chunks and partial writes do not establish packet boundaries.

Keep codec processing outside TerminalDisplay and transport implementations.
Preserve separate keyboard, paste, mouse, focus and terminal-reply output kinds,
and display-only local echo. Decoded input and generated replies must not create
feedback loops. See [terminal processing boundaries](terminal-emulation.md) and
[command submission and output ownership](command-input.md).

Stateful decoders must retain incomplete frames and escape sequences across
arbitrary read boundaries, including embedded NUL. Bound pending input, frame
size and output expansion. Define invalid-input handling, end-of-stream behavior,
reset on disconnect, error propagation, backpressure and cancellation. Shut down
codec work before destroying its interpreter, plugin factories or borrowed owners.

Acceptance must use synthetic streams for split SLIP delimiters/escapes, multiple
frames in one read, empty frames, invalid HEX, truncated input, resource limits,
partial writes, disconnect/reopen and cancellation. No real-device transmission
is required for default verification.
