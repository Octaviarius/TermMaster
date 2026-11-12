"""Run the installed Codegraph CLI/MCP against this repository.

Uses the executable already configured for the user-level Codex Codegraph MCP.
Does not install packages or change another project's server configuration.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tomllib


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["init", "index", "sync", "status", "query", "explore", "serve"])
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    options = parser.parse_args()
    config_path = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")) / "config.toml"
    try:
        settings = tomllib.loads(config_path.read_text(encoding="utf-8"))
        server = settings["mcp_servers"]["codegraph"]
        args = server.get("args", [])
        # Keep the configured Node runtime, VM flags and CLI entry point;
        # replace only the server command and its other-project path.
        prefix = args[:args.index("serve")]
        command = [server["command"], *prefix, options.command]
    except (OSError, ValueError, KeyError, tomllib.TOMLDecodeError) as error:
        print(f"Cannot resolve the installed Codegraph MCP from {config_path}: {error}", file=sys.stderr)
        return 1

    if options.command in {"query", "explore"}:
        command += ["--path", str(ROOT), *options.arguments]
    elif options.command == "serve":
        command += ["--mcp", "--path", str(ROOT), *options.arguments]
    else:
        command += [*options.arguments, str(ROOT)]

    environment = os.environ.copy()
    environment["CODEGRAPH_TELEMETRY"] = "0"
    try:
        return subprocess.call(command, cwd=ROOT, env=environment)
    except OSError as error:
        print(f"Cannot launch Codegraph: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
