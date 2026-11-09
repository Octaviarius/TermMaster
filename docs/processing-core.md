# Processing core

`Processor` is the shared asynchronous byte processing contract. Native elementary
processors and `Lineflow` implement that contract. A future `Codeflow` composition
can implement the same interface around a Python runtime; no interpreter, script
execution or control-flow language is delivered in this stage.

The core is independent of terminal widgets and transports. Workspace definitions
are reusable configuration, while each instantiated pipeline owns separate stream
state. Rx and Tx consumers must instantiate separate pipelines. The operation
editor remains a standalone interaction prototype and is not connected to this core.

```mermaid
flowchart LR
    W[Workspace processorBlocks] --> P[Validated immutable preparation]
    R[Host processor registry] --> P
    P --> RX[Independent Lineflow instance]
    P --> TX[Independent Lineflow instance]
    RX --> N[Native Processor instances]
    TX --> M[Native Processor instances]
    F[Future Codeflow composition] -. same Processor contract .-> N
```

## Contract and ownership

`Processor::Chunk` owns a `QByteArray` and a stream-chunk or packet boundary. NUL
bytes remain bytes; an empty packet is an explicit output. Stream chunks do not
imply complete messages. An accepted invocation can produce zero, one or many
outputs followed by exactly one completion. A stateful framer completes an input
that only extends its buffer without producing output. `WouldBlock` and `Rejected`
consume nothing and do not invoke callbacks. Input acceptance is not delivery to
a device.

All calls and callbacks run on the processor QObject thread. Workers post through
context-bound queued connections. A consumer allows only one active invocation;
Lineflow serializes child invocations and preserves emitted output order. Its
queued pump has a finite event budget and retries a temporarily blocked child in
a later event-loop turn. Output callbacks have no per-output credit: the host
bounds its queue, each child invocation's expansion and the final output of an
accepted input, then fails and cancels on overflow. Identity stages do not divide
the maximum payload size by pipeline depth.
Native plugin code is trusted to bound its own buffers according to factory limits;
these bounds do not sandbox arbitrary plugin memory or CPU usage.

Cancellation immediately stops acceptance and suppresses pending invocation
callbacks, and acknowledges only after every worker has stopped using the
instance. Cancellation is idempotent; each caller receives an acknowledgement.
Generations discard stale pipeline callbacks but do not stop native worker work.
Reset creates a new instance from the prepared snapshot after the old instance
has acknowledged cancellation; prepared definitions do not carry stream state.

The registry returns shared instance leases. Last release invokes cancellation;
the native instance and module lease remain alive until acknowledgement and a
subsequent event-loop turn, so plugin cancellation returns before destruction. A plugin
that never acknowledges remains pinned. Factories and instances must be created
and released on the registry's owning thread. Factories return an unparented
processor on that same thread. Configuration snapshots contain persisted value
types, not QObject pointers.

## Plugin lifetime

The plugin SDK consists of header-only `Processor`, `ProcessorCatalog`, and the
existing pure host interfaces. Hosted capability IID version 7 rejects older
host layouts before any new SDK field or method can be used. Native plugins link Qt and the SDK, not another
static Core. `PluginHost::registerProcessorType` uses a manager-assigned owner
scope during plugin startup. Every type supplies a stable ID, title, version 1
`termaster.settings-schema` descriptor and factory. Registry catalog queries
return copied metadata without an executable factory. Unknown configuration
properties are retained according to the descriptor's schema. Registration throws
on invalid metadata, duplicate IDs, invalid descriptors or an unusable default
configuration. It validates the resolved empty configuration before inserting the
factory; required settings must therefore have a valid default baseline.

`PluginManager` owns the registry. Shutdown/reload fails while a factory is being
called or a live/cancelling instance holds its owner lease. Once leases drain,
plugin shutdown runs; the manager removes all remaining factories before unloading
the library, including factories a plugin forgot to unregister. A failed shutdown
keeps the library mapped. Host copies of names, descriptors and errors do not
retain pointers into an unloaded plugin's literal storage.

```mermaid
sequenceDiagram
    participant Manager
    participant Plugin
    participant Registry
    participant Instance
    Manager->>Plugin: startup with owner scope
    Plugin->>Registry: register native type
    Registry->>Instance: create independent instance and lease
    Manager->>Registry: canRemoveOwner
    Registry-->>Manager: blocked while instance or cancellation lives
    Instance->>Instance: cancel and acknowledge quiescence
    Registry->>Registry: destroy instance and release lease
    Manager->>Plugin: shutdown
    Manager->>Registry: remove remaining factories
    Manager->>Plugin: unload library
```

## Workspace definitions and preparation

Workspace content contains an optional `processorBlocks` map. Keys are stable
IDs, independent of mutable names. A block contains matching `id`, `name` and
ordered `nodes`. Each node has a unique `name` within its block; that name is its identity, and contains
either a native `type` with a `configuration` map or a `reference` to another block. References reuse definitions, never live
instances. Reference configuration overrides are rejected during preparation.
Unknown block and node metadata is retained by the settings owner.

```yaml
processorBlocks:
  receive-lines:
    id: receive-lines
    name: Receive lines
    nodes:
      - name: frame
        type: native.cr-framer
        configuration: {}
  receive:
    id: receive
    name: Receive processing
    nodes:
      - name: framing
        reference: receive-lines
      - name: output
        type: native.identity
        configuration: {}
```

Workspace load/sync checks structural limits, IDs, reference existence and cycles.
Unavailable native types remain valid persisted definitions, so a workspace opens
without a missing plugin. `Lineflow::prepare` checks the selected graph, value
bounds, expansion depth/instance limits, registered types and settings schemas,
then snapshots definitions and configurations. Instantiation creates fresh
processors and revalidates against the currently registered factories. A prepared
snapshot does not pin plugin libraries or bypass later schema validation.

## Defaults and private terminal overrides

Configuration resolution merges schema defaults, the workspace node configuration,
and private instance overrides in that order. Maps merge recursively, arrays
replace, and unknown properties are preserved. Required values with defaults are
filled before validation. Registration rejects a type whose empty resolved
configuration lacks a required value or violates its descriptor. Resolved effective values reach
the native factory and its `configuration()` snapshot without changing shared
workspace definitions.

Private overrides are a map keyed by leaf occurrence paths. Paths join
reference-node names and the leaf-node name with `/`; they do not use positional indexes
or processor type IDs. Reordering a node preserves its address. Renaming changes identity: old override
paths are rejected, and a future editor must update affected references atomically. Two references
to the same definition have separate paths and independent live instances.
`Prepared::instantiate(registry, overrides, error)` validates exact addresses and
creates a fresh runtime. The prepared common configuration remains immutable.

Terminal-specific settings persist in the session object's existing settings map:

```yaml
objects:
  terminal-a:
    type: terminal
    module: vendor.terminal
    settings:
      processing:
        rx:
          block: receive
          overrides:
            framing/frame:
              futureFramerOption: true
        tx:
          block: receive
          overrides: {}
```

`framing/frame` above is one literal map key. Read and write the whole `overrides`
map atomically through `Settings::Node`; do not pass the key to the slash-delimited
settings path API. Terminal A and terminal B keep separate maps, as do Rx and Tx.
Saving these opaque settings does not load a plugin, resolve defaults, start a
processor, or connect terminal routing.

The supplied native factories are `native.identity`, `native.cr-framer` and
`native.hex-decoder`; register them explicitly with `registerNativeProcessors`.
The CR framer preserves partial frames across stream chunks and emits packet
boundaries at CR, including empty packets. The HEX decoder preserves a split
nibble across stream chunks, rejects invalid hexadecimal bytes and rejects a
packet ending in a half byte. These processors have no terminal UI integration.

## Verification

Configure the existing Debug tree from the repository root:

```powershell
. ./.vscode/ExportEnvironment.ps1
cmake -S . -B build/Desktop_Qt_6_12_0_MSVC2022_64bit-Debug -DTERMASTER_BUILD_PLUGIN_TESTS=ON -DTERMASTER_BUILD_PROCESSING_TESTS=ON
cmake --build build/Desktop_Qt_6_12_0_MSVC2022_64bit-Debug --parallel 6
ctest --test-dir build/Desktop_Qt_6_12_0_MSVC2022_64bit-Debug --output-on-failure
```

Synthetic checks cover binary and empty packets, stateful zero-output completion,
independent instances, nested references, schema rejection, graph and queue bounds,
async multiple output ordering, temporary backpressure and cancellation. The real
SDK plugin fixture registers an asynchronous native factory, instantiates it through
the host registry, verifies unload refusal during a live/cancelling instance and
verifies automatic factory removal before unload. Workspace tests verify unknown
metadata and unavailable-type roundtrips and atomic failure preservation.

Terminal routing, production editing UI, Python execution and Codeflow runtime
remain separate work. Verification uses synthetic bytes without serial hardware,
external network acceptance or elevation.

Verified on 2026-11-09 with Qt 6.12.0, MSVC x64 and the existing Debug tree:
full application/plugin build and all 34 configured CTest executables passed.
The separately built SDK consumer was rebuilt against the generated package and
its targeted `independentSdkConsumer` test passed without skips. The ordinary
suite leaves opt-in visual capture disabled; host symlink privileges were not
available for the existing confinement fixtures. No processing test was skipped.
All changed C++ files were clang-formatted; local documentation file links and
`git diff --check` passed.
