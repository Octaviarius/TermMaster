# Python Codeflow

Codeflow implements a reusable packet Processor as a Python class. Add it from
**Tools / New Codeflow…** or the Lineflow Codeflow action. Existing Codeflow
nodes open their dedicated editor. Other native processors work without Python.

## Inline Python Code

For an occurrence-local script, drag **Native / General / Python Code (1×1)** or
**Python Code (2×2)** into Lineflow. Double-click it, click its small editor button,
or choose **Edit Python Code…** in the node properties. No separate reusable block
or source file is needed. The selected variant fixes its port count.

Save/Ctrl+S or Apply updates the diagram draft with the source and typed parameters.
Apply the outer diagram to persist it. Different occurrences remain independent.
The inline editor keeps Check/Test and local cancellation; it does not restart
other active diagrams. Modules and SDK handlers work as in Codeflow. Declared
`Node(...)` catalog dependencies require a published workspace Codeflow.

## Check, test and publish

1. Choose an existing Python executable, including an environment's
   `Scripts/python.exe` on Windows or `bin/python` on other systems.
2. Browse an external `.py` file or edit the source draft. The Processor class
   field selects its entry class. Empty class selection requires one unambiguous
   concrete Processor subclass.
3. Use **Check** (Ctrl+Return) to import the draft and preview Properties/Ports.
4. Use **Test** (Ctrl+T) with HEX or Text and the required input lane. HEX `00`
   represents NUL; empty input represents an explicit empty packet. Text uses the
   selected UTF-8 or Latin-1 encoding. Test creates independent state and does not
   send to a real terminal transport.
5. **Save** writes source. **Apply** (Ctrl+Shift+Return) checks, saves and publishes
   the revision. Save/publication failures retain a concrete diagnostic.

The editor supplies line numbers, syntax highlighting, indentation, bracket
matching, Ctrl+/ comments, find/replace, zoom and clickable traceback lines.
Properties and Ports are generated after Check. Restart/Stop control affected
processing; they do not take ownership of terminal connections. Stop also cancels
an active editor test/check operation.

Editing a source file does not silently activate its new contents. Apply a checked
revision; changed published file digests or descriptors fail explicitly. External
source saves detect concurrent changes and use atomic single-file replacement.
Source and workspace catalog publication are separate file operations; a saved
source with failed publication is reported. Browsing a file does not copy it into
the workspace. Relative source/resource paths use the workspace directory.

## Minimal processor

```python
from termaster import Processor, Property, Input, Output, handles

class Codeflow(Processor):
    prefix = Property(str, default="", title="Prefix")
    incoming = Input(bytes, title="Data")
    outgoing = Output(bytes, title="Data")

    @handles(incoming, output=outgoing)
    def process(self, data):
        return self.prefix.encode("utf-8") + data
```

Each occurrence owns independent instance state. Configuration is typed and
immutable while running. Initialize ordinary state in `start()`, never a custom
constructor. `start()` and `stop()` can be async; packet admission waits for start.
Cancellation joins startup, invocations and owned tasks before the stop hook and
Python acknowledgement. Noncooperative code is terminated after the host deadline.

A handler receives one owned byte packet, including arbitrary binary/NUL data:

| Result | Meaning |
| --- | --- |
| `bytes`, `bytearray`, `memoryview` | One owned output packet. |
| `b""` | One empty output packet. |
| `None` | No output. |
| `list` or `tuple` of byte buffers | Several packets, in order. |
| `self.outgoing.emit(data)` then `None` | Explicit output routing. |

Sync and async handlers share this contract. Do not mix returned packets with
explicit emissions. Strings, dictionaries and generators are errors; no implicit
encoding occurs. Transport chunks can split messages: use a Framer or explicitly
bounded instance buffering when a protocol needs complete frames.

## Properties and ports

`Property` supports `bool`, `int`, `float`, `str`, `pathlib.Path` and scalar Enum
values. Options include `default`, `choices`, `minimum`, `maximum`, `step`, `title`,
`group`, `help`, `order`, `read_only` and `widget`. Property names are stable setting
IDs. Path values resolve relative to the workspace at runtime, while settings
retain the raw relative text. File/directory pickers use Path options `mode="save"`
and `kind="directory"`. Numeric settings stay within the shared exact-number range.

Conditional fields support one finite string/enum selector:

```python
mode = Property(str, default="fixed", choices=["fixed", "variable"])
width = Property(int, default=2, minimum=1, maximum=8,
                 when=mode.is_("fixed"))
```

Declare those fields inside the Processor class. The generated Draft 7 schema
feeds the ordinary settings form; arbitrary visibility expressions are unsupported.

The delivered shapes are 1x1 and paired 2x2 byte ports. Every input has one handler.
Paired inputs and outputs each require one `flow="rx"` and one `flow="tx"`:
Rx means device to terminal, Tx means terminal to device. Runtime indices are Rx 0,
Tx 1. Rotation changes presentation without changing lane identity.

## Native child processors

```python
from termaster import Processor, Input, Output, Node, handles

class Codeflow(Processor):
    incoming = Input()
    outgoing = Output()
    encoder = Node("native.hex-encoder", separator="")
    pacer = Node("native.pacer", byteDelayMs=10)

    @handles(incoming, output=outgoing)
    async def process(self, data):
        result = []
        for encoded in await self.encoder.process(data):
            result.extend(await self.pacer.process(encoded))
        return result
```

A declared `Node` references an available native or saved Lineflow catalog entry.
Its factory/plugin owners remain retained during execution and cancellation.
`await self.child.process(data)` returns a bounded list of bytes. To select a
paired child input and retain output ports, use
`await self.child.process(data, port=1, with_ports=True)`; each result has immutable
`.data` and `.port`. Calls can also be awaited in `start()`. Temporary child
backpressure is retried within bounded queues and execution deadlines. Await
sequential calls when their order matters.

`self.ctx.log(message, level="info")` emits bounded diagnostics.
`ctx.currentpacket` exposes immutable request/port/data during a handler.
`ctx.create_task(coroutine)` and `ctx.timer(seconds, callback, repeat=False)` own
background work and stop it during cancellation. Owned tasks can emit output
without an active input packet and must bound their own buffering.

## Environments, limits and trust

An independent processing diagram owns one external CPython helper; its Codeflow
instances share that interpreter while retaining separate state. Independent
instantiations use separate helpers. One diagram cannot mix Python environments.
Standard-library, local relative imports and installed compatible third-party/native
modules are supported. Relative imports such as `from .helpers import decode`
use private entry-package namespaces. Imported helper files are not immutable
source snapshots. Python is not bundled; package/ABI compatibility remains the
selected environment's responsibility.

**Create virtual environment…** runs the selected interpreter's `-m venv`.
**Install local packages…** runs its `-m pip install --no-index --no-deps` with
selected local distributions. No package download or implicit dependency installation
occurs. Supply required dependencies separately; package build/import code still
runs with normal user permissions.

This is not a sandbox. Scripts may access files, networks and processes with user
permissions; performed effects cannot be rolled back. Startup/import, packet and
stop-grace timeouts are editable from 20 to 30,000 ms. Bounded IPC, packet/expansion,
queue, unsolicited-output and diagnostic limits do not impose a general Python
heap quota. Default processing limits are 1 MiB and 1,024 output chunks. An
unresponsive helper faults its processing diagram; terminal transport is separate.
Native C++ children must honor their own cancellation/quiescence contracts. On
Windows the helper's owned descendants close with its job.

stdout, including native writes to descriptor 1, becomes diagnostics rather than
binary protocol traffic. Code imports execute during explicit Check/Test/Apply;
catalog browsing uses checked metadata without executing the source.
