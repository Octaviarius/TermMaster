# RenderingProbe

A standalone C++20 / Qt Widgets example with one window, synthetic colored text
and five renderer choices. It does not change the Termaster executable or select
its future architecture. It contains no VT parser, transport or disk history.

Keys: **1** fresh QTextLayout, **2** cached QTextLayout, **3** cached QGlyphRun,
**4** raster row cache, **5** QOpenGLWidget + cached QGlyphRun; **Space** pauses
scrolling, **Escape** closes the window. The title identifies the selected renderer.

The OpenGL experiment uses Qt's QPainter engine, not a custom GPU glyph atlas.
Row images include physical-pixel phase in their cache key; scrolling at fractional
DPI may rebuild them even when their text is unchanged.
Background rectangles are drawn separately from text. Colors include exact RGB;
normal/bold spans, Cyrillic, combining marks, CJK and emoji exercise font fallback.
The fixture uses normal Qt text layout, not a completed terminal-cell-width model.

## Build and run

Configure this directory independently of the existing application's old build
cache. Qt 6.7+ with Widgets and OpenGLWidgets, CMake 3.21+, and a C++20 compiler
are required. This example downloads nothing.

The commands used on the development machine, from the repository root:

```powershell
$env:PATH = 'C:\Qt\6.12.0\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
& C:\Qt\Tools\CMake_64\bin\cmake.exe -S examples/RenderingProbe -B build/RenderingProbe -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64 `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
& C:\Qt\Tools\CMake_64\bin\cmake.exe --build build/RenderingProbe --parallel 4
& .\build\RenderingProbe\RenderingProbe.exe
```

Adjust paths to the local installation. The installed Qt records a GCC 15.1
build; the available application compiler here is GCC 13.1. This combination
built and ran locally, but it is not a recommendation for the production toolchain.

## Repeatable profiling

```powershell
python examples/RenderingProbe/Profile.py build/RenderingProbe/RenderingProbe.exe `
  build/RenderingProbe/Profile --qt-bin C:/Qt/6.12.0/mingw_64/bin --frames 80 --repetitions 3
```

The runner starts a fresh process for each case, randomizes case order with a
fixed seed, and validates sample counts and the expected number of layout/tile
cache rebuilds. It compares static redraws, application-cache resets, one changed
row, scrolling with one newly generated row, and one changed row with a dirty-region
update. There are 20 unmeasured warmup frames per process. "Cold" clears application
row caches every frame; Qt/driver font caches and OS file caches remain warm.

All main comparisons request a full visible repaint. The separate partial case
uses a row rectangle. QOpenGLWidget still renders its whole surface in this
implementation; partial results intentionally expose that difference.

Each frame records:

- row layout/run/tile preparation wall time;
- QPainter drawing wall time;
- total paint callback wall time;
- time from the update request, including the synthetic mutation, to paint completion;
- layout and tile reconstruction counts.

The OpenGL total includes `glFinish()` after QPainter ends. Its drawing substage
measures command generation, while the total includes the GPU completion wait.
The raster total includes synchronous rendering into the QWidget backing store.
Neither includes final compositor/display presentation, so these times are not
input-to-photon latency or achieved display FPS. GPU results also include Qt's
OpenGL paint-engine overhead; a custom batched atlas may behave differently.

Windows process private bytes are sampled once at the end, not an exact cache-size
or peak measurement. They include Qt and driver allocations. Results for a fixed
viewport say nothing about the scalability of a future disk-history implementation.

Raw JSON goes under the requested output directory. Keep generated runs in
`build/`; durable methodology and acceptance results belong in
[the profiling report](../../docs/terminal-rendering-profile.md).

`--snapshot FILE.png` warms the caches, scrolls once, writes a PNG and exits. `--rows`, `--cols`,
`--frames`, `--workload`, `--partial` and `--method` configure bounded fixtures.
`QT_SCALE_FACTOR=1.5` or `2` exercises larger physical pixel caches.
Choose dimensions that fit the monitor. For the high-DPI subset, use `--cols 100
--rows 22 --repetitions 1 --methods 3 4 --cases static scroll --scale 1.5`
with `Profile.py`, then repeat at `--scale 2`.

For image checks (Pillow required):

```powershell
python examples/RenderingProbe/Verify.py build/RenderingProbe/RenderingProbe.exe `
  build/RenderingProbe/Visual --qt-bin C:/Qt/6.12.0/mingw_64/bin
```
