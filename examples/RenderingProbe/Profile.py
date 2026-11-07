"""Run identical synthetic fixtures in separate renderer processes; no downloads."""

import argparse
import json
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--qt-bin", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--cols", type=int, default=140)
    parser.add_argument("--rows", type=int, default=42)
    parser.add_argument("--scale", default="1")
    parser.add_argument("--methods", type=int, nargs="+", choices=range(1, 6), default=list(range(1, 6)))
    parser.add_argument("--cases", nargs="+", choices=["static", "cold", "change", "scroll", "change-partial"],
                        default=["static", "cold", "change", "scroll", "change-partial"])
    args = parser.parse_args()
    if not 1 <= args.repetitions <= 10:
        parser.error("repetitions must be between 1 and 10")

    executable = args.executable.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output.resolve()
    env = dict(os.environ)
    env["PATH"] = str(args.qt_bin.resolve(strict=True)) + os.pathsep + env["PATH"]
    env["QT_SCALE_FACTOR"] = args.scale
    cases = [(method, workload, partial, repetition)
             for repetition in range(args.repetitions)
             for method in args.methods
             for workload, partial in [(case.removesuffix("-partial"), case.endswith("-partial"))
                                       for case in args.cases]]
    random.Random(137).shuffle(cases)
    grouped = {}
    for method, workload, partial, repetition in cases:
        name = f"method{method}-{workload}{'-partial' if partial else ''}-r{repetition}"
        destination = output / f"{name}.json"
        command = [str(executable), "--method", str(method), "--workload", workload,
                   "--frames", str(args.frames), "--cols", str(args.cols),
                   "--rows", str(args.rows), "--output", str(destination)]
        if partial:
            command.append("--partial")
        completed = subprocess.run(command, env=env, timeout=70, capture_output=True, text=True)
        if completed.returncode:
            raise RuntimeError(f"{name}: exit {completed.returncode}\n{completed.stderr}")
        data = json.loads(destination.read_text(encoding="utf-8"))
        if len(data["samples"]) != args.frames:
            raise RuntimeError(f"{name}: unexpected sample count")
        for sample in data["samples"]:
            expected_layouts = (args.rows if workload == "cold" or method == 1
                                else 0 if workload == "static" else 1)
            if method == 4 and workload == "scroll" and not float(data["row_height"] * data["dpr"]).is_integer():
                expected_layouts = args.rows
            if partial and method == 1:
                expected_layouts = 1
            if sample["layouts_built"] != expected_layouts:
                raise RuntimeError(f"{name}: cache invalidation mismatch: {sample}")
            if method == 4 and sample["tiles_built"] != expected_layouts:
                raise RuntimeError(f"{name}: tile cache invalidation mismatch: {sample}")
        key = (method, workload, partial)
        grouped.setdefault(key, []).append(data)
        print(f"{name}: {data['render_p50_ms']:.3f} ms p50; "
              f"{data['render_p95_ms']:.3f} ms p95", flush=True)

    summary = {"host": platform.platform(), "processor": platform.processor(),
               "columns": args.cols, "rows": args.rows, "frames": args.frames,
               "repetitions": args.repetitions, "scale": args.scale, "results": []}
    for (method, workload, partial), runs in sorted(grouped.items()):
        samples = [sample for run in runs for sample in run["samples"]]
        summary["results"].append({
            "method": method, "workload": workload, "partial": partial,
            "qt": runs[0]["qt"], "gpu": runs[0]["gpu"], "dpr": runs[0]["dpr"],
            "font": runs[0]["font"], "width": runs[0]["width"], "height": runs[0]["height"],
            "median_run_p50_ms": statistics.median(run["render_p50_ms"] for run in runs),
            "median_run_p95_ms": statistics.median(run["render_p95_ms"] for run in runs),
            "median_prepare_ms": statistics.median(sample["prepare_ms"] for sample in samples),
            "median_draw_ms": statistics.median(sample["draw_ms"] for sample in samples),
            "median_request_ms": statistics.median(sample["request_to_completion_ms"] for sample in samples),
            "median_private_mib": statistics.median(run["private_mib"] for run in runs),
        })
    (output / "Summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"Validated {len(cases)} runs. Summary: {output / 'Summary.json'}")


if __name__ == "__main__":
    main()
