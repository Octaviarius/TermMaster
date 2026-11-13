# Workspaces and sessions

A workspace is a chosen file independent of application configuration. It owns the session catalog, recent sessions, processing favorites and reusable processor definitions. Processor files live under `<workspace-file>.d/processors/`.

Sessions have stable IDs and mutable names. Each session.yaml stores terminals, settings and layout. Catalog removal does not delete files. Exit and workspace replacement save the active session; reopening restores the last session and reconnects terminals that were connected when saved. Disconnected terminals stay disconnected.

Application defaults are overridden by workspace, session and terminal settings. Reset removes a local override rather than copying inherited values.

[Terminal windows](help:terminal) · [Settings](help:settings)
