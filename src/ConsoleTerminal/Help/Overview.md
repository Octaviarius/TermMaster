# Local console

Windows consoles use ConPTY for interactive terminal behavior, distinct from plain stdio pipes. Elevation uses a one-shot helper; the main Widgets application stays unelevated. Closing and cancellation release owned process/job resources.

[Terminal input](help:terminal) · [Settings](help:settings)
