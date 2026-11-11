# External editor rendering reference

## Scope and reproduction

Installed VS Code 1.146.0 (Electron 43.8.6 / Chromium 150.0.7871.250) and
Notepad++ 8.9.8.1 were exercised on the same Windows 11 / Ryzen 9 5900X host as
the terminal experiments. Fresh private profiles and generated files isolate
normal sessions and preferences. User plugins/extensions were disabled. No
software was downloaded or installed. Existing working editor windows were not
used as targets. The initial VS Code launch was blocked by its updater; the
successful measurements used the subsequently updated installation.

Scripts:

- [BenchmarkCode.py](../tools/benchmarks/EditorReference/BenchmarkCode.py): native
  window sizing plus Chrome DevTools Protocol metrics/input/DOM validation.
- [BenchmarkNotepad.py](../tools/benchmarks/EditorReference/BenchmarkNotepad.py):
  native Win32 sizing/paint delivery and the public Scintilla message API.

```powershell
python -B tools/benchmarks/EditorReference/BenchmarkCode.py `
    --scratch C:/Users/octaviarius/AppData/Local/Temp/NewCodeReference `
    --output docs/benchmarks/editor-reference/CodeRuns.json --rounds 2
python -B tools/benchmarks/EditorReference/BenchmarkNotepad.py `
    --scratch C:/Users/octaviarius/AppData/Local/Temp/NewNotepadReference `
    --output docs/benchmarks/editor-reference/NotepadRuns.json --rounds 2
```

Run them sequentially, without a concurrent build/benchmark. Scratch directories
must be fresh. Python and the existing websocket-client package are required for
VS Code; neither script installs dependencies. Each process is closed normally
after checking unmodified generated files. No terminal command is typed into an
editor, and the tested files contain only synthetic records.

Two fresh processes per editor/size/content produce 60 width, height and diagonal
resize samples; page and jump sample counts are in the raw reports. Files contain
10,000 or 100,000 short records, with no wrapping. JSON syntax highlighting
includes Cyrillic, Japanese, combining accents and emoji. Notepad++ also has an
ASCII plain-text baseline. This is a more expensive text mix than ordinary ASCII
source code and is not a controlled single-variable test of color overhead.

Courier New is selected at 16 CSS px in VS Code; the fresh Notepad++ profile uses
Courier New with every style set to 12 pt. Notepad++ reports 96 DPI, DirectWrite
technology 1 and its existing layout-cache mode 1. JSON uses six active style IDs;
the isolated generated style configuration maps them to five distinct colors.
VS Code colors are checked from actual computed styles after every operation.
First and last records, bounded visible DOM rows, native scroll positions,
line counts and file hashes are verified. When a first jump reaches a region
before its tokens are ready, the script waits for highlighting before treating
it as a colored result.

## Timing boundaries

These are external reference/scaling measurements, not a strict cross-engine
ranking against the earlier Qt timings. The inputs, UI chrome and completion
boundaries differ from the terminal's ANSI-cell fixture.

VS Code wall time includes native sizing or CDP key input, two requestAnimationFrame
callbacks, protocol round trips and a highlighted-DOM validation probe. The fixed
two-frame wait creates a roughly 30–33 ms floor. CDP Performance metrics are also
recorded with timeDomain=threadTicks; these are renderer-thread metrics including
probe work, not total application/GPU cost. Layout/Style components do not include
all rendering. The minimap remains enabled. Background tokenization/validation
can contribute to variation; startup/warmup is outside the interaction samples.

Notepad++ wall time covers native synchronous operations and delivery of pending
paints without invalidating the whole view. GUI-thread CPU uses GetThreadTimes;
its accounting is coarse, so only aggregate means are useful. The exact tested
version's [DirectWrite path](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8.1/scintilla/win32/ScintillaWin.cxx#L978)
selects D2D1_PRESENT_OPTIONS_NONE. Microsoft documents that this
[waits for display refresh](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/ne-d2d1-d2d1_present_options).
Thus 16.7/33/50 ms quantization can include presentation waiting. It is not pure
text-layout CPU, and the source does not prove that every measured millisecond
is a refresh wait.

## Results

All four VS Code and eight Notepad++ processes passed the validation checks.
VS Code held 30–35 rendered line elements for both document sizes, and the tested
visible text remained syntax-colored. A tenfold increase in total rows did not
produce a tenfold increase in resize or scroll work.

Selected medians, ms:

| Metric | 10,000 records | 100,000 records |
| --- | ---: | ---: |
| VS Code large resize, ready wall | 32.85 | 32.93 |
| VS Code large resize, renderer TaskDuration | 22.50 | 22.25 |
| VS Code large resize, Layout component | 2.76 | 2.74 |
| VS Code page scroll, ready wall | 32.05 | 32.00 |
| Notepad++ ASCII large resize, native wall | 16.52 | 17.05 |
| Notepad++ JSON/Unicode large resize, native wall | 46.28 | 45.14 |
| Notepad++ JSON/Unicode large resize, mean GUI CPU | 41.67 | 40.89 |
| Notepad++ JSON/Unicode page scroll, native wall | 49.82 | 49.89 |

VS Code is not uniformly free of pauses: one-pixel width steps have ready-wall p95
around 87 ms at 10,000 records and 115 ms at 100,000. Large-resize and page medians
are stable across sizes. Notepad++ has much lower costs for plain ASCII than this
heavy JSON/Unicode fixture; its timing is still broadly independent of row count.
The comparison therefore supports bounded interactive work, not a claim that
these editors always outperform Termaster on every visible-text workload.

Raw records and complete median/p95/max tables:
[VS Code](benchmarks/editor-reference/CodeRuns.json),
[VS Code summary](benchmarks/editor-reference/CodeSummary.json),
[Notepad++](benchmarks/editor-reference/NotepadRuns.json),
[Notepad++ summary](benchmarks/editor-reference/Summary.json),
[environment and validation](benchmarks/editor-reference/Environment.json).

## Applicable architecture and next steps

VS Code's [VisibleLinesCollection](https://github.com/microsoft/vscode/blob/main/src/vs/editor/browser/view/viewLayer.ts)
works from the viewport range and retains overlapping presentation lines.
Its [viewLines implementation](https://github.com/microsoft/vscode/blob/main/src/vs/editor/browser/viewParts/viewLines/viewLines.ts)
separates visible rendering and deferred width measurement. These sources support
the observed bounded DOM; they do not imply all operations, including wrapping
and tokenization, are independent of document size.

Scintilla separates [document storage and display mapping](https://www.scintilla.org/Design.html)
and provides [line-layout and position caches](https://www.scintilla.org/ScintillaDoc.html#SCI_SETLAYOUTCACHE).
The important transferable boundary is stable source text plus selectively
invalidated view data. A terminal's mutable active grid still needs its own VT
resize contract; an editor's document model cannot simply replace it unchanged.

The historical VIEW-1 experiment had the opposite hot path on width changes:
TerminalScreen::setLimits resizes and revises every retained row, then
StockTerminalView::sync rebuilds per-cell text/formatting for all revised rows.
It may discover the document text is unchanged only after paying that work.
This source-based attribution agrees with the prior integrated stock resize
stalls and the much faster standalone QPlainTextEdit control. It is not a
measured flame-graph breakdown.

The existing grid renderer already limits painting and caches prepared runs.
Its measured Release large-resize median was about 11–12 ms on the earlier
10,000-line fixture. The reference editors therefore do not establish that Qt
or every project renderer is inherently slow. Debug and Release must remain
separate comparisons.

Priorities derived from the reference stage:

1. Keep retained history independent of viewport column changes; preserve an
   explicit resize policy for the active terminal screen.
2. Track semantic row content separately from geometry-only changes and feed
   only actual changed spans into the project renderer.
3. Coalesce presentation updates while preserving parser/output ordering and
   source-data correctness. Retain bounded shaping/layout caches and native
   scrolling where they help.
4. Recheck continuous drag/input latency with the same completion boundary after
   those changes. VIEW-2 applies the model/layout changes using the project renderer.
