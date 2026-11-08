# Termaster agent instructions

Read existing project documentation, CMake files and relevant sources before
changing behavior. These rules apply repository-wide.

## Scope and workflow

- Develop Termaster as a desktop terminal application using C++ and Qt 6 Widgets.
  Preserve the current application model; do not introduce services, IPC or a
  separate backend without an explicit requirement.
- Continue authorized work until completion or an explicit stop request.
  Answer intervening questions briefly and resume active work.
- Preserve unrelated working-tree changes. Keep changes focused; avoid incidental
  rewrites, repository-wide formatting and speculative abstractions.
- Implement each feature on a separate branch from dev. Use branches named
  codex/NAME-number-concise-context, with at most one numeric subnumber.
- Verify and commit focused stages separately. Commit messages use
  `<module>: <action> <description>`.
- After completion, merge into dev with --no-ff and delete the source branch.
  Merge titles use `Merge [NAME-number]: concise description`.
  Describe purpose, verification and material limitations in the merge body.
- Preparatory documentation uses ordinary commits without feature IDs or
  feature merge commits. Do not push unless explicitly requested.
- Record requirements, architecture and implementation status in English topical
  documentation. Keep docs/roadmap.md as the single milestone checklist when
  roadmap documentation is introduced. Do not create an ADR hierarchy.
- During development support only the current application data format.
  Do not add legacy readers, migrations or compatibility aliases.
  Prepare local development data separately when formats change.
- Remove obsolete code and its tests together after checking callers and
  dependencies. Preserve active lifecycle and data-integrity contracts.

## Automation and subagents

- Prefer simple Python scripts and automated tools for repetitive tasks.
  Batch independent operations and validate mechanical changes automatically.
- Download files only through physical network adapters, never VPN or virtual
  adapters.
- Delegate bounded independent tasks only when doing so saves effort.
  Prefer one or two workers; keep small or tightly coupled changes local.
- Select the least expensive sufficient model:
  - gpt-6-luna / low: inventories, searches and simple mechanical edits.
  - gpt-6.1-sol / low: implementation, debugging and tests.
  - gpt-6-astra / high: difficult unresolved architecture or failure analysis.
- Explicit model selection requires fork_turns="none".
  Provide context, owned files, constraints and acceptance checks.
- Give editing workers disjoint files. Reviewers remain read-only.
  The primary agent reviews results, integrates changes, verifies and commits.
  Never pass real secrets to workers.

## Architecture

- Preserve meaningful boundaries between:
  - terminal connection backends and byte transport;
  - terminal emulation and escape-sequence parsing;
  - terminal screen state, cursor and scrollback;
  - widget rendering and input handling;
  - application orchestration;
  - settings persistence.
- Keep application modules under src/. Each module exposes public headers under
  inc/Termaster/ModuleName and keeps implementation/internal organization under
  src/. Place changes with the responsible component. Dependencies belong under
  components/ at the repository root.
- Mirror the public header hierarchy in module src/: a header at
  inc/Termaster/ModuleName/path/Class.h has its implementation at src/path/Class.cpp.
  Do not repeat the Termaster/ModuleName prefix under src/.
- Keep module-owned GUI under src/gui/, grouped by feature. Public GUI headers
  belong under inc/Termaster/ModuleName/gui/ with the same feature hierarchy.
  Keep internal headers and .ui files beside their implementations; do not add
  module-root gui/ directories or separate gui/inc and gui/src trees.
- Group each feature's widgets, presentation models, delegates and controllers
  together under gui/feature/. Keep domain models and application state outside
  gui/; being consumed by a widget does not make a model part of presentation.
- Qt is an intentional dependency. Use QObject, signals/slots, QByteArray,
  QString and Qt ownership conventions where appropriate.
  Do not create replacements for Qt infrastructure without a concrete need.
- Keep connection backends independent of concrete widgets.
  Move parsing toward screen/model operations when that boundary is changed;
  do not expand direct rendering dependencies inside protocol parsing.
- Application coordinators sequence components. Persistence, transport I/O and
  rendering must not orchestrate one another.
- Keep resource ownership with the responsible component.
  Document borrowed lifetimes and thread affinity.
- Use inheritance only for actual backend substitution or framework integration.
  Prefer cohesive existing owners over trivial forwarding classes and wrappers.
- Add terminal backends only when requested. Icons, names and placeholders do
  not establish that SSH, sockets or local-console support is implemented.

## Terminal and I/O correctness

- Treat transport data as bytes. Use QByteArray or appropriate byte views,
  not QString, until the encoding boundary.
- Input may contain arbitrary bytes, including NUL. Reads are stream chunks,
  not complete messages or escape sequences.
- Preserve parser and decoder state across reads. UTF-8 characters and terminal
  control sequences may be split at any byte boundary.
- Bound escape-sequence accumulation, screen allocation, scrollback and pending
  output. Malformed or unsupported input must not cause unbounded growth,
  hangs or invalid memory access.
- Make terminal compatibility explicit. Do not claim full ANSI/VT compatibility
  when only a subset is implemented.
- Preserve distinctions between received data, local echo, keyboard encoding,
  pasted text and configured newline conversion.
- Handle partial writes, queued output, backpressure and asynchronous errors.
  Acceptance into a write buffer does not prove delivery to the device.
- Keep connection state transitions explicit. Open/close, cancellation,
  device removal, failure and reopening must release owned resources correctly.
- Do not access widgets from worker threads. Respect QObject thread affinity;
  use context-bound connections and queued delivery where required.
- Do not retain references to callback data beyond its lifetime.
  Shutdown must stop new work, cancel operations and drain owned workers before
  destroying resources they use.
- Never send commands to real devices or modify device settings as part of
  default automated verification.

## C++ and API conventions

- Use C++20 for new code. Configure the language standard explicitly when
  changing build requirements; preserve compatibility with the selected Qt
  and compiler toolchain.
- Use #pragma once in new or substantially rewritten project headers.
- Follow Qt-style naming: camelCase methods, short getters and setX setters.
  Prefix private/protected fields and internal methods with an underscore.
- Keep representation private and enforce invariants through cohesive methods.
- Keep owned statuses, enums, callbacks and result types nested in the
  responsible class and declared in its primary header.
- Order access sections public, protected, private; declare each used section
  exactly once and omit unused sections. Keep public types and methods together;
  do not split access sections into repeated blocks or a separate type prelude.
  Within each section put types/constants first, fields second and methods last.
  Preserve member initialization order, destruction dependencies and lifetimes.
- Group methods by responsibility and separate groups with blank lines.
  Group implementation methods by class: constructors/destructor, public,
  protected and private methods.
- Separate validation, preparation, execution and result handling with blank
  lines inside functions.
- Declare one variable per declaration and per line.
- Use Q_DISABLE_COPY or related Qt macros when corresponding special members
  should be deleted. Preserve deliberate move-only APIs.
- Avoid new global mutable state or additional singletons unless their lifetime
  and responsibility require it.
- Follow existing project naming. Do not introduce another project's namespaces,
  macros or target prefixes.
- Use CamelCase filenames for new project classes and test scripts.
  Preserve standard metadata filenames and vendored names.
- Write UTF-8 explicitly in editing scripts. Use English documentation and
  useful Doxygen comments in /** ... */ form.
- Use neutral, impersonal documentation wording. Record requirements, decisions
  and verification results without attributing them to conversation participants.
- Run clang-format -i on each changed C/C++ file after editing.
  Use the repository .clang-format. Do not format unrelated files, Markdown,
  CMake or scripts. Report unavailable formatting.

## Settings and diagnostics

- Use the existing settings owner rather than reading and writing settings
  independently from every widget.
- Keep persistence separate from widget state and connection lifecycle.
  Validate loaded values before applying them to devices or allocating resources.
- Do not silently discard unknown settings or overwrite unrelated preferences.
- Check persistence failures and report concrete causes.
- Keep passwords, private keys and potentially sensitive terminal contents out
  of routine logs. Raw session recording requires an explicit user action.
- Log meaningful lifecycle transitions and failures without routine per-byte
  or per-character noise.
- Do not describe simulated input or synthetic counters as real-device acceptance.

## GUI

- Use MVC for nontrivial behavior: models own presentation state, views render
  it and controllers perform application commands.
- Keep authoritative state with its owner rather than duplicating it in widgets
  and controllers.
- Preserve approved icon shapes, sizes, ordering, spacing and alignment.
  Compare rendered results with the reference before delivering visual changes.
- Use consistent form layouts for settings: right-aligned labels and
  left-aligned controls. Labels must remain readable without splitting words.
- Keep detailed explanations in tooltips or documentation. Error messages
  should state the concrete cause concisely.
- Terminal rendering must preserve cursor, selection, colors, text attributes,
  scrolling and resize behavior affected by the change.
- Verify font metrics, high-DPI behavior and Unicode handling when changing
  terminal layout or rendering. Do not assume one Unicode code point always
  occupies one terminal cell.

## Build and dependencies

- Use CMake and the existing target graph. Prefer target-scoped include paths,
  compile settings and dependencies over new directory-wide settings.
- Preserve current target names unless renaming is explicitly part of the task.
- Use four-space CMake indentation, one list item per line and a separate
  closing parenthesis. Prefer lines within 100 columns, at most 120.
- Do not edit or reformat vendored dependencies under components/.
  Preserve licenses and pin new source dependencies.
- Do not download dependencies during CMake configuration.
- Discover and use actual build commands and toolchain configuration.
  Do not assume a preset exists; introduce reproducible presets as a separate
  scoped change when needed.
- Treat build/ as entirely disposable generated output.
  Never store authored helper scripts, source inputs, credentials or the only
  copy of required verification records there.
- Keep reusable scripts in tracked tools/tests directories and durable,
  secret-free acceptance summaries in topical documentation.
- On Windows, verify resolved host paths when packaged tools may redirect
  AppData into a package cache.
- Do not request elevation, install drivers or modify installed services unless
  explicitly authorized.

## Verification

- Run verification appropriate to the changed behavior. Add meaningful tests
  for correctness and lifecycle risks rather than tests that mirror the code.
- Use synthetic terminal streams and bounded fixtures by default.
  Serial hardware and external network acceptance require explicit opt-in.
- Parser checks should cover chunk boundaries, incomplete UTF-8, split escape
  sequences, malformed input and configured resource limits.
- I/O checks should cover partial writes, errors, close/reopen and cancellation.
- GUI changes require visual verification of the affected behavior.
  A successful build does not establish correct rendering.
- Run available project tests and affected builds; report unavailable or
  unexecuted checks honestly.
- Keep test assertions active in Release configurations.
- For documentation-only changes, check links and git diff --check.
- Never configure or build while an announced build-output cleanup is running.
