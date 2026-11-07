# Qt rendering probe: measured results

Date: 2026-11-07. Status: completed synthetic investigation.
Following discussion of these results, the user selected `QTextLayout` for the
primary widget. See the [accepted renderer choice](terminal-rendering-research.md#accepted-renderer-choice).
The tables below remain experimental evidence; the remaining architecture is open.

## Configuration and method

Windows, AMD Ryzen 9 5900X, NVIDIA GeForce RTX 3090; Qt 6.12.0, MinGW GCC 13.1,
CMake/Ninja Release (`-O3 -DNDEBUG`, C++20). Font: Courier New, 11 pt. Main fixture:
140 columns by 42 rows, 1276 by 814 logical pixels, DPR 1. Each table value is the
median of three independent process results, each with 20 warmup and 80 measured
frames. Case order was randomized with seed 137. 75 main runs passed sample-count
and cache-reconstruction checks. Two smaller high-DPI comparisons add eight runs.

Instrumentation separates row preparation from QPainter drawing. Total paint
wall time includes painter teardown and, for OpenGL, `glFinish()`. OpenGL drawing
substage is command submission; the reported total includes GPU completion.
Raster drawing completes into the QWidget backing store. Final compositor/display
presentation is excluded from both. Do not convert these numbers into achieved
display FPS or input latency. These are instrumented stages, not sampled call stacks.

Application row caches are reset in the cold case; Qt and driver glyph/font caches
are not purged. Scrolling rotates already prepared rows and generates one new row.
It redraws the visible viewport without a backing-store pixel-scroll optimization.
Fonts/colors/layout and synthetic text are shared across methods. The main fixture
requests full repaint even for a single-row mutation. The partial case is separate.

## Main comparison

Paint callback wall time, milliseconds (p50). Lower is better.

| Renderer | Static redraw | Change one row | Scroll | Rebuild all row caches |
| --- | ---: | ---: | ---: | ---: |
| Fresh QTextLayout | 4.491 | 4.409 | 4.530 | 4.698 |
| Cached QTextLayout | 1.802 | 2.041 | 1.897 | 4.229 |
| Cached QGlyphRun | 1.575 | 1.714 | 1.725 | 4.740 |
| Raster row images | 0.220 | 0.422 | 0.344 | 6.307 |
| OpenGL + QGlyphRun | 2.668 | 2.733 | 2.712 | 5.310 |

Static redraw p50/p95 and end-of-process private memory. Memory is not a cache
size or peak measurement; GPU-driver and Qt allocations are included.

| Renderer | p50, ms | p95, ms | Private memory, MiB |
| --- | ---: | ---: | ---: |
| Fresh QTextLayout | 4.491 | 8.629 | 32.4 |
| Cached QTextLayout | 1.802 | 2.205 | 32.9 |
| Cached QGlyphRun | 1.575 | 2.354 | 32.4 |
| Raster row images | 0.220 | 0.993 | 36.0 |
| OpenGL + QGlyphRun | 2.668 | 3.180 | 157.5 |

For one changed row with a dirty-region update:

| Renderer | p50, ms | p95, ms |
| --- | ---: | ---: |
| Fresh QTextLayout | 0.126 | 0.230 |
| Cached QTextLayout | 0.129 | 0.234 |
| Cached QGlyphRun | 0.144 | 0.268 |
| Raster row images | 0.140 | 0.253 |
| OpenGL + QGlyphRun | 2.715 | 4.357 |

The OpenGL implementation paints its whole surface even when given a row update.
The partial table consequently compares actual implementation behavior, not equal
GPU and raster work. CPU partial repaint needs a row plus a correct clip/dirty region.

## High-DPI observation

The supplemental fixture is 100 columns by 22 rows to fit the monitor at 200%.
Each result below has one run of 20 warmup and 80 measured frames; it is exploratory
and should not be combined statistically with the larger DPR 1 fixture.

| Scale | Renderer | Static p50, ms | Scroll p50, ms |
| --- | --- | ---: | ---: |
| 150% | Cached QGlyphRun | 1.183 | 2.024 |
| 150% | Raster row images | 0.451 | 2.992 |
| 200% | Cached QGlyphRun | 1.535 | 1.559 |
| 200% | Raster row images | 0.347 | 0.476 |

A row image depends on its subpixel position in physical pixels. At 150%, moving
a 19-logical-pixel row changes that phase, so this implementation rebuilds the
affected tiles; glyph-run caches remain reusable. At 200% the row step is an
integer physical distance. This explains a workload-dependent reversal rather
than establishing a universal winner.

The first 200% supplemental attempt timed out because the native window was placed
outside the captured monitor. Explicit placement inside the primary screen fixed
it; the four successful reruns are the recorded 200% data. The earlier 42-row 150%
fixture did not fit the monitor and failed its reconstruction-count check. It was
excluded and replaced with the documented 22-row fixture. Native-window placement
and visibility must be controlled when repeating visible-widget measurements.

## Findings for discussion

1. Reusing prepared rows removes repeated shaping/layout work. On static redraw,
   preparation is approximately zero in methods 2-5, while fresh layout still
   spends substantial time preparing text. Per-frame stages are retained in JSON.
2. Raster images are cheapest to redraw in the main fixture, at additional memory
   cost. Rebuilding every tile is expensive. Caching the entire history as pixels
   is incompatible with bounded RAM; a future tile cache needs an explicit budget.
3. Cached glyph runs are a useful candidate for a general drawing path. The
   data does not show a sufficiently universal improvement over cached layouts
   to prescribe an architecture without the user's requirements.
4. OpenGL through QPainter is slower here than the strongest raster alternatives,
   and its process footprint is larger. This does not measure a custom instanced
   glyph atlas and does not establish that GPU rendering is inherently slower.
5. Dirty-region correctness changes the workload dramatically. Repainting one
   changed row is substantially cheaper than reshaping/repainting the viewport.
6. A fast text renderer does not provide VT compatibility or unlimited history.
   Those need separately specified parser/screen/storage behavior.

## Visual acceptance and limits

15 captures cover five methods at 100%, 150% and 200%, after initial cache warmup
and one scroll. The four raster methods produce identical pixels in the final
captures. OpenGL has different antialiasing; its image geometry was checked and
the 100% and 150% captures were visually reviewed. This does not test every font,
Unicode sequence, monitor transition, driver, resize or selection/cursor behavior.
The example includes foreground/background RGB, normal/bold spans, Cyrillic,
combining marks, CJK and emoji. It uses Qt text layout, not authoritative terminal
column-width semantics, and intentionally has no cursor/selection/VT/history.

A fractional-DPI regression was found and corrected: tiles now include physical
pixel phase in invalidation and use transparent padding to avoid row-boundary
background seams. The final visual comparison checks cached-row movement.

The app and scripts were reviewed for profiling validity. C++ was clang-formatted;
Python scripts were compiled for syntax; repository whitespace checks passed.
The deployed executable passed five additional static smoke runs. Its native
window was opened and interactive pause and switching 2 -> 5 -> 4 were verified.
The original TermMaster target was not built or changed as part of this probe.

## Reproduction and evidence

- [Example and commands](../examples/RenderingProbe/README.md)
- [Research and requirements](terminal-rendering-research.md)
- [Aggregated measurements and visual differences](RenderingProbeMeasurements.json)
- [Example image](images/RenderingProbe.png)

Raw per-frame runs remain generated output under `build/RenderingProbe/ProfileFinal`,
`Profile150Small` and `Profile200Small`; the aggregated record above is durable.
Its source hash uses canonical LF line endings; the final changes to help text
and initial window placement do not alter the measured paint paths.

High-DPI subset commands use `Profile.py` with `--cols 100 --rows 22 --repetitions 1
--methods 3 4 --cases static scroll --scale 1.5` (then `--scale 2`).
For automated visual checks run `Verify.py EXE OUTPUT --qt-bin QT_BIN`; Pillow
is required for image comparison. No files or dependencies were downloaded.
