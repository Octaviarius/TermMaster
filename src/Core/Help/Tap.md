# Tap observation branch

Tap has one input and one output. It forwards the original whole byte packet unchanged exactly once, while an owned copy enters a private one-line Lineflow. Branch output is discarded. Use the branch for inspection, formatting and boundary-file recording without changing the main stream.

Drag Tap from Native / Control into either lane. Each placement gets its own branch. Double-click the card or click its small Lineflow button to edit that branch with the normal palette, canvas and settings pane. Its topology remains 1 x 1. Duplicating the owning block copies private branches; ordinary reusable references stay shared.

The copy queue is bounded by processor byte/packet limits. Strict overflow or branch errors fail processing rather than silently losing observations; the transport stays connected. Cancellation discards unprocessed copies and waits for accepted branch work to stop. Previously forwarded main packets cannot be retracted. Branch children share the graph thread and retain plugin leases through cancellation.

A new branch connects Input directly to Output. Input has no settings. Output can record the processed copy as JSONL, binary or HEX; enable recording and choose a filename. Relative filenames start in the open workspace folder. The original packet always bypasses branch transformations.
