"""Run AppTests slots in separate processes around Qt 6.12 offscreen ID reuse.

Each slot still exercises its complete lifecycle, including reentrant closure and
cross-session restoration. Native gesture acceptance remains an explicit opt-in
run of the executable on the Windows platform plugin.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    environment = os.environ.copy()
    environment["QT_QPA_PLATFORM"] = "offscreen"
    environment["QT_FORCE_STDERR_LOGGING"] = "1"
    # Physical pointer injection must never be inherited by this headless runner.
    environment.pop("TERMASTER_STICKY_DRAG_GESTURE", None)

    def run(arguments):
        return subprocess.run(
            [str(executable), *arguments],
            cwd=executable.parent,
            env=environment,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=args.timeout,
            check=False,
        )

    listing = run(["-functions"])
    if listing.returncode:
        print(listing.stdout + listing.stderr)
        return 1
    functions = re.findall(r"^([A-Za-z_]\w*)\(\)\s*$", listing.stdout, re.MULTILINE)
    if not functions or len(set(functions)) != len(functions):
        print("Could not enumerate unique AppTests slots", file=sys.stderr)
        return 1

    failures = []
    for function in functions:
        try:
            result = run([function])
        except subprocess.TimeoutExpired as error:
            print(f"FAIL {function}: exceeded {args.timeout:g} seconds")
            print(error.stdout or b"")
            print(error.stderr or b"")
            failures.append(function)
            continue
        output = result.stdout + result.stderr
        summaries = re.findall(r"^Totals:.*$", output, re.MULTILINE)
        if result.returncode or not summaries:
            print(f"FAIL {function}: exit {result.returncode}\n{output}")
            failures.append(function)
        else:
            print(f"PASS {function}: {summaries[-1]}", flush=True)

    print(f"AppTests: {len(functions) - len(failures)}/{len(functions)} isolated slots passed")
    if failures:
        print("Failed: " + ", ".join(failures))
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
