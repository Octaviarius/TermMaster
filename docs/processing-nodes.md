# Configurable processing nodes

Built-in processing implementations live in `src/Core/src/Processing/Nodes`.
Each node owns its algorithm, state, schema and factory: Gate, Framer, HexEncoder,
HexDecoder, Base64Encoder and Base64Decoder. NativeProcessors only registers the
catalog entries. Codec nodes share bounded synchronous invocation plumbing, not
an algorithm dispatcher or mutable state.

Descriptors expose `group` as a trimmed optional string of at most 128 characters,
with no NUL. Saved workspace definitions use the same field. An empty group is
presented as General. The catalog groups by origin (Native / Workspace), then
by this subgroup. Group headings are expandable and cannot be selected as
operations or dragged. Searching matches origin, subgroup, name and ID. Workspace
block groups can be edited beside the block selector and remain drafts until Apply.

## Gate

`native.gate`, group Control, has `enabled: true` by default. Enabled input is
forwarded with its bytes and packet/stream boundary unchanged. Disabled input
completes successfully with zero outputs: traffic is dropped, never queued for
later release. It therefore does not stall a Lineflow. Limits still apply while
disabled. Cancellation stops acceptance. Configuration changes follow the common
prepared-instance replacement contract rather than mutating another instance.

## Framer

`native.framer`, group Framing, accumulates arbitrary byte chunks and emits zero,
one or many complete Packet frames. A stream read is never a frame boundary.
State survives split delimiters and UTF-8 is not decoded from incoming data.

| Parameter | Default | Behavior |
|---|---|---|
| mode | delimiter | delimiter, size or either rule |
| frameSize | 64 | Raw bytes per frame in size/either mode |
| ending | `\r` | Byte pattern with `\r`, `\n`, `\0`, `\xHH`, `\\` escapes; literal text is UTF-8 |
| includeEnding | false | Keep a matched ending in the emitted frame |
| emitEmpty | true | Preserve empty frames produced by the rules |
| packetTail | preserve | preserve, emit, discard or error for incomplete input-packet tails |
| maxFrameSize | 1048576 | Accumulation limit, clamped to the runtime byte bound |

In either mode the first completed ending or raw-size boundary emits a frame.
Ending bytes count toward raw size; a complete ending wins a tie. Removing an
ending occurs only when the ending rule won. The decoded ending is limited to
256 bytes. Matching is incremental and linear, including overlapping patterns.

By default even an incoming Packet boundary does not flush accumulated bytes.
The emit tail policy explicitly emits remaining bytes there; an entirely empty
packet emits an empty frame only in that policy with emitEmpty enabled. It does
not add a duplicate empty frame after a rule already emitted during the same call.
Discard clears the residual and partial delimiter match; error rejects a nonempty
residual. Stream chunks do not apply tail policy. Cancellation discards residual
state; the Processor contract has no implicit end-of-stream flush.

## HEX

- `native.hex-encoder`: letterCase is upper (default) or lower; separator defaults
  to one space and accepts at most 16 Unicode characters of literal UTF-8 text.
  Separators occur between bytes across StreamChunk calls, never at the beginning
  or end of a packet. Packet completion resets separator state.
- `native.hex-decoder`: ignoreWhitespace defaults to true and ignores ASCII
  space, TAB, CR, LF, VT and FF. Both letter cases decode. Non-hex bytes are errors;
  a split nibble survives stream reads but an odd nibble at Packet end is rejected.

Both entries belong to Codecs. Non-whitespace encoder separators require a
separate transformation before the strict HEX decoder.

## Base64

- `native.base64-encoder`: alphabet is standard or url, padding defaults true,
  lineLength defaults zero (no wrapping) or a multiple of four up to 4096, and
  lineEnding is lf or crlf. Stream reads emit complete three-byte units and retain
  at most two trailing bytes. Packet completion emits the final unit and optional
  padding, then resets wrapping and accumulation state.
- `native.base64-decoder`: alphabet is standard or url, ignoreWhitespace defaults
  true and requirePadding defaults true. Parsing preserves quartets across reads.
  Invalid alphabet bytes, misplaced/incomplete padding, data after padding and
  noncanonical unused bits are errors. With requirePadding false, a two/three-digit
  tail may finish at Packet end; a one-digit tail is always invalid.

Both entries belong to Codecs. A padded Base64 value terminates that packet stream;
data after padding requires a new packet boundary. An empty valid Packet is
preserved. Stream chunks never imply padding or finalization. Put a Framer before
an encoder/decoder when input is a continuous transport stream requiring explicit
message boundaries. Cancellation is not a request to flush or encode a tail.

## Limits and configuration

Each implementation receives independent state and resolved schema defaults.
Factories reject malformed options and guard input, output and persistent state
against runtime bounds. A processing error stops that instance and suppresses all
outputs from the failing invocation. Previously completed invocations are not
rolled back. Output and error data own their storage. Reentrant input during an
output callback returns WouldBlock; completion releases the invocation slot.

Schemas are consumed by the existing generated settings forms and `x-widget`
registry. Numeric settings persist as numbers and booleans as booleans. Unknown
configuration is preserved. Current development IDs are native.gate and
native.framer; Identity and the fixed CR-only node are removed without aliases.
HostedPlugin IID 9 includes the descriptor group field; native modules must be
rebuilt against the generated SDK.

## Verification

Verified on 2026-11-09 with Qt 6.12.0 / MSVC x64 Debug. The application and native
modules built successfully; all 42 configured CTest targets passed (72.82 s).
After the requested name-column refinement, affected App and editor targets passed
again (27.84 s combined). Node tests cover all 256 byte values, incremental splits,
multiple/empty frames, binary and overlapping endings, packet policies, canonical
Base64 bits/padding, malformed data/configuration, output/state bounds, cancellation,
reentrant input and destruction during callbacks. Registered Lineflow roundtrips
and a disabled Gate verify zero-output completion without blocking downstream.

Metadata tests verify group type/length/ownership, raw-storage deep copies,
publication atomicity and change notifications. GUI acceptance checks both origin
roots, non-draggable subgroup headings, subgroup editing, filtering, real encoder
and Gate controls, and stable name/class column proportions after graph resets.
The name column stretches; the class column starts compact, is resizable and is
capped to 40 percent of the viewport. Full names remain available in tooltips.

Native Windows screen-fit, column-reset and visual checks passed at scales 1 and 2
(five QtTest passes without skips per scale). HEX and Framer captures use logical
1100 by 760 client areas and were visually inspected. The separately clean-built
SDK consumer passed its explicit acceptance with three passes and no skips.
Changed C++ files were clang-formatted; documentation links, captures and diff
checks passed. No real-device or external endpoint acceptance was performed.

- [HEX encoder and grouped catalog](images/lineflow-editor/Editor.png)
- [Framer settings](images/lineflow-editor/Framer.png)
- [Framer at scale 2](images/lineflow-editor/Framer2x.png)
