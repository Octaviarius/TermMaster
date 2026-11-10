# Contextual help and third-party notices

Status: next scheduled stage after FLOW-27 paired processor families. This document
records the design; the runtime help browser and credits page remain planned.

## Help source and presentation

Use module-owned Markdown for user documentation and schema-derived parameter
reference sections. Doxygen remains an optional developer API build, separate
from packaged user help. Markdown explains behavior and examples; parameter
names, descriptions, defaults, types, required flags, enums and bounds come from
the exact registered SettingsSchema so they cannot drift into duplicate tables.

App owns the Help window and commands. Reusable Core/Gui/Help presentation uses
QTextBrowser and QTextDocument Markdown support, without WebEngine. The MVP has
a topic tree, text search and current-page Find. A bounded in-memory search index
covers authored text and generated parameter reference. It needs no database,
service or second processor registry.

Module distributions package help beside their library/public headers, resolved
relative to the module path, never the working directory or AppData. Relative
resource paths must stay inside their help root. Embedded metadata supplies a
help-root/topic reference discoverable without plugin startup. A processor entry
references a stable topic ID; missing topics fall back to generated schema help.

Lineflow's selected node and settings pages supply contextual Help/F1 targets.
Opening help must not instantiate processors, execute scripts, change settings or
connect devices. Read pages/images into host-owned memory; help must not retain
plugin factories, callbacks, resource pointers or provider leases. Provider removal
invalidates its index/history entries; an already copied page may remain visible
with its provider availability indicated.

```mermaid
flowchart LR
    M[Module Markdown and local assets] --> D[Contextual document]
    S[Registered metadata and settings schema] --> D
    D --> V[Core Widgets viewer]
    A[App Help command or selected node] --> V
    D --> I[Bounded offline search index]
```

MVP checks cover disabled-plugin help discovery, selected nested-node resolution,
unload with an open copied page, launch from another working directory, unknown
and stale topics, path containment, broken links, search and native DPI rendering.

## Developer help: first custom processor

Add a concise, end-to-end tutorial using one minimal working processor plugin.
The tutorial must show the actual calls and file/build placement rather than
presenting interfaces without an executable registration path:

1. Implement the public `Processor` contract: `configuration()`, `accept()` and
   `cancel()`, with owned byte packets, supplied limits and completion callbacks.
2. Provide a versioned YAML settings schema with usable defaults and demonstrate
   one generated control through `x-widget` where appropriate.
3. Build a `ProcessorCatalog::Type` with its ID, title, group, schema, port metadata
   and factory; register it through `PluginHost::registerProcessorType()` during
   plugin startup after host injection.
4. Show the minimal SHARED plugin CMake target, public SDK includes and packaged
   library/header layout; do not depend on private Core implementation headers.
5. Demonstrate catalog discovery and placement in Lineflow, then a synthetic
   input/output check without connecting a real device.
6. Unregister through `PluginHost::unregisterProcessorType()` during shutdown,
   handle refusal while instances are in use, and acknowledge cancellation only
   after owned work and callbacks have stopped.

Keep the first example small and complete. Follow it with optional topics for
stateful/async output and for separate family-owned direct, inverse and Codec
creation choices. Explain that unary schemas are assembled before form creation
and must not instantiate the unused direction or its editors. Compile and test
the published example against the current SDK so documentation remains accurate.
This is scheduled developer-help content; the tutorial is not implemented yet.

## Third-party page and distribution

Add a separate App Help/About action for Third-party components. A versioned
manifest supplies component name, authors, upstream URL, selected version/commit,
license identifier and paths to verbatim license/NOTICE texts. The page presents
credits and links plus readable full original notices; acknowledgements do not
replace license conditions. Package the manifest and texts with each build.
Track only shipped code/assets and the chosen deployment's dependencies, not
unused tests or optional upstream integrations.

Initial repository audit:

| Component | Source evidence | Original notice |
| --- | --- | --- |
| fmt 12.2.1 | components/fmt, commit 208219daff58f2a7438f6931cc2ddc358acf33ef | components/fmt/LICENSE |
| PCRE2 10.49 | components/pcre2, commit 6f9d7c1373262c541324a16a358785b33ef116cf | components/pcre2/LICENCE.md and COPYING |
| SLJIT | components/pcre2/deps/sljit, commit de0259c7aaf36aa40cba8014f3fad3edde9307f9 | components/pcre2/deps/sljit/LICENSE |
| Valijson | components/valijson, commit af2383509277f8e8051acbb3e4b3060f4779aa10 | components/valijson/LICENSE |
| yaml-cpp | components/yaml-cpp, commit 1d8ca1f35eb3a9c9142462b28282a848e5d29a91 | components/yaml-cpp/LICENSE |
| Lucide and Feather-derived icons | components/lucide/SOURCE.md | components/lucide/LICENSE, retain both notices |
| Qt and bundled third-party code | selected Qt kit and deployed modules | deployment-specific Qt license/NOTICE inventory |

The current Qt 6.12.0 MSVC kit includes SPDX/CycloneDX SBOMs under its `sbom/`
directory. Use the actual selected kit/module records to resolve Qt's bundled
compression and other notices; do not infer the deployment from optional upstream
features. The project may use system ZLIB or Qt's bundled ZlibPrivate depending
on the configure result, so the manifest must record that choice.

Connection-status SVG geometry is attributed in docs/terminals-and-serial.md to
the adjacent Flowgate project's QML. That source project has no top-level license
file in the current checkout. Record provenance and resolve its ownership/license
explicitly before assigning it a third-party license. Project-generated artwork
and project-authored icons should be distinguished from copied third-party assets.

This inventory is source evidence and pending packaging work, not a declaration
that every redistribution obligation has already been fulfilled.


FLOW-27 paired families keep help topics associated with the fixed catalog choice.
Parameter reference generation must consume the assembled descriptor for that
choice: unary pages include only common and selected-direction parameters; Codec
pages include common parameters once and both directional groups. The separate
family owners provide the source definitions. HELP-1 remains the next scheduled
implementation stage after FLOW-27.
