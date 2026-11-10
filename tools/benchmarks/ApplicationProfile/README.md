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
