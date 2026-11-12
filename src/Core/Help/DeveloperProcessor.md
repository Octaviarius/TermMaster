# First custom processor

The complete, compiled example is in `examples/ProcessorPlugin/`. It registers
`sample.prefix`, a one-input/one-output processor that prepends UTF-8 text to each
owned byte packet. It preserves binary payloads, including NUL, and emits one
packet for empty input. It performs no device or network I/O.

## Implement the public contract

`src/PrefixProcessor.h` derives from the public
`<Termaster/Core/Processing/Processor.h>` SDK header. Its three required methods
are `configuration()`, `accept(Chunk, Callbacks)` and
`cancel(std::function<void()>)`. Configuration is copied per instance; the
constructor converts only the textual prefix to UTF-8. Incoming bytes stay in
`QByteArray` and are never decoded.

`accept()` validates the port and checks prefix expansion against
`Limits::maxBytes` before allocation. It emits an owned output packet followed
by exactly one successful completion for each accepted invocation. Rejected
invocations invoke no callbacks. A reentrant invocation returns `WouldBlock`.
Output port zero is independent of the incoming port selection.

All calls and callbacks run on the owning QObject thread, normally the GUI
thread. The example finishes synchronously. `cancel()` stops acceptance and
suppresses completion if called from an output callback. It acknowledges only
after the active invocation stops touching instance state. Repeated cancellation
acknowledges each caller. Keep the instance alive until acknowledgement; create
a new instance to reset it.

## Add settings and register the type

`Resources/Prefix.yaml` is the actual embedded settings descriptor:

```yaml
version: 1
type: termaster.settings-schema
description: Prefix processor example settings
content:
  $schema: http://json-schema.org/draft-07/schema#
  type: object
  properties:
    prefix:
      type: string
      title: Prefix
      description: UTF-8 text prepended to every complete input byte packet.
      default: "[sample] "
      maxLength: 256
      x-settings-path: prefix
      x-widget: stringLineEdit
      x-order: 1
```

The host resolves and validates defaults before construction. The sample factory
also validates direct calls and supplies its useful default when omitted.
`PrefixPlugin` implements both `Plugin` and `HostedPlugin`; the manager injects
`PluginHost*` before startup. Its startup assembles the real catalog entry:

```cpp
ProcessorCatalog::Type type;
type.id = "sample.prefix";
type.title = "Prefix packet (example)";
type.group = "Examples";
type.editor.insert("helpTopic", "example.prefix");
type.settingsSchema = schema.readAll();
type.inputCount = 1;
type.outputCount = 1;
type.inputNames = {"Packet"};
type.outputNames = {"Prefixed packet"};
// The complete factory in PrefixPlugin.cpp validates configuration and limits.
type.create = [](const QVariantMap& configuration,
                 const Processor::Limits& limits,
                 QString*) -> std::unique_ptr<Processor> {
    return std::make_unique<PrefixProcessor>(configuration, limits);
};
_host->registerProcessorType(std::move(type));
```

The shortened factory above illustrates the signature; build the complete source,
which handles missing defaults and invalid limits. Registration errors fail
startup. Shutdown calls
`_host->unregisterProcessorType("sample.prefix", error)` and returns false on
refusal, retaining registration state for a retry. Prepared factories and live
instances pin native code; release them before unload. The host also checks
these leases before invoking shutdown.

`src/PrefixPlugin.json` embeds `"help":"help/ProcessorTutorial.qch"`. The versioned
`Help/manifest.json` associates `example.prefix` with `Prefix.md`. The help
compiler packages the authored page and navigation into `ProcessorTutorial.qch`
beside the library under `help/`. Markdown and JSON remain build inputs. The host can discover this documentation while the
plugin is disabled; viewing help does not start the plugin. `editor.helpTopic`
associates the registered processor with that page.

## Build the sample

The sample uses only Qt and the exported SDK, with no private Core headers or
second static Core inside the plugin. Its CMake entry point uses the same helper
as application modules:

```cmake
find_package(TermasterPluginApi CONFIG REQUIRED)
termaster_add_plugin(ProcessorTutorialPlugin
    OUTPUT_ROOT "${CMAKE_CURRENT_BINARY_DIR}/tutorial-plugin"
    SOURCES
        src/PrefixProcessor.cpp
        src/PrefixProcessor.h
        src/PrefixPlugin.cpp
        src/PrefixPlugin.h
        src/PrefixPlugin.json
        Resources/ProcessorTutorial.qrc
)
set_target_properties(ProcessorTutorialPlugin PROPERTIES AUTORCC ON)
termaster_compile_help(
    TARGET ProcessorTutorialHelp
    NAMESPACE org.termaster.example.prefix
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/tutorial-plugin/ProcessorTutorialPlugin/${TERMASTER_PLUGIN_LIBRARY_DIR}/help/ProcessorTutorial.qch"
    MANIFESTS "${CMAKE_CURRENT_SOURCE_DIR}/Help/manifest.json"
    SCHEMAS "example.prefix=${CMAKE_CURRENT_SOURCE_DIR}/Resources/Prefix.yaml"
)
add_dependencies(ProcessorTutorialPlugin ProcessorTutorialHelp)
```

From the repository root, in a matching MSVC x64 developer PowerShell with Qt,
CMake and Ninja on PATH (see the repository's development environment guide):

```powershell
cmake --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Debug -DTERMASTER_BUILD_HELP_TESTS=ON
cmake --build --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Debug --target ProcessorTutorialTests
ctest --test-dir build/Desktop_Qt_6_12_0_MSVC2022_64bit-Debug -R '^ProcessorTutorialTests$' --output-on-failure
```

The helper emits `tutorial-plugin/ProcessorTutorialPlugin/` under the sample's
binary directory, containing `bin` (Windows DLL), `lib/cmake` and `include`.
This sample exports no extra public classes, so its include directory is empty.
On Unix-like platforms the library and its adjacent compiled `help` directory are under
`lib`. The sample is deliberately outside the application's normal plugin root.

To compile it independently against a previously configured SDK, use the same
compiler, Qt, architecture and configuration as the application:

```powershell
$sdk = (Resolve-Path build/Desktop_Qt_6_12_0_MSVC2022_64bit-Debug/plugin/PluginApi).Path
cmake -S examples/ProcessorPlugin -B build/ProcessorTutorialExternal -G Ninja `
    -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_PREFIX_PATH=$sdk;$env:TERMASTER_QT_ROOT"
cmake --build build/ProcessorTutorialExternal --target ProcessorTutorialPlugin
```

Standalone builds create the plugin; the Core-backed integration executable is
available in the repository build. The helper does not bundle Qt or compiler
runtimes.

## Discover and exercise it

For interactive use, copy the sample library and its adjacent compiled `help/` directory
into an isolated application's configured plugin directory. Enable **Prefix
processor tutorial** in the plugin manager. In Lineflow, add **Examples / Prefix
packet (example)** and edit **Prefix** in its generated settings form. Select the
node and open contextual Help to see the [packaged example](help:example.prefix).
Avoid changing the normal plugin directory just to run the automatic check.

`tests/ProcessorTutorialTests.cpp` loads the compiled library through the real
`PluginManager`, first disabled and then enabled, using temporary settings and
a minimal non-widget host. It exercises the registration and factory path:

```cpp
auto processor = manager.processors().create("sample.prefix", {}, {}, &error);
processor->accept({QByteArray("A\0B", 3), 0}, {
    [](Processor::Chunk output) { /* bytes: "[sample] A\0B", length 12 */ },
    [](QString error) { /* empty error means completion */ }
});
processor->cancel([] { /* no owned work or future callbacks remain */ });
```

The test checks defaults, overrides, NUL and empty packets, expansion bounds,
reentrant cancellation and unload refusal while instances or prepared factories
remain live. It uses synthetic packets and never opens a device.

## Further processor shapes

Stateful or asynchronous processors must bound retained packets, preserve packet
ownership, post worker results through context-bound delivery to their QObject
thread, and acknowledge cancellation only after workers and pending callbacks
stop. Lifetime output handlers are for traffic unrelated to an accepted call.

For paired families, keep common, direct and inverse descriptors with the family
owner. Assemble each unary choice's common and selected-direction schema before
opening forms; do not instantiate the unused direction or its editors. Codec
choices include common fields once and both directional groups. This small
prefix example intentionally exposes only a unary native choice.
