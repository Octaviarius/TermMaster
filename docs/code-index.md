# Local code index

Codegraph 1.6.2 indexes Termaster source and tests through its installed MCP server.
The project configuration excludes vendored components, build output and assets.
Generated SQLite data stays in `.codegraph/`, which is ignored by Git.

## Use

Call `codegraph_explore` with `projectPath` set to the absolute TermMaster root.
Explicit project selection matters: the existing user-level MCP default points to
another repository. No global server configuration is changed by this setup.

The project helper resolves the already installed executable and Node flags from
the user-level Codex `mcp_servers.codegraph` entry, then targets this repository:

```powershell
python tools/Codegraph.py status --json
python tools/Codegraph.py sync
python tools/Codegraph.py query moveTerminal --limit 5
python tools/Codegraph.py explore TerminalController moveTerminal
python tools/Codegraph.py serve
```

`serve` starts the stdio MCP server with the repository as its root and the normal
file watcher enabled. It expects an MCP client on stdin/stdout; it is not a detached
background service. The helper requires Python 3.11+ and an existing installed
Codegraph MCP entry. It performs no downloads and disables telemetry for its child
process. It does not copy environment credentials from the MCP configuration.

Queries selecting a second project through `projectPath` may have no live watcher;
run `sync` after changes and check the freshness banner. A full rebuild is available
with `index`; initial setup uses `init --yes`.

## Verification and limits

The initial index completed with 371 files, 6800 nodes and 25396 edges. A real MCP
query returned current source and the call chain from TerminalController through
`_wireWorkspace` to `moveTerminal`, plus the tab drag implementation and callers.
After adding the helper and synchronizing, status reported 372 files, 6809 nodes
and 25407 edges, with no pending changes or references. The helper's stdio server
completed initialize/tools-list/explore requests for this repository and shut down
cleanly after client disconnect.

The parser reported no symbols for
`src/Core/inc/Termaster/Core/Gui/Plugin/PluginManagerGui.h`; a direct filename MCP
query returned no relevant code. Use direct reads for that header. No project
source is changed to accommodate an index parser limitation. Cross-file C++ call
resolution is approximate and is not a replacement for compiler/test validation.
