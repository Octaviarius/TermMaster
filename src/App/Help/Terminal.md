# Terminal windows and input

A group owns tabs, local toolbar and status controls. Commands target the focused group in their window. Drag a tab onto another strip to join it, onto an edge to split, or outside to create a secondary window. Whole-group dragging retains its tabs and controls. Closing a secondary window returns its terminals to the main window.

Interactive input sends keyboard bytes. **Buffered input** retains a draft until submission; Shift+Enter inserts a newline. The configured terminator is appended. Display wrapping never changes transmitted bytes.

**Local echo** displays user input locally, separately from received data and terminal-generated replies. Use it when the device does not echo input; otherwise characters may appear twice.

File and periodic sending share the bounded pipeline. Acceptance into a queue is not proof of delivery. Stop sending cancels owned producers.

[Shortcuts](help:shortcuts) · [Processing](help:processing)
