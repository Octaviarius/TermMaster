# Python processors and Codeflow

Status: proposed design, not a delivered Python API. The examples below describe
an SDK to implement. Native processors, Lineflow, generated settings and Tap are
already delivered. Implementation stages are tracked only in the
[roadmap](roadmap.md#flow-6-codeflow).

## Product model

Python is the execution language/provider. Codeflow is a reusable Processor
implemented by a Python class; it is not a second terminal model. Codeflow can be
placed inside Lineflow or Tap and can expose a paired terminal-compatible 2x2
interface. Native processing remains usable without Python.

A script author subclasses `Processor`, declares `Property`, `Input` and `Output`
descriptors, and attaches methods with `@handles`. No handwritten JSON Schema,
YAML manifest or Qt code is required for script parameters or ports. The SDK
extracts an immutable description, from which Core generates the existing settings
schema and port metadata. App retains persistence and orchestration ownership.

Implementation boundaries: a Python processing plugin owns interpreter discovery,
SDK/runtime integration and Python-specific editing support. Core keeps the generic
Processor contract, descriptor validation and reusable settings UI without a Python
dependency. App owns workspace source persistence, publication and execution
orchestration. Plugins use the host SDK rather than linking a static Core copy;
editor/runtime factories retain their owners through destruction and cancellation.

## Proposed author API

```python
import zlib
from termaster import Processor, Property, Input, Output, handles


class ZlibCodec(Processor):
    level = Property(
        int, default=6, minimum=0, maximum=9,
        title="Compression level", group="Compression",
        help="Compression level used for outgoing packets.",
    )

    rx_in = Input(bytes, title="Compressed", flow="rx")
    rx_out = Output(bytes, title="Payload", flow="rx")
    tx_in = Input(bytes, title="Payload", flow="tx")
    tx_out = Output(bytes, title="Compressed", flow="tx")

    @handles(rx_in, output=rx_out)
    def decode(self, data: bytes):
        return zlib.decompress(data)

    @handles(tx_in, output=tx_out)
    def encode(self, data: bytes):
        return zlib.compress(data, level=self.level)
```

Each placement creates an independent instance. Fields such as `self.level` expose
typed, validated configuration, rather than descriptor objects. State belongs to
that instance; module globals must not become the supported state-sharing API.
Optional `start()` and `stop()` hooks initialize and release runtime resources.
They may be async. The base class binds configuration, context and ports before
calling `start()`; authors do not need a custom constructor for ordinary scripts.
An optional class decorator can supply display title, group and help text; the
class name is the default processor title.

A handler receives owned bytes for one whole input packet. Its return contract is:

| Return/action | Meaning |
| --- | --- |
| `bytes`, `bytearray`, `memoryview` | One packet on the decorator's output; copy mutable/borrowed data into owned storage. |
| `b""` | One explicit empty packet. |
| `None` | No output for this input; useful for filtering or explicit buffering. |
| Bounded `list`/`tuple` of byte buffers | Several distinct packets on the declared output. |
| `self.other_out.emit(data)` and return `None` | Explicit routing, fan-out or unsolicited output on a declared port. |
| `str`, arbitrary objects, mixed emit-plus-return | A concrete contract error; no implicit text encoding or duplicate publication. |

Generators and async generators are deferred until a bounded streaming contract
exists. Async handlers use the same result contract. Expansion, packet count,
pending bytes and time budgets apply to both returns and explicit emissions.
`self.ctx` supplies task-local packet information, scoped logging, cancellation,
owned timers and declared child processors. It does not implicitly expose transport
ownership, terminal widgets or a QApplication. Callback data must not be borrowed
from Qt buffers after an invocation completes.

## Properties and variants

Descriptors use familiar Python types and optional constraints:

```python
from pathlib import Path
from termaster import Property

prefix = Property(str, default="", title="Prefix")
enabled = Property(bool, default=True)
width = Property(int, default=2, minimum=1, maximum=8, step=1)
ratio = Property(float, default=1.0, minimum=0.0)
letter_case = Property(str, default="upper",
                       choices={"upper": "Uppercase", "lower": "Lowercase"})
polynomial = Property(int, default=0x1021, minimum=0, maximum=0xFFFF,
                      widget="hexInteger", title="Polynomial")
output_file = Property(Path, default="packets.bin", mode="save")
working_directory = Property(Path, default=".", kind="directory")

encoding = Property(str, default="fixed",
                    choices={"fixed": "Fixed", "varint": "Varint"})
fixed_width = Property(int, default=2, minimum=1, maximum=8,
                       when=encoding.is_("fixed"))
max_bytes = Property(int, default=10, minimum=1, maximum=10,
                     when=encoding.is_("varint"))
```

Python Enum classes can supply choices with typed enum values. Titles default to
readable sentence case from attribute names; explicit titles preserve acronyms.
Group, order, help, read-only presentation and registered widget hints map to the
existing form contract. Path fields use the shared picker and retain raw relative
configuration; the runtime Path value resolves from the workspace directory.
Color editing can use a string Property with `widget="color"`. Complex dictionaries
and lists use the existing YAML/custom editor contract initially.

The first conditional-property implementation supports one finite string/enum
selector with equality conditions, matching existing oneOf capabilities. It does
not promise arbitrary computed visibility or execute Python getters during paint.
Unsupported conditions receive an explicit descriptor error. Numeric configuration
continues to honor Core's exact numeric validation range. Python data processing
itself can use arbitrary-size integers.

Attribute names are stable parameter IDs. Changing a title does not rename stored
configuration. Values remain immutable within an active revision initially; Apply
prepares a replacement instance/graph. Live setters and change hooks are a later
extension, not a second mutation path into settings persistence.

## Ports and direction

Input/Output specify port direction. `flow="rx"` and `flow="tx"` specify the
logical terminal lane, not the Python object's direction or current visual side.
Rx goes from device to display, Tx from terminal to device. The example therefore
decodes Rx and encodes Tx within one 4-pole node. Canvas flipping changes physical
placement using the existing routing contract; it must not swap handler meanings.

Descriptor attribute names are stable IDs, while `title` labels the data carried
at the port. The initial release supports 1x1 and paired 2x2 Codeflow shapes in the
current Lineflow canvas. Every input requires one handler. Omitted handling is a
validation error; an explicit pass-through template is available.

Initial port payload type is `bytes`, matching the common Processor contract.
Text, JSON or frames may be documented as content hints, but dictionaries or
strings must not silently cross native byte ports. General N-input/M-output shapes
and structured typed links require separate graph/editor work; Core's larger port
count allowance alone does not establish delivered canvas support.

## Codeflow composition

Control flow stays ordinary Python: conditions, loops, state and async waits.
Registered native processors and reusable Lineflow blocks become declared child
handles, rather than additional control-flow cards in Lineflow:

```python
from termaster import Processor, Input, Output, Node, handles


class DecodeAndFilter(Processor):
    incoming = Input(bytes)
    outgoing = Output(bytes)
    decoder = Node("native.base64-decoder", alphabet="standard")

    @handles(incoming, output=outgoing)
    async def process(self, data: bytes):
        packets = await self.decoder.process(data)
        return [packet for packet in packets if packet]
```

The shown `Node.process` shorthand is for 1x1 children and returns a bounded list
of byte packets, including zero outputs. A multiport child uses explicit named
ports/results. Declarations provide dependency IDs to the existing provider
preparation API; factories and plugin leases are retained before execution.
Undeclared arbitrary runtime registry access is not a supported escape hatch.

Native children stay on the composition's existing processing worker. Python
awaits their asynchronous completion; no GUI blocking, nested Qt event loops or
thread-per-node policy is introduced. Different input ports may have pending
async invocations with FIFO/backpressure per port. Shared state can interleave at
await points; no concurrent Python threads mutate an instance by default. Waiting
for another packet on an already occupied input must not deadlock admission: such
protocols need explicit buffering/state or owned background tasks that release
the input invocation. Cancellation stops admission, timers and child requests and
waits for native quiescence before releasing provider leases.

## Runtime ownership and the required execution-mode decision

A worker thread is insufficient to guarantee a deadline for arbitrary Python and
native extension calls. Embedded CPython cannot safely force-stop a blocked C
extension, recover the application from its crash, or unload arbitrary extension
modules. Per-node subinterpreters do not provide general module compatibility.

The recommended production option is a normal, version-pinned CPython execution
process owned by an active processing graph, with one selected environment. It is
not a persistent service or a replacement App backend. Codeflow instances in the
same graph share that host but own separate state. Independent graph hosts can use
different environments and execute independently of the interpreter GIL. Mixed
environments in one graph are rejected initially rather than spawning unbounded
per-node hosts. Killing a timed-out host faults that graph; transport remains open.

This option introduces local IPC and an execution helper, which is an architectural
exception to the current repository rule. It is a recommendation requiring an
explicit implementation decision; this planning work does not authorize or create
that infrastructure. The alternative is an embedded Python plugin with cooperative
cancellation and no hard-stop guarantee for native calls. The first implementation
stage must settle this choice before runtime infrastructure is written.

With process execution, Core owns a bounded asynchronous Processor adapter;
App selects environments and resource context; the Python module owns interpreter
objects and its SDK. A dedicated bounded binary channel carries IDs, port IDs,
owned byte packets, output frames, completion and cancellation. Do not use pickle
for host messages, base64 for bulk bytes, or mix control frames with stdout.
Requests carry revision/invocation generations; stale responses are discarded.
Native module writes to stdout/stderr remain diagnostics, not protocol traffic.

One host can batch transfers while preserving each packet's boundary. Native-only
paths do not start Python. Performance checks must measure startup, cold imports,
small-packet latency, throughput, paired flow fairness and cancellation; a Python
loop per byte must not be presented as equivalent to a vectorized/native codec.

Lifecycle: stopped -> starting -> running -> stopping -> stopped, with faulted and
timed-out outcomes. Limits cover startup/import, packet execution, queued work,
output bytes/packets, diagnostics and stop grace. An application-side watchdog
remains independent of Python execution. Cooperative stop runs hooks and cancels
owned async tasks first; a process host can then be terminated after its grace
period. Owned child-process cleanup must be part of that host's lifecycle.
OS memory limits are exposed only where implemented. A hard Python-host stop does
not forcibly stop C++ native children; their existing cancellation contract remains
authoritative. File/network effects already performed by a script cannot be rolled
back. Arbitrary modules execute with normal user permissions, not in a sandbox.

## Module loading and environments

Support the standard library, workspace packages and installed third-party
packages, including compatible native extensions. No module allowlist is proposed.
Compatibility still depends on Python ABI, architecture, platform libraries and
package requirements; "any module" must not mean every wheel works in every runtime.

Use the selected interpreter/venv explicitly. Installing packages uses that
interpreter's `-m pip`, never an unrelated pip found on PATH. Package installation
is an explicit environment action, not configuration-time CMake downloading or an
automatic consequence of opening a workspace. Downloads must use physical network
adapters as required by the project. Dependencies are not installed by this plan.

Local script packages support standard relative imports such as
`from .helpers import decode`. Give each script package an isolated import namespace
so two scripts' `helpers.py` files do not alias accidentally. Third-party module
state may still be shared within one graph host; instance isolation is not module
isolation. A fresh graph host is the safe import-reset boundary; do not use
`importlib.reload` as a universal native-module unloading strategy.

Cache compilation by source revision and runtime version; compile/import outside
the per-packet path. Repeated packets reuse an instance. Restart/Apply creates a
fresh approved revision and resets its state. Entry source is pinned; arbitrary
dynamic imports or external file reads cannot be promised as immutable snapshots.

## Descriptor discovery and publication

Class decorators and inherited Properties require loading Python classes. Module
imports can run arbitrary top-level code even without constructing an instance.
Therefore catalog browsing, workspace opening and syntax highlighting must not
import a script simply to paint its name or properties.

A deliberate Check/Apply/Add action loads the module in the selected discovery
runtime, collects inherited descriptors without instantiating Processor, and
returns plain bounded metadata. Import errors or timeouts include file/line
tracebacks. Static syntax checking can run automatically without imports.

Workspace catalog YAML retains a validated metadata/source-digest snapshot;
occurrence values stay in existing definition/session configuration layers.
Metadata is derived from the class, not a separately hand-maintained user schema.
Provider validate checks the snapshot and structure without running user code;
prepare may compile but cannot create live runtime instances. Runtime startup
compares the actual descriptor with the approved snapshot before accepting data.
Changed ports/schema require explicit republication; they cannot mutate a live
Processor's shape. Failed checks retain existing drafts and the running revision.

## Storage and naming

Managed defaults:

```text
lab.termaster
lab.termaster.d/
  processors/<safe-catalog-id>.yaml
  scripts/<script-id>/
    main.py
    helpers.py
  python/environments/<environment-id>/
```

The scripts directory is workspace-owned. Stable IDs determine directories and
catalog filenames; display names never become paths. New editor templates are
named Codeflow 1, Codeflow 2, etc.; the template class is `Codeflow`. A named class
can supply the base display title with numeric occurrence suffixes, consistent
with the existing naming policy. Do not use random word names.

Linking an existing `.py` file anywhere in the filesystem is supported by design.
Store an explicit external reference, absolute or workspace-relative, and do not
copy/move it silently. Provide Save As / Copy into workspace / Open containing
folder. An unavailable script retains its catalog identity, path and cached
metadata with a concrete diagnostic. It does not silently become Bypass.

Main source editing is a draft. Managed source/metadata saves extend the existing
bounded workspace transaction. External source uses conflict detection and atomic
single-file replacement; a cross-volume external source plus workspace catalog
update cannot be promised as one filesystem-atomic transaction. Pin the validated
revision before activation and report partial save failures concretely. Relative
resource paths and Property(Path) values use the workspace directory; Python
package imports use script/package location. Do not change process-wide cwd.

## Editor and interaction

Use a dedicated Codeflow editor opened by double-click or the existing node mini
button. Keep the terminal display/connection alive when its editor opens. Layout:
source in the center; a compact Properties/Ports preview; a Test pane with separate
input, output and diagnostics. Show file, dirty state, active revision, Python
environment and running/faulted status. Toolbars group Save, Check, Test, Apply and
Restart/Stop, with shortcuts local to the editor rather than terminal key input.

Prefer a mature Qt code editor with a Python lexer (QScintilla is the leading
candidate); verify its Qt 6.12/MSVC build, Unicode/DPI behavior, dependency size and
license before pinning it. The current project is GPLv3; the exact dependency
license still requires review. QPlainTextEdit plus QSyntaxHighlighter remains a
standard-Qt fallback if the ready-made integration is unsuitable. No custom text
renderer or embedded web application is needed for the first editor.

Required conveniences: line numbers, Python syntax colors, multiline strings and
f-strings, indentation guides, auto-indent, Tab/Shift-Tab, comment toggle, bracket
matching, search/replace, undo/redo, zoom, error markers and clickable tracebacks.
Class/property/port templates reduce typing. Static completion and language-server
integration are later work; completion must not import arbitrary user modules.

Properties/Ports preview refreshes after successful descriptor checks, while
preserving draft values. Test supports Rx/Tx selection, HEX/text input, explicit
encoding, empty/NUL packets and expected outputs without touching real devices.
Tests run in a separate runtime instance so they do not mutate live script state.
Diagnostics show traceback, port, invocation, duration and revision; logs are
bounded/tagged and do not dump routine packet payloads.

## Verification boundaries

Verify descriptor inheritance, typed properties, conditions, ports and metadata
changes; NUL/empty/split data; output expansion and ordering; async child calls;
import failures and native modules; infinite Python loops and blocked native calls;
queue pressure, reload races, source-save conflicts, workspace replacement and
shutdown. Never advertise a hard deadline with only cooperative embedded execution.
Use synthetic streams and local fixtures by default, not real-device commands.

## Stream codecs and packet input

Native byte codecs, framing and terminal Lineflow are already delivered. Python
codecs use that same packet contract and the same separate Rx/Tx directions.
Transport reads remain arbitrary byte chunks; explicit framing nodes or bounded
script state perform accumulation. Preserve keyboard/paste/mouse/focus/reply
output distinctions and display-only local echo. No script response is fed back
into received data implicitly. See [terminal processing](terminal-processing.md)
and [terminal emulation](terminal-emulation.md).
