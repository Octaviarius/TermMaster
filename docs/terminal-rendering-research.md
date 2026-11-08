# Terminal rendering and history research

Research date: 2026-11-07. Status: research completed; text renderer selected.
The renderer choice below is accepted; remaining architectural options are
subject to further design.

## Accepted renderer choice

Qt Widgets is the selected application UI framework. Build the
application windows, dialogs and terminal widget with Widgets. The terminal
text-rendering choice is `QTextLayout` within that Widgets application.

`QTextLayout` is selected for the primary terminal widget based on the profiling
results and the cost of redrawing changed text. Prefer this direct
Qt text-layout path over introducing a separate glyph-run renderer, raster row
cache or custom GPU atlas at this stage.

Retain prepared layouts for unchanged visible rows and invalidate affected rows
when their text, formatting or layout inputs change. Keep layout caching bounded
to the working viewport rather than retaining a layout for every historical row.
The selected renderer is now integrated in the project-owned terminal widget.
See [current emulation](terminal-emulation.md) for the model, bounded viewport
cache, compatibility matrix and remaining history work.

## Requirements recorded so far

- A fast primary terminal widget in a C++/Qt 6 Widgets application.
- Independent foreground and background colors, preserved in history.
- Broad DEC VT-family compatibility and Unix terminal extensions.
- History without an application-imposed line-count limit, with caching.
- A minimal, runnable Qt application with one window to demonstrate rendering.
- Several rendering variants compared through actual profiling.

"Unlimited" history can mean no fixed line-count limit. Storage capacity,
address space, input rate and failure handling remain finite. RAM usage must
be bounded independently of history length. This interpretation needs to remain
explicit in any product claim.

## Research snapshot of the archived renderer

The existing widget provides a useful rendering experiment, but its data model
and repaint path do not yet establish the requirements above.

- `TermWidget::_paintRect()` rebuilds text, formats and `QTextLayout` during
  painting. Calling `setCacheEnabled(true)` on a short-lived/repopulated layout
  does not retain prepared layouts across paint events.
- `textFormats` is declared outside the row loop and is not cleared for each
  row. Format-range boundaries also need correctness tests before performance
  conclusions can be drawn from this implementation.
- The passed dirty rectangle is not used to select rows in `_paintRect()`.
  The method visits rows from the beginning and relies on widget bounds to stop.
  Empty rows do not advance the drawing position.
- Model updates trigger synchronous `repaint()`. Event-coalesced `update()`
  with explicit dirty regions is a candidate for reducing repeated work.
- `TermModel` keeps all rows in a `QList<TermLine>`. No disk-backed history or
  bounded decoded-history cache is implemented.
- `TermAttribute` stores 8-bit foreground/background palette indexes. In the
  SGR 38/48 truecolor path, `Emulator` constructs a `QColor` but does not apply
  it. Exact RGB colors therefore cannot currently survive that path.
- Parsing accesses `TermWidget` and its model directly. A future renderer
  comparison should not make protocol semantics depend on a paint backend.
- `Emulator::inputData()` buffers an incomplete UTF-8 suffix, but subsequently
  iterates UTF-16 `QChar` units. A code-unit loop is not a grapheme/cell model.

These are source observations, not a measured allocation or CPU profile.

## Rendering candidates

The comparison should use the same visible rows, colors, font, scale factor,
dirty regions and mutations. Cache construction belongs in cold/mutation
measurements rather than being hidden outside the measured workload.

| Candidate | Reused work | Main trade-off |
| --- | --- | --- |
| Fresh `QTextLayout` | Qt's internal font resources | Simple baseline; repeats layout preparation on repaint. |
| Persistent `QTextLayout` per visible row | Layout and shaping until invalidated | Low integration cost; cache lifetime and invalidation matter. |
| Cached `QGlyphRun` plus background rectangles | Glyph selection and positions | More explicit draw path; cell mapping, colors and decoration remain application responsibilities. |
| Raster row/tile cache | Completed pixels | Cheap warm blits; memory and invalidation grow with physical pixel area. |
| `QOpenGLWidget` with `QPainter` | Prepared runs plus Qt's graphics-engine caches | Measures an accelerated Qt path without implementing a custom atlas. |
| Custom GPU glyph atlas | Rasterized glyph masks, positions and draw batches | Greater control; additional font, GPU-resource and compositing complexity. |

Qt documents `QTextLayout` caching and glyph-run extraction. `QPainter` can
draw runs at explicit positions. These APIs support the proposed experiments;
they do not establish which variant is fastest for this workload.
[QTextLayout](https://doc.qt.io/qt-6/qtextlayout.html),
[QPainter::drawGlyphRun](https://doc.qt.io/qt-6/qpainter.html#drawGlyphRun).

`QStaticText` is another cached-layout option for repeatedly drawn text. Its
documentation cautions that changing text/layout every iteration can remove
the benefit. It is less directly suited to explicit terminal-cell placement
than a run-based experiment, but remains a useful reference.
[QStaticText](https://doc.qt.io/qt-6/qstatictext.html).

For a custom atlas, a monochrome glyph mask can be reused with different text
colors; background rectangles are separate geometry. Color emoji require a
color-glyph path rather than tinting a monochrome mask. The Windows Terminal
AtlasEngine is a concrete primary-source example of font/glyph cache lookup
and batched glyph geometry, not a portable component proposed for adoption.
[AtlasEngine overview](https://github.com/microsoft/terminal/blob/main/src/renderer/atlas/README.md).

`QOpenGLWidget` integrates OpenGL rendering into Widgets. `QRhiWidget`, available
since Qt 6.7, is a candidate for graphics-API portability. Using QRhi currently
requires the Qt `GuiPrivate` target; its compatibility contract and exact
installed Qt version must be checked before selecting it.
[QOpenGLWidget](https://doc.qt.io/qt-6/qopenglwidget.html),
[QRhiWidget](https://doc.qt.io/qt-6/qrhiwidget.html),
[QRhi](https://doc.qt.io/qt-6/qrhi.html).

## Color, cells and cache invalidation candidates

Preserve a color's meaning as default, indexed or exact RGB. Resolve default
and indexed colors against the applicable palette when drawing. Converting
all colors permanently into palette indexes loses truecolor precision;
converting all indexes permanently into RGB changes palette-update semantics.
The behavior of historical palette changes must be specified explicitly:
live-palette history and palette snapshots are different contracts.

A future screen cell needs a relationship to a grapheme, occupied cell width,
continuation cells and rendition. Foreground, background and underline color
are distinct. Bold, faint, italic, underline style, inverse, hidden, blink,
protected cells and hyperlinks need explicit semantics where supported.

UTF-8 bytes, Unicode scalar values, grapheme clusters, glyphs and terminal
columns are separate concepts. Grapheme segmentation alone does not determine
terminal column width. Font fallback, combining marks, emoji sequences, wide
characters and a documented ambiguous-width policy need fixtures. Shaping
must preserve the terminal grid, cursor and selection coordinates. A renderer
must not let ordinary paragraph wrapping silently determine screen contents.
[Unicode text segmentation](https://www.unicode.org/reports/tr29/).

Possible cache keys include row identity/revision, font/style, physical pixel
size, device-pixel ratio, width policy and shaping policy. Pixel caches also
depend on resolved colors and palette revision. Glyph-mask caches usually do
not depend on foreground color. Cursor, selection and search highlighting can
be overlays so that their movement need not reshape unchanged text.

Bound each cache by bytes, not merely item count. Layout/run caches, raster
tiles and GPU atlas pages need separate budgets and eviction. A row that leaves
the viewport need not keep a layout or image for the entire session.

## Large history candidates

A possible arrangement is a mutable screen, a bounded recent-row cache and
an append-oriented disk store for rows retired into scrollback. Store semantic
text/cells and rendition, not screenshots and not only the original byte stream.
Replaying all prior terminal input to show an old page would make random access
depend on session length.

Use independent, bounded-size blocks with an index for locating row ranges.
Each block needs enough information to decode its text, attributes, hyperlinks,
cell widths and soft-wrap metadata without reading the whole preceding file.
Compression is a candidate to benchmark after correctness, not a prerequisite.

Bounded memory must cover the index, style tables, hyperlink tables, search
metadata, pending writes and decoded blocks as well as visible rows. A single
in-memory offset per historical line is still unbounded RAM growth. Candidate
solutions include paged on-disk indexes and block-local style dictionaries.
Use 64-bit history IDs/offsets; do not stretch QWidget pixel coordinates or a
32-bit scrollbar range to represent an arbitrarily long document directly.

Konsole's file history uses cell, index and line-property storage. Its source
also calls out the limitation of mapping an entire large history file. This
supports investigating disk-backed history and bounded mapping windows; it
does not prove the proposed Termaster format or performance.
[HistoryScrollFile](https://github.com/KDE/konsole/blob/master/src/history/HistoryScrollFile.cpp),
[HistoryFile](https://github.com/KDE/konsole/blob/master/src/history/HistoryFile.cpp).

Disk reads, decompression and search should not block a paint event. Any worker
design needs bounded queues, request cancellation, generation checks and clear
ownership; widgets remain on the GUI thread. On disk-full or write failure,
report the failure and apply an explicit retention/backpressure policy rather
than silently losing history or growing a pending RAM queue indefinitely.

History is scrollback, not automatically a recording of every overwritten screen
state. Alternate-screen handling, partial scroll regions, erase-scrollback,
selection anchors, wide-cell edits and resize/reflow all need precise rules.
Reflowing the whole disk history on every resize is a latency risk; lazy block
reflow or preserving original rows are candidates with different semantics.
Graphics/history retention must also be budgeted if image protocols are enabled.

## Compatibility scope and evidence

There is no finite shared specification named "all Unix extensions". Preserve
the broad compatibility objective while defining versioned profiles and an
explicit sequence/behavior matrix. Different historical terminal personalities
can require different answers and mode behavior; they cannot be established by
one generic ANSI parser or by rendering colored text.

| Area | Examples to inventory and verify |
| --- | --- |
| DEC personalities | VT52, VT100/102, VT220, VT3xx, VT4xx and VT5xx families; exact models and manuals still to enumerate. |
| Stream parser | C0/C1, ESC, CSI, OSC, DCS, APC/PM/SOS where applicable; cancellation, termination and chunk boundaries. |
| Screen semantics | Margins, origin/autowrap modes, insert/delete/erase, rectangular operations, protected cells, tabs and saved state. |
| DEC text presentation | Character sets, national replacements, line drawing, double-width/double-height lines and downloadable fonts where required. |
| Replies and input | Device attributes/status, mode reports, keyboard/keypad modes, mouse/focus reports and terminal identification. |
| Unix ecosystem | xterm colors/modes, alternate screen, bracketed paste, hyperlinks, clipboard policy, synchronized updates, shell integration and terminfo behavior. |
| Graphics | DEC Sixel/ReGIS and modern image protocols where selected; text-only drawing does not establish coverage. |
| Modern extensions | Kitty keyboard/graphics/text sizing and other named protocols, with versions and feature negotiation. |

Xterm explicitly documents selected features from higher DEC models rather
than claiming complete emulation of every VT terminal. Its control-sequence
reference and DEC function checklist are useful inventory inputs.
[Xterm scope](https://invisible-island.net/xterm/ctlseqs/),
[Xterm control sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.pdf),
[DEC function checklist](https://www.invisible-island.net/xterm/ctlseqs/dec_term_function_checklist.pdf).

Kitty maintains a separate extension catalogue, illustrating why extensions
must be named and versioned rather than treated as one closed Unix standard.
[Kitty extensions](https://sw.kovidgoyal.net/kitty/protocol-extensions/).

## Emulation implementation candidates and verification

Extending the existing emulator, integrating a library, or adapting a mature
Qt terminal implementation remain alternatives. `libvterm` describes a
toolkit-independent VT220/xterm-like C library. The current `libghostty-vt`
header exposes terminal state and rendering-oriented examples; its release
and build requirements must be evaluated at a pinned version. Neither name
establishes complete coverage of the desired DEC and Unix matrix.
[libvterm](https://www.leonerd.org.uk/code/libvterm/),
[libghostty-vt API](https://github.com/ghostty-org/ghostty/blob/main/include/ghostty/vt.h),
[libghostty-vt packaging](https://github.com/ghostty-org/ghostty/blob/main/PACKAGING-LIBGHOSTTY.md).

The separate minimal prototype will compare controlled synthetic workloads:
cold preparation, repeated warm drawing, mutations and scrolling. Record
preparation and draw costs, cache sizes, frame-time distributions, exact Qt/
compiler/font/device configuration and GPU synchronization method. Do not
compare CPU submission time to GPU completion time or report either as
end-to-end presentation latency. No performance results are asserted here.

Protocol acceptance additionally needs stream-split fixtures, malformed input,
resource limits, model snapshots and device-reply checks. VTTEST is useful
evidence for its covered behavior, alongside explicit extension fixtures; a
VTTEST run alone cannot prove all-model compatibility.
[VTTEST](https://invisible-island.net/vttest/).
