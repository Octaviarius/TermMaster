"""Validate and measure regex engines in sequential isolated Windows processes."""

import argparse
import csv
import hashlib
import ctypes
import itertools
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time


ENGINES = ["pcre2", "pcre2-jit", "pcre2-dfa", "vectorscan", "vectorscan-pcre2"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--target-seconds", type=float, default=0.12)
    parser.add_argument("--cpu", type=int, default=4)
    parser.add_argument("--mingw", type=Path, default=Path("C:/Qt/Tools/mingw1310_64"))
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()
    if args.repetitions < 1 or not math.isfinite(args.target_seconds) or args.target_seconds <= 0:
        parser.error("Use positive repetitions and target duration")
    if args.cpu < 0 or args.cpu >= (os.cpu_count() or 1):
        parser.error("CPU index is outside the available logical processors")
    root = Path(__file__).resolve().parents[3]
    exe = (args.exe or root / "build/RegexBenchmark/bench-mingw/RegexBenchmark.exe").resolve()
    output = args.output or root / "docs/benchmarks/regex"
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(args.mingw / "bin") + os.pathsep + env["PATH"]

    affinity = ctypes.windll.kernel32.SetProcessAffinityMask
    affinity.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    affinity.restype = ctypes.c_int

    def invoke(engine, scenario, chunk, payload, mib, iterations=1):
        command = [str(exe), "--engine", engine, "--scenario", scenario,
                   "--chunk", str(chunk), "--payload", str(payload), "--mib", str(mib),
                   "--iterations", str(iterations)]
        child = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        if not affinity(int(child._handle), 1 << args.cpu):
            child.kill()
            child.wait()
            raise RuntimeError("Cannot set benchmark process affinity")
        try:
            stdout, stderr = child.communicate(timeout=120)
        except subprocess.TimeoutExpired:
            child.kill()
            child.communicate()
            raise RuntimeError(f"Benchmark timed out: {command}")
        if child.returncode:
            raise RuntimeError(f"Benchmark failed: {command}\n{stderr}")
        return json.loads(stdout)

    # Bytewise input forces every header, payload and terminator boundary to split.
    checks = []
    for engine, scenario, chunk in itertools.product(ENGINES, ["frames", "noise", "near-miss"],
                                                     [1, 2, 3, 17, 257, 4096, 65536]):
        checks.append(invoke(engine, scenario, chunk, 64, 1))
    for engine in ENGINES:
        checks.append(invoke(engine, "frames", 1, 16384, 1))
    (output / "Correctness.json").write_text(json.dumps(checks, indent=2) + "\n", encoding="utf-8")
    print(f"Correctness: {len(checks)} jobs passed", flush=True)

    chunks = [1, 17, 256, 4096, 65536] if not args.quick else [1, 4096]
    cases = [(s, c, 64, 8) for s in ["frames", "noise", "near-miss"] for c in chunks]
    cases += [("frames", c, 16384, 1 if c <= 17 else 8) for c in chunks]
    rows = []
    raw_path = output / "RawResults.jsonl"
    start = time.time()
    with raw_path.open("w", encoding="utf-8") as raw:
        for case_index, (scenario, chunk, payload, mib) in enumerate(cases, 1):
            for engine in ENGINES:
                calibration = invoke(engine, scenario, chunk, payload, mib)
                iterations = min(128, max(1, math.ceil(args.target_seconds / calibration["seconds"])))
                samples = []
                for repetition in range(args.repetitions):
                    sample = invoke(engine, scenario, chunk, payload, mib, iterations)
                    sample["repetition"] = repetition
                    raw.write(json.dumps(sample, separators=(",", ":")) + "\n")
                    raw.flush()
                    samples.append(sample)
                speed = [x["bytes"] / x["seconds"] / (1024 * 1024) for x in samples]
                representative = samples[0]
                known_bytes = sum(representative.get(key, 0) for key in [
                    "engine_static_bytes", "pcre2_match_data_bytes", "pcre2_heap_frames_bytes",
                    "dfa_workspace_bytes", "dfa_match_data_bytes", "vectorscan_scratch_bytes",
                    "vectorscan_stream_bytes", "buffer_capacity_bytes",
                ])
                row = dict(engine=engine, scenario=scenario, chunk=chunk, payload=payload, mib=mib,
                           iterations=iterations, repetitions=args.repetitions,
                           median_mib_s=statistics.median(speed), min_mib_s=min(speed), max_mib_s=max(speed),
                           median_compile_ms=statistics.median(x["compile_ms"] for x in samples),
                           known_engine_and_buffer_bytes=known_bytes,
                           buffer_peak=max(x["buffer_peak"] for x in samples),
                           median_private_bytes=statistics.median(x["process_private_bytes"] for x in samples),
                           median_peak_working_set_bytes=statistics.median(x["peak_working_set_bytes"] for x in samples),
                           rescan_factor=representative["scanned_subject_bytes"] / representative["bytes"])
                rows.append(row)
                print(f"{case_index}/{len(cases)} {engine:18} {scenario:9} chunk={chunk:5} payload={payload:5} "
                      f"{row['median_mib_s']:9.2f} MiB/s", flush=True)
    with (output / "Summary.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    cpu = json.loads(subprocess.check_output([
        "pwsh", "-NoProfile", "-Command",
        "Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores | ConvertTo-Json -Compress",
    ], text=True))
    harness = Path(__file__).resolve().parent / "RegexBenchmark.cpp"
    metadata = dict(harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                    cpu_model=cpu["Name"].strip(), physical_cores=cpu["NumberOfCores"],
                    pcre2="10.49", vectorscan="5.4.14", vectorscan_target="core-avx2",
                    build_type="Release -O3 -DNDEBUG", date=time.strftime("%Y-%m-%d"),
                    platform=platform.platform(), python=platform.python_version(), cpu_affinity=args.cpu,
                    logical_processors=os.cpu_count(), repetitions=args.repetitions,
                    target_seconds=args.target_seconds, elapsed_seconds=time.time() - start,
                    correctness_jobs=len(checks), exe=str(exe), engines=ENGINES,
                    compiler=subprocess.check_output([str(args.mingw / "bin/g++.exe"), "--version"],
                                                     env=env, text=True).splitlines()[0])
    (output / "Environment.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"Saved {len(rows)} measurements to {output}", flush=True)


if __name__ == "__main__":
    main()
