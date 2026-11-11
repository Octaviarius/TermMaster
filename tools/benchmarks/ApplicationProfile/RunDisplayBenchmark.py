"""Measure the project renderer with visual wrapping enabled and disabled."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--scratch", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--repetitions", type=int, default=2)
    args = parser.parse_args()
    if args.repetitions < 1:
        parser.error("repetitions must be positive")
    args.scratch.mkdir(parents=True, exist_ok=False)
    args.output.mkdir(parents=True, exist_ok=False)
    summary = {}
    for content in ("ascii", "mixed"):
        for wrap in (False, True):
            name = content + ("-wrap" if wrap else "-nowrap")
            env = os.environ.copy()
            env.update(QT_QPA_PLATFORM="windows", QT_SCALE_FACTOR="1",
                       TERMASTER_PROFILE_DISPLAY_COMPARE="1",
                       TERMASTER_PROFILE_DISPLAY_CONTENT=content,
                       TERMASTER_PROFILE_DISPLAY_WRAP=str(int(wrap)))
            subprocess.run([sys.executable, str(Path(__file__).with_name("Run.py")),
                            "--source", str(args.source), "--scratch", str(args.scratch / name),
                            "--output", str(args.output / name), "--build", str(args.build),
                            "--repetitions", str(args.repetitions), "--timeout", "300"], env=env, check=True)
            status = json.loads((args.output / name / "Runs.json").read_text(encoding="utf-8"))
            if not status["sourceUnchanged"] or any(r["exitCode"] or r["timedOut"] for r in status["runs"]):
                raise RuntimeError("Process or source-preservation check failed: " + name)
            stages = {}
            for report in sorted((args.output / name).glob("Run[0-9]*.json")):
                data = json.loads(report.read_text(encoding="utf-8"))
                for item in data["measurements"]:
                    if item.get("ok") is False:
                        raise RuntimeError(str(item))
                    if item["stage"].startswith("compare.") and "wallMs" in item:
                        stages.setdefault(item["stage"], []).append(item["wallMs"])
            summary[name] = {stage: {"samples": len(values), "medianMs": statistics.median(values),
                                    "p95Ms": sorted(values)[min(len(values)-1, int(len(values)*.95))],
                                    "maxMs": max(values)} for stage, values in stages.items()}
    (args.output / "Summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
