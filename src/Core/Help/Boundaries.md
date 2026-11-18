# Boundary recording

Select the Initial/Device or Final/Terminal boundary control in Lineflow. Boundaries are fixed and do not appear in the palette. Recording is disabled by default; editing a form does not open files.

## Packets

Choose JSONL, binary or HEX. JSONL preserves packet boundaries with UTC time, an exact index, direction, byte length and Base64 payload. Choose a direction in paired flows. `{direction}` in the absolute filename expands to `rx`, `tx` or `stream`; binary recording in both directions requires separate directional files. Create the parent directory first.

Append mode, maximum file size, backup count and flush frequency are configurable. Writers hold an exclusive file lease, rotate bounded backups and stop on recording errors. Processing errors do not disconnect the transport. Already forwarded data cannot be rolled back.

## Terminal lines

Only the actual terminal root's final boundary can record rendered text after escape processing. Choose **lines** to record UTF-8 logical lines after they leave the mutable primary screen for history. Carriage-return progress redraws and cursor edits finish before recording; escape bytes and color commands are not written as text. Soft-wrapped source rows join one logical line, independently of visual wrapping.

Choose LF/CRLF and whether to trim trailing spaces. **Flush active screen** includes remaining current text when recording stops; turn it off to keep only completed history lines. An unfinished soft-wrap prefix is then discarded. Alternate screens do not produce history records; the optional close snapshot includes the visible alternate screen. Local echo belongs to the displayed screen and can appear in these records.

File writes run off the GUI thread with bounded admission. Queued records and each logical line are bounded to 1 MiB, with at most 1024 pending records. Stopping drains and flushes the owned worker before releasing terminal resources. A rendered-line file error is reported without interrupting the connection. Nested composition boundaries do not claim to observe the terminal display.
