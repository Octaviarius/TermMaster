"""Validate paired resize probes and summarize native latency distributions."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import statistics


def summarize(directory):
    status = json.loads((directory / "Runs.json").read_text(encoding="utf-8"))
    assert status["sourceUnchanged"] and not status["serialOptIn"], directory
    assert all(run["exitCode"] == 0 and not run["timedOut"] for run in status["runs"]), directory
    groups = defaultdict(list)
    for path in sorted(directory.glob("Run[0-9]*.json")):
        records = json.loads(path.read_text(encoding="utf-8"))["measurements"]
        assert all(item["ok"] for item in records), path
        for item in records:
            if item["stage"].startswith("window.resize."):
                groups[item["stage"]].append(item)
    assert len(groups) == 4, directory
    result = {}
    for name, records in groups.items():
        values = sorted(item["wallMs"] for item in records)
        result[name] = {"samples": len(values), "medianMs": statistics.median(values),
                        "p95Ms": values[math.ceil(len(values) * 0.95) - 1], "maximumMs": values[-1]}
        for field in ("geometryMs", "eventsMs"):
            if all(field in item for item in records):
                result[name][field] = statistics.median(item[field] for item in records)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    result = {}
    for build in ("release", "debug"):
        before = summarize(args.root / ("before-" + build))
        after = summarize(args.root / ("after-" + build))
        result[build] = {name: {"before": before[name], "after": after[name],
                               "speedup": before[name]["medianMs"] / after[name]["medianMs"]}
                         for name in before}
    (args.root / "Summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for build, stages in result.items():
        for name, item in stages.items():
            print(f"{build} {name}: {item['before']['medianMs']:.3f} -> "
                  f"{item['after']['medianMs']:.3f} ms ({item['speedup']:.2f}x)")


if __name__ == "__main__":
    main()
