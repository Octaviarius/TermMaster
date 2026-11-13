# Application performance probes

These tools measure the real App class and project plugins against isolated
workspace copies. They never connect restored terminals. See
[results and remediation](../../../docs/application-performance.md).
Windows/MSVC is required for the CPU/memory counters and local CDB integration.

```powershell
. .\.vscode\ExportEnvironment.ps1
cmake --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Release `
    -DTERMASTER_BUILD_APPLICATION_PROFILE=ON
cmake --build --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Release `
    --target ApplicationProfile SerialTerminal ConsoleTerminal SocketTerminal
python tools/benchmarks/ApplicationProfile/Run.py `
    --source G:/TMP/Tmaster --scratch G:/TMP/Tmaster-Profile-NewRun `
    --build build/Desktop_Qt_6_12_0_MSVC2022_64bit-Release `
    --output docs/benchmarks/application-profile/new-release --repetitions 2
python tools/benchmarks/ApplicationProfile/Summarize.py docs/benchmarks/application-profile
```

Scratch must be a new directory outside the original workspace; the output must
also be outside it. `Run.py` copies all source files, checks original hashes
before/after, keeps private console/debugger logs in scratch, and publishes only
measurement records. The source fixture must remain unchanged by other processes
during a run. The current fixture entry filename is `ws.termaster`. The report is
checkpointed before teardown so bounded hangs do not lose completed timings.
The process timeout covers the entire probe, not just shutdown. Inspect the last
completed stages before classifying a timeout as a close hang.

Use `--serial` only with an explicitly authorized COM3 USB-UART loopback. It opens
COM3 at 115200, clears its queues, sends known binary data and checks every received
byte. It does not modify driver configuration. Omit it for ordinary profiling.

The following environment variables select additional controls:

- `TERMASTER_PROFILE_RESIZE_QUICK=1`: warm/reset grid resize and cold/warm widget
  repaint controls; font family, grid, DPR and direct glyph-metric controls are recorded.
  Changed-appearance repaint toggles foreground by one RGB unit to force cache
  invalidation; identical reapplication measures the no-op path separately.
- `TERMASTER_PROFILE_RESIZE_ONLY=1`: after the ordinary 20 empty-window resizes,
  fill the visible terminal with a synthetic 10,040-line ASCII stream (no transport),
  then measure 30 one-pixel width steps, 30 height steps and 30 alternating large
  diagonal resizes. Record actual font/DPR/grid/history and separate synchronous
  geometry from event-draining time. Close through the ordinary loaded-window path;
  skip unrelated lifecycle/editor/transport probes. Other resize/close flags should
  be unset for this mode.
- `TERMASTER_PROFILE_ONE_CYCLE=1`: one lifecycle cycle instead of five.
- `TERMASTER_PROFILE_SKIP_TERMINAL=1`: lifecycle-only attribution without screen/paint microbenchmarks.
- `TERMASTER_PROFILE_CLOSE_ONLY=1`: open the workspace and close its loaded window
  through App's normal queued close path. No explicit quit is injected.

For Debug, configure/build the matching Debug preset and use its plugin directory
through `--build`. Clear flags between different scenarios; `Runs.json` records
those used by new runs. Do not run benchmarks concurrently with compilation or
other CPU-heavy probes.

## Temporary call attribution

`Instrument.py` backs up production files and inserts scope timers at named
function bodies. Backups include original/instrumented hashes; restore refuses
concurrently modified files. Keep backups outside disposable build output.
Restore in a `finally` block even if compilation or measurements fail:

```powershell
python tools/benchmarks/ApplicationProfile/Instrument.py apply G:/TMP/ProfileSourceBackup
try {
    cmake --build --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Release --target ApplicationProfile
    $env:TERMASTER_PROFILE_SKIP_TERMINAL = '1'
    python tools/benchmarks/ApplicationProfile/Run.py `
        --source G:/TMP/Tmaster --scratch G:/TMP/ProfileInstrumentedRun `
        --build build/Desktop_Qt_6_12_0_MSVC2022_64bit-Release `
        --output docs/benchmarks/application-profile/new-instrumented --repetitions 1
} finally {
    python tools/benchmarks/ApplicationProfile/Instrument.py restore G:/TMP/ProfileSourceBackup
    Remove-Item Env:TERMASTER_PROFILE_SKIP_TERMINAL -ErrorAction SilentlyContinue
    cmake --build --preset Desktop_Qt_6_12_0_MSVC2022_64bit-Release --target ApplicationProfile Termaster
}
```

Interpret inclusive/exclusive totals only alongside uninstrumented baselines.
The single-GUI-thread trace store is shared by static App/Core, but does not
provide complete thread/process stacks or allocator attribution. Snapshots are
printed before final teardown as well as at process exit.

`--capture-stacks` invokes the installed CDB on a timed-out probe with a bounded
non-invasive main-thread stack command and local symbol path only. Logs stay in
scratch. MSVC writes `ApplicationProfile.map` for matching project-frame lookup;
retain the exact map alongside its captured binary until addresses are resolved.
No network symbol server, downloads, elevation or policy changes are involved.


## Project renderer and visual wrapping

Only the project renderer is built. Experimental renderers remain in the historical
VIEW-1 branch. The native 10,000-record resize/scroll fixture supports both visual
wrap settings; source hashes, actual paints, scrollbar movement, normal exit and
unchanged source workspace are required. No serial connection is opened.

```powershell
python tools/benchmarks/ApplicationProfile/RunDisplayBenchmark.py `
    --source G:/TMP/Tmaster `
    --scratch C:/Users/octaviarius/AppData/Local/Temp/TermasterResizeVerification/FreshWrapRun `
    --output docs/benchmarks/terminal-wrap/Release `
    --build build/Desktop_Qt_6_12_0_MSVC2022_64bit-Release
```

Run sequentially without concurrent builds or benchmarks. Both roots must be fresh.
Measurements end at Qt event/paint delivery, rather than compositor presentation.
The short records fit the fixture's smallest width; correctness tests separately
exercise long source rows, automatic-wrap joins, selection and width changes.

## Docking and restored startup

`Run.py` now always prepares each marked scratch copy in a separate bounded
`--prepare-fixture` process before timed startup. It writes `connected: false` on
all objects in copied `session.yaml` documents through the existing Settings
owner. A pending workspace processing journal is rejected. Originals are hashed
and never edited. Preparation and fixture copying are outside recorded startup.

Use `--restored-startup` to seed the isolated `application.yaml` last-workspace
reference. `app.construct` and `app.start.to.eventloop` then include normal
restoration; `app.restored.first.presentation.fromProcessMainMs` includes delivery
of the main window's first paint. These timings begin at probe main, excluding
OS process-loader work. The runner also polls a first-paint marker every 5 ms
and records `launchToFirstPaintMs`, including process creation/loader work and
polling/scheduling uncertainty. Preparation is excluded. Full process wall time
is recorded separately.

Use `--dock` for visible native Windows synthetic docking. Each fixture has
1, 8 or 32 tabs in the measured group plus one control tab in a second group.
Every page receives 1,000 synthetic ASCII lines; no device owns a transport.
Five cycles measure tab join/split, whole-group placement, whole-group detach,
and actual secondary-window close/return. Each operation has `.sync` and
`.presentation` records; the latter drains Qt paints and DeferredDelete and
checks object identity, population, rejected connection attempts and actual
viewport painting. Secondary completion also requires destruction of its host.
Measurements end at Qt delivery, excluding final compositor presentation.

```powershell
python tools/benchmarks/ApplicationProfile/Run.py `
    --source G:/TMP/Tmaster --scratch G:/TMP/TermasterDock-Fresh `
    --build build/Desktop_Qt_6_12_0_MSVC2022_64bit-Release `
    --output docs/benchmarks/docking-startup/Release --dock --restored-startup --repetitions 5
```

Run only after the preceding development build finishes. Use fresh scratch paths,
clear unrelated profile flags, and repeat Debug separately. Do not combine these
controls with `--serial`. Diagnostic scope targets include startup, docking,
workspace transfer, action refresh and presentation persistence; baseline timings
must come from uninstrumented sources.

Set `TERMASTER_PROFILE_SCOPES=1` only for a diagnostic instrumented run to attach
per-operation inclusive/exclusive scope deltas. These add instrumentation overhead;
keep headline measurements from the uninstrumented runs.

Before replacing the copied workspace with synthetic fixtures, `--dock` also
exercises the configured restored plugin panels and terminal-local processing.
Its 2–64 restored terminals are grouped into the main window outside timing;
five cycles measure split/join, whole-group detach and actual secondary close.
Stages use `dock.configured<N>.<operation>.sync` and `.presentation`. Captured
borrowed devices must remain alive and Closed, every session connection intent
must remain false, page identities and runtime/session populations must remain
unchanged, and completion requires real viewport paints and secondary destruction.
No bytes are delivered to or written through these configured transports.
Target collection selects each real page and verifies its active identity before
borrowing its device; inability to establish that target fails the configured
phase explicitly. Existing screen contents and Inplace definitions are retained.

Aggregate and validate completed runs with:

```powershell
python tools/benchmarks/ApplicationProfile/SummarizeDock.py docs/benchmarks/docking-startup/release-configured
```

The summary reports sample count, median, nearest-rank p95 and maximum; operation
`.total` is the paired `.sync` plus `.presentation` sample, not the sum of separate
percentiles. Small sample p95 values are descriptive. Every recorded invariant,
process exit and original-source hash must pass.

For returning-user startup, add `--existing-preferences` to seed the three bundled
plugins as enabled before timing. Without it, isolated startup also includes the
one-time default plugin-activation settings writes. For a short startup-only run,
set `TERMASTER_PROFILE_CLOSE_ONLY=1`, use `--restored-startup` and omit `--dock`.

The temporary instrumentation supports `TERMASTER_PROFILE_NATIVE_GROUP_FRAME=1`
as a controlled experiment: only the group-wide QSS border is replaced by native
QFrame Box/2px. Tab-bar and toolbar styles remain. This condition exists solely in
the instrumented sources and is removed by byte-exact restoration. It is not a
shipped application option. Compare matching instrumented runs and preserve the
mode metadata. Debug docking with 32 populated tabs can require `--timeout 420`.

Set `TERMASTER_PROFILE_CONFIGURED_ONLY=1` with `--dock` for a bounded diagnostic
that measures only restored configured panels and exits normally. This omits
synthetic 1/8/32-tab construction and is useful for expensive Debug instrumentation.
